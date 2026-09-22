#include "minisys.h"
#include "kvstore.h"
#include <unistd.h>
#include <assert.h>

static void test_basic_crud(void) {
    printf("[TEST] Hash Table Basic CRUD operations...");
    kv_store_t *store = kv_store_create(16, 100, NULL);
    assert(store != NULL);

    char val[MAX_VAL_LEN];
    size_t val_len;

    // SET
    assert(kv_set(store, "user:1", "Alice", 0));
    assert(kv_set(store, "user:2", "Bob", 0));

    // GET
    assert(kv_get(store, "user:1", val, sizeof(val), &val_len));
    assert(strcmp(val, "Alice") == 0);

    assert(kv_get(store, "user:2", val, sizeof(val), &val_len));
    assert(strcmp(val, "Bob") == 0);

    // EXISTS
    assert(kv_exists(store, "user:1"));
    assert(!kv_exists(store, "user:999"));

    // DEL
    assert(kv_del(store, "user:1"));
    assert(!kv_exists(store, "user:1"));

    kv_store_free(store);
    printf(" PASSED!\n");
}

static void test_lru_eviction(void) {
    printf("[TEST] LRU Eviction Policy...");
    // Limit max capacity to 3 items
    kv_store_t *store = kv_store_create(16, 3, NULL);

    kv_set(store, "k1", "v1", 0);
    kv_set(store, "k2", "v2", 0);
    kv_set(store, "k3", "v3", 0);

    // Touch k1 to make k2 least recently used
    char buf[64];
    kv_get(store, "k1", buf, sizeof(buf), NULL);

    // Add 4th item -> should evict k2
    kv_set(store, "k4", "v4", 0);

    assert(!kv_exists(store, "k2"));
    assert(kv_exists(store, "k1"));
    assert(kv_exists(store, "k3"));
    assert(kv_exists(store, "k4"));

    kv_store_free(store);
    printf(" PASSED!\n");
}

static void test_ttl_expiration(void) {
    printf("[TEST] TTL Expiration Min-Heap...");
    kv_store_t *store = kv_store_create(16, 100, NULL);

    // Set key with 1 sec TTL
    kv_set(store, "temp_key", "temp_val", 1);
    assert(kv_exists(store, "temp_key"));

    // Sleep 1.2s to trigger expiration
    usleep(1200000);

    assert(!kv_exists(store, "temp_key"));

    kv_store_free(store);
    printf(" PASSED!\n");
}

static void test_aof_persistence(void) {
    printf("[TEST] AOF Log File Snapshot & Replay...");
    const char *test_aof = "test_run.aof";
    unlink(test_aof);

    kv_store_t *store = kv_store_create(16, 100, test_aof);
    kv_set(store, "persistent1", "value1", 0);
    kv_set(store, "persistent2", "value2", 0);
    kv_del(store, "persistent1");
    kv_store_free(store);

    // Reload from AOF log
    kv_store_t *reloaded = kv_store_create(16, 100, test_aof);
    assert(!kv_exists(reloaded, "persistent1"));
    assert(kv_exists(reloaded, "persistent2"));

    char buf[64];
    kv_get(reloaded, "persistent2", buf, sizeof(buf), NULL);
    assert(strcmp(buf, "value2") == 0);

    kv_store_free(reloaded);
    unlink(test_aof);
    printf(" PASSED!\n");
}

typedef struct {
    kv_store_t *store;
    int thread_id;
    int num_ops;
} concurrent_arg_t;

static void *concurrent_worker(void *arg) {
    concurrent_arg_t *ca = (concurrent_arg_t *)arg;
    char key[64];
    char val[64];

    for (int i = 0; i < ca->num_ops; i++) {
        snprintf(key, sizeof(key), "t%d_k%d", ca->thread_id, i);
        snprintf(val, sizeof(val), "v%d", i);

        kv_set(ca->store, key, val, 0);

        char out[64];
        kv_get(ca->store, key, out, sizeof(out), NULL);

        if (i % 2 == 0) {
            kv_del(ca->store, key);
        }
    }
    return NULL;
}

static void test_concurrency(void) {
    printf("[TEST] Concurrent Multi-Threaded Access...");
    kv_store_t *store = kv_store_create(64, 10000, NULL);
    int num_threads = 8;
    int ops = 1000;

    pthread_t threads[8];
    concurrent_arg_t args[8];

    for (int i = 0; i < num_threads; i++) {
        args[i].store = store;
        args[i].thread_id = i;
        args[i].num_ops = ops;
        pthread_create(&threads[i], NULL, concurrent_worker, &args[i]);
    }

    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    kv_store_free(store);
    printf(" PASSED!\n");
}

int main(void) {
    printf("======================================\n");
    printf("  Running minisys Core Unit Test Suite\n");
    printf("======================================\n");
    stats_init();

    test_basic_crud();
    test_lru_eviction();
    test_ttl_expiration();
    test_aof_persistence();
    test_concurrency();

    printf("\nAll 5 core system tests passed cleanly!\n");
    return 0;
}
