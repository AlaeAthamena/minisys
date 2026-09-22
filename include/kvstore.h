#ifndef KVSTORE_H
#define KVSTORE_H

#include "minisys.h"

// Key-Value Entry Structure
typedef struct kv_entry {
    char *key;
    char *value;
    size_t val_len;
    time_t expire_at; // 0 if no expiration
    uint32_t hash;
    struct kv_entry *next;     // Hash collision chain
    struct kv_entry *lru_prev; // LRU list
    struct kv_entry *lru_next; // LRU list
    size_t heap_idx;          // TTL Min-Heap index
} kv_entry_t;

// LRU Doubly Linked List
typedef struct {
    kv_entry_t *head;
    kv_entry_t *tail;
    size_t count;
    size_t max_capacity;
} lru_list_t;

// TTL Min-Heap
typedef struct {
    kv_entry_t **data;
    size_t size;
    size_t capacity;
} ttl_heap_t;

// Hash Table with RW Lock
typedef struct {
    kv_entry_t **buckets;
    size_t capacity;
    size_t count;
    pthread_rwlock_t lock;
    lru_list_t lru;
    ttl_heap_t ttl_heap;
    char *aof_filename;
    FILE *aof_fp;
    pthread_mutex_t aof_lock;
    bool is_replaying;
} kv_store_t;

// API Functions
kv_store_t *kv_store_create(size_t initial_capacity, size_t max_lru_capacity, const char *aof_path);
void kv_store_free(kv_store_t *store);

bool kv_set(kv_store_t *store, const char *key, const char *value, int ttl_seconds);
bool kv_get(kv_store_t *store, const char *key, char *out_val, size_t max_val_len, size_t *out_val_len);
bool kv_del(kv_store_t *store, const char *key);
bool kv_exists(kv_store_t *store, const char *key);
bool kv_expire(kv_store_t *store, const char *key, int ttl_seconds);

size_t kv_keys(kv_store_t *store, char ***out_keys);
void kv_free_keys(char **keys, size_t count);

void kv_clean_expired(kv_store_t *store);

// AOF Log Functions
bool aof_init(kv_store_t *store, const char *filename);
void aof_append_cmd(kv_store_t *store, const char *cmd, const char *key, const char *val, int ttl);
bool aof_replay(kv_store_t *store);

// Hash helper function
uint32_t murmur3_32(const char *key, size_t len, uint32_t seed);

#endif // KVSTORE_H
