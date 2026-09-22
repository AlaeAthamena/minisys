#include "kvstore.h"

// Forward declarations for internal helper functions
void lru_add(lru_list_t *lru, kv_entry_t *entry);
void lru_remove(lru_list_t *lru, kv_entry_t *entry);
void lru_touch(lru_list_t *lru, kv_entry_t *entry);
kv_entry_t *lru_pop_tail(lru_list_t *lru);

bool ttl_heap_push(ttl_heap_t *heap, kv_entry_t *entry);
kv_entry_t *ttl_heap_peek(ttl_heap_t *heap);
kv_entry_t *ttl_heap_pop(ttl_heap_t *heap);
void ttl_heap_remove(ttl_heap_t *heap, kv_entry_t *entry);

kv_store_t *kv_store_create(size_t initial_capacity, size_t max_lru_capacity, const char *aof_path) {
    kv_store_t *store = calloc(1, sizeof(kv_store_t));
    if (!store) return NULL;

    store->capacity = initial_capacity > 0 ? initial_capacity : DEFAULT_INITIAL_CAPACITY;
    store->buckets = calloc(store->capacity, sizeof(kv_entry_t *));
    if (!store->buckets) {
        free(store);
        return NULL;
    }

    pthread_rwlock_init(&store->lock, NULL);
    store->lru.max_capacity = max_lru_capacity > 0 ? max_lru_capacity : 10000;

    if (aof_path) {
        aof_init(store, aof_path);
        aof_replay(store);
    }

    return store;
}

static void free_entry(kv_entry_t *entry) {
    if (!entry) return;
    free(entry->key);
    free(entry->value);
    free(entry);
}

void kv_clean_expired(kv_store_t *store) {
    time_t now = time(NULL);
    while (store->ttl_heap.size > 0) {
        kv_entry_t *top = ttl_heap_peek(&store->ttl_heap);
        if (top && top->expire_at <= now) {
            kv_del(store, top->key);
        } else {
            break;
        }
    }
}

static void kv_resize(kv_store_t *store) {
    size_t new_cap = store->capacity * 2;
    kv_entry_t **new_buckets = calloc(new_cap, sizeof(kv_entry_t *));
    if (!new_buckets) return;

    for (size_t i = 0; i < store->capacity; i++) {
        kv_entry_t *curr = store->buckets[i];
        while (curr) {
            kv_entry_t *next = curr->next;
            size_t new_idx = curr->hash % new_cap;
            curr->next = new_buckets[new_idx];
            new_buckets[new_idx] = curr;
            curr = next;
        }
    }

    free(store->buckets);
    store->buckets = new_buckets;
    store->capacity = new_cap;
}

bool kv_set(kv_store_t *store, const char *key, const char *value, int ttl_seconds) {
    if (!store || !key || !value) return false;

    pthread_rwlock_wrlock(&store->lock);

    uint32_t hash = murmur3_32(key, strlen(key), 0x97f4b007);
    size_t bucket_idx = hash % store->capacity;

    time_t now = time(NULL);
    time_t expire_at = ttl_seconds > 0 ? now + ttl_seconds : 0;

    kv_entry_t *curr = store->buckets[bucket_idx];
    while (curr) {
        if (strcmp(curr->key, key) == 0) {
            // Update existing key
            free(curr->value);
            curr->value = strdup(value);
            curr->val_len = strlen(value);
            curr->expire_at = expire_at;

            lru_touch(&store->lru, curr);

            if (expire_at > 0) {
                if (curr->heap_idx != (size_t)-1) {
                    ttl_heap_remove(&store->ttl_heap, curr);
                }
                ttl_heap_push(&store->ttl_heap, curr);
            }

            pthread_rwlock_unlock(&store->lock);
            aof_append_cmd(store, "SET", key, value, ttl_seconds);
            return true;
        }
        curr = curr->next;
    }

    // Evict LRU if capacity reached
    if (store->count >= store->lru.max_capacity) {
        kv_entry_t *evicted = lru_pop_tail(&store->lru);
        if (evicted) {
            // Delete from bucket chain
            size_t ev_idx = evicted->hash % store->capacity;
            kv_entry_t **p = &store->buckets[ev_idx];
            while (*p) {
                if (*p == evicted) {
                    *p = evicted->next;
                    break;
                }
                p = &(*p)->next;
            }
            if (evicted->heap_idx != (size_t)-1) {
                ttl_heap_remove(&store->ttl_heap, evicted);
            }
            free_entry(evicted);
            store->count--;
        }
    }

    // Create new entry
    kv_entry_t *entry = calloc(1, sizeof(kv_entry_t));
    entry->key = strdup(key);
    entry->value = strdup(value);
    entry->val_len = strlen(value);
    entry->hash = hash;
    entry->expire_at = expire_at;
    entry->heap_idx = (size_t)-1;

    entry->next = store->buckets[bucket_idx];
    store->buckets[bucket_idx] = entry;
    store->count++;

    lru_add(&store->lru, entry);

    if (expire_at > 0) {
        ttl_heap_push(&store->ttl_heap, entry);
    }

    // Resize if load factor > 0.75
    if ((double)store->count / store->capacity > 0.75) {
        kv_resize(store);
    }

    pthread_rwlock_unlock(&store->lock);
    aof_append_cmd(store, "SET", key, value, ttl_seconds);
    return true;
}

bool kv_get(kv_store_t *store, const char *key, char *out_val, size_t max_val_len, size_t *out_val_len) {
    if (!store || !key || !out_val) return false;

    pthread_rwlock_rdlock(&store->lock);

    uint32_t hash = murmur3_32(key, strlen(key), 0x97f4b007);
    size_t bucket_idx = hash % store->capacity;
    time_t now = time(NULL);

    kv_entry_t *curr = store->buckets[bucket_idx];
    while (curr) {
        if (strcmp(curr->key, key) == 0) {
            if (curr->expire_at > 0 && curr->expire_at <= now) {
                pthread_rwlock_unlock(&store->lock);
                kv_del(store, key); // Expired
                return false;
            }

            strncpy(out_val, curr->value, max_val_len - 1);
            out_val[max_val_len - 1] = '\0';
            if (out_val_len) *out_val_len = strlen(out_val);

            lru_touch(&store->lru, curr);
            pthread_rwlock_unlock(&store->lock);
            return true;
        }
        curr = curr->next;
    }

    pthread_rwlock_unlock(&store->lock);
    return false;
}

bool kv_del(kv_store_t *store, const char *key) {
    if (!store || !key) return false;

    pthread_rwlock_wrlock(&store->lock);

    uint32_t hash = murmur3_32(key, strlen(key), 0x97f4b007);
    size_t bucket_idx = hash % store->capacity;

    kv_entry_t **pp = &store->buckets[bucket_idx];
    while (*pp) {
        kv_entry_t *curr = *pp;
        if (strcmp(curr->key, key) == 0) {
            *pp = curr->next;
            lru_remove(&store->lru, curr);
            if (curr->heap_idx != (size_t)-1) {
                ttl_heap_remove(&store->ttl_heap, curr);
            }
            free_entry(curr);
            store->count--;

            pthread_rwlock_unlock(&store->lock);
            aof_append_cmd(store, "DEL", key, NULL, 0);
            return true;
        }
        pp = &(*pp)->next;
    }

    pthread_rwlock_unlock(&store->lock);
    return false;
}

bool kv_exists(kv_store_t *store, const char *key) {
    char val[16];
    return kv_get(store, key, val, sizeof(val), NULL);
}

bool kv_expire(kv_store_t *store, const char *key, int ttl_seconds) {
    if (!store || !key) return false;

    pthread_rwlock_wrlock(&store->lock);

    uint32_t hash = murmur3_32(key, strlen(key), 0x97f4b007);
    size_t bucket_idx = hash % store->capacity;
    time_t now = time(NULL);

    kv_entry_t *curr = store->buckets[bucket_idx];
    while (curr) {
        if (strcmp(curr->key, key) == 0) {
            curr->expire_at = now + ttl_seconds;
            if (curr->heap_idx != (size_t)-1) {
                ttl_heap_remove(&store->ttl_heap, curr);
            }
            ttl_heap_push(&store->ttl_heap, curr);

            pthread_rwlock_unlock(&store->lock);
            aof_append_cmd(store, "EXPIRE", key, NULL, ttl_seconds);
            return true;
        }
        curr = curr->next;
    }

    pthread_rwlock_unlock(&store->lock);
    return false;
}

size_t kv_keys(kv_store_t *store, char ***out_keys) {
    if (!store || !out_keys) return 0;

    pthread_rwlock_rdlock(&store->lock);

    size_t count = store->count;
    if (count == 0) {
        *out_keys = NULL;
        pthread_rwlock_unlock(&store->lock);
        return 0;
    }

    char **keys = malloc(sizeof(char *) * count);
    size_t idx = 0;

    for (size_t i = 0; i < store->capacity && idx < count; i++) {
        kv_entry_t *curr = store->buckets[i];
        while (curr && idx < count) {
            keys[idx++] = strdup(curr->key);
            curr = curr->next;
        }
    }

    *out_keys = keys;
    pthread_rwlock_unlock(&store->lock);
    return idx;
}

void kv_free_keys(char **keys, size_t count) {
    if (!keys) return;
    for (size_t i = 0; i < count; i++) {
        free(keys[i]);
    }
    free(keys);
}

void kv_store_free(kv_store_t *store) {
    if (!store) return;

    pthread_rwlock_wrlock(&store->lock);
    for (size_t i = 0; i < store->capacity; i++) {
        kv_entry_t *curr = store->buckets[i];
        while (curr) {
            kv_entry_t *next = curr->next;
            free_entry(curr);
            curr = next;
        }
    }
    free(store->buckets);
    free(store->ttl_heap.data);

    if (store->aof_fp) {
        fclose(store->aof_fp);
    }
    if (store->aof_filename) {
        free(store->aof_filename);
    }

    pthread_rwlock_unlock(&store->lock);
    pthread_rwlock_destroy(&store->lock);
    free(store);
}
