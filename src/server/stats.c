#include "minisys.h"

server_stats_t g_stats;

void stats_init(void) {
    memset(&g_stats, 0, sizeof(server_stats_t));
    g_stats.start_time = time(NULL);
    pthread_mutex_init(&g_stats.lock, NULL);
}

void stats_increment_req(void) {
    pthread_mutex_lock(&g_stats.lock);
    g_stats.total_requests++;
    pthread_mutex_unlock(&g_stats.lock);
}

void stats_increment_conn(void) {
    pthread_mutex_lock(&g_stats.lock);
    g_stats.total_connections++;
    pthread_mutex_unlock(&g_stats.lock);
}
