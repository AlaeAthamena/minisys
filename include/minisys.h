#ifndef MINISYS_H
#define MINISYS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <pthread.h>

#define MINISYS_VERSION "1.0.0"
#define DEFAULT_PORT 8080
#define DEFAULT_THREAD_POOL_SIZE 8
#define DEFAULT_MAX_CONNECTIONS 1024
#define DEFAULT_INITIAL_CAPACITY 1024
#define MAX_KEY_LEN 256
#define MAX_VAL_LEN 4096
#define MAX_PATH_LEN 1024
#define BUFFER_SIZE 8192

// Global statistics counter
typedef struct {
    uint64_t total_connections;
    uint64_t total_requests;
    uint64_t total_keys;
    uint64_t get_hits;
    uint64_t get_misses;
    uint64_t set_ops;
    uint64_t del_ops;
    uint64_t bytes_sent;
    uint64_t bytes_received;
    time_t start_time;
    pthread_mutex_t lock;
} server_stats_t;

extern server_stats_t g_stats;

void stats_init(void);
void stats_increment_req(void);
void stats_increment_conn(void);

#endif // MINISYS_H
