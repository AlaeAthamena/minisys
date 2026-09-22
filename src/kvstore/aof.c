#include "kvstore.h"

bool aof_init(kv_store_t *store, const char *filename) {
    if (!store || !filename) return false;
    store->aof_filename = strdup(filename);
    pthread_mutex_init(&store->aof_lock, NULL);
    store->aof_fp = fopen(filename, "a+");
    return (store->aof_fp != NULL);
}

void aof_append_cmd(kv_store_t *store, const char *cmd, const char *key, const char *val, int ttl) {
    if (!store || !store->aof_fp || store->is_replaying) return;

    pthread_mutex_lock(&store->aof_lock);
    if (strcmp(cmd, "SET") == 0) {
        fprintf(store->aof_fp, "SET %s %s %d\n", key, val ? val : "", ttl);
    } else if (strcmp(cmd, "DEL") == 0) {
        fprintf(store->aof_fp, "DEL %s\n", key);
    } else if (strcmp(cmd, "EXPIRE") == 0) {
        fprintf(store->aof_fp, "EXPIRE %s %d\n", key, ttl);
    }
    fflush(store->aof_fp);
    pthread_mutex_unlock(&store->aof_lock);
}

bool aof_replay(kv_store_t *store) {
    if (!store || !store->aof_filename) return false;
    FILE *fp = fopen(store->aof_filename, "r");
    if (!fp) return false;

    store->is_replaying = true;

    char line[BUFFER_SIZE];
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\r\n")] = 0; // Strip newline
        if (strlen(line) == 0) continue;

        char cmd[16] = {0};
        char key[MAX_KEY_LEN] = {0};
        char val[MAX_VAL_LEN] = {0};
        int ttl = 0;

        int scanned = sscanf(line, "%15s %255s %4095s %d", cmd, key, val, &ttl);
        if (scanned >= 2) {
            if (strcmp(cmd, "SET") == 0) {
                kv_set(store, key, val, ttl);
            } else if (strcmp(cmd, "DEL") == 0) {
                kv_del(store, key);
            } else if (strcmp(cmd, "EXPIRE") == 0) {
                kv_expire(store, key, ttl);
            }
        }
    }
    fclose(fp);
    store->is_replaying = false;
    return true;
}
