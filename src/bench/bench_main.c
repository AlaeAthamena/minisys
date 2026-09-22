#include "minisys.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/time.h>

typedef struct {
    const char *host;
    int port;
    int requests_per_thread;
    const char *method;
    const char *path;
    const char *body;
    uint64_t *latencies_us;
    uint64_t success_count;
    uint64_t fail_count;
    uint64_t bytes_transferred;
    int thread_id;
} bench_worker_arg_t;

static double get_time_sec(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec * 1e-6;
}

static uint64_t get_time_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000ULL + tv.tv_usec;
}

static void *bench_worker(void *arg) {
    bench_worker_arg_t *w = (bench_worker_arg_t *)arg;

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(w->port);
    inet_pton(AF_INET, w->host, &serv_addr.sin_addr);

    char req[BUFFER_SIZE];
    int req_len = snprintf(req, sizeof(req),
                           "%s %s HTTP/1.1\r\n"
                           "Host: %s:%d\r\n"
                           "Content-Type: application/json\r\n"
                           "Content-Length: %zu\r\n"
                           "Connection: keep-alive\r\n"
                           "\r\n"
                           "%s",
                           w->method, w->path, w->host, w->port,
                           w->body ? strlen(w->body) : 0, w->body ? w->body : "");

    char resp[BUFFER_SIZE];

    for (int i = 0; i < w->requests_per_thread; i++) {
        uint64_t t0 = get_time_us();

        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            w->fail_count++;
            continue;
        }

        if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
            close(sock);
            w->fail_count++;
            continue;
        }

        write(sock, req, req_len);
        ssize_t n = read(sock, resp, sizeof(resp) - 1);

        uint64_t t1 = get_time_us();
        close(sock);

        if (n > 0) {
            w->success_count++;
            w->bytes_transferred += req_len + n;
            w->latencies_us[i] = (t1 - t0);
        } else {
            w->fail_count++;
            w->latencies_us[i] = 0;
        }
    }

    return NULL;
}

static int compare_uint64(const void *a, const void *b) {
    uint64_t arg1 = *(const uint64_t *)a;
    uint64_t arg2 = *(const uint64_t *)b;
    if (arg1 < arg2) return -1;
    if (arg1 > arg2) return 1;
    return 0;
}

int main(int argc, char *argv[]) {
    const char *host = "127.0.0.1";
    int port = DEFAULT_PORT;
    int concurrency = 10;
    int total_requests = 10000;
    const char *method = "GET";
    const char *path = "/api/stats";
    const char *body = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) concurrency = atoi(argv[++i]);
        else if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) total_requests = atoi(argv[++i]);
        else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) port = atoi(argv[++i]);
        else if (strcmp(argv[i], "-h") == 0 && i + 1 < argc) host = argv[++i];
        else if (strcmp(argv[i], "-m") == 0 && i + 1 < argc) method = argv[++i];
        else if (strcmp(argv[i], "-u") == 0 && i + 1 < argc) path = argv[++i];
    }

    if (concurrency <= 0) concurrency = 1;
    if (total_requests <= 0) total_requests = 1000;

    int req_per_thread = total_requests / concurrency;
    int actual_total_requests = req_per_thread * concurrency;

    printf("\033[1;36m=========================================================\n");
    printf("  minisys-bench: High-Concurrency C HTTP Load Generator\n");
    printf("=========================================================\033[0m\n");
    printf(" Target URL         : \033[1;32mhttp://%s:%d%s\033[0m\n", host, port, path);
    printf(" HTTP Method        : \033[1;33m%s\033[0m\n", method);
    printf(" Concurrency Level  : \033[1;34m%d workers\033[0m\n", concurrency);
    printf(" Total Requests     : \033[1;35m%d requests\033[0m\n", actual_total_requests);
    printf("---------------------------------------------------------\n");
    printf(" Running benchmark...\n");

    pthread_t *threads = malloc(sizeof(pthread_t) * concurrency);
    bench_worker_arg_t *args = malloc(sizeof(bench_worker_arg_t) * concurrency);

    double start_time = get_time_sec();

    for (int i = 0; i < concurrency; i++) {
        args[i].host = host;
        args[i].port = port;
        args[i].requests_per_thread = req_per_thread;
        args[i].method = method;
        args[i].path = path;
        args[i].body = body;
        args[i].latencies_us = malloc(sizeof(uint64_t) * req_per_thread);
        args[i].success_count = 0;
        args[i].fail_count = 0;
        args[i].bytes_transferred = 0;
        args[i].thread_id = i;

        pthread_create(&threads[i], NULL, bench_worker, &args[i]);
    }

    uint64_t total_success = 0;
    uint64_t total_fail = 0;
    uint64_t total_bytes = 0;

    uint64_t *all_latencies = malloc(sizeof(uint64_t) * actual_total_requests);
    size_t lat_idx = 0;

    for (int i = 0; i < concurrency; i++) {
        pthread_join(threads[i], NULL);
        total_success += args[i].success_count;
        total_fail += args[i].fail_count;
        total_bytes += args[i].bytes_transferred;

        for (int j = 0; j < req_per_thread; j++) {
            if (args[i].latencies_us[j] > 0) {
                all_latencies[lat_idx++] = args[i].latencies_us[j];
            }
        }
        free(args[i].latencies_us);
    }

    double end_time = get_time_sec();
    double duration = end_time - start_time;
    double rps = total_success / duration;
    double throughput_mb = (total_bytes / (1024.0 * 1024.0)) / duration;

    qsort(all_latencies, lat_idx, sizeof(uint64_t), compare_uint64);

    uint64_t min_lat = lat_idx > 0 ? all_latencies[0] : 0;
    uint64_t max_lat = lat_idx > 0 ? all_latencies[lat_idx - 1] : 0;
    uint64_t p50_lat = lat_idx > 0 ? all_latencies[(size_t)(lat_idx * 0.50)] : 0;
    uint64_t p95_lat = lat_idx > 0 ? all_latencies[(size_t)(lat_idx * 0.95)] : 0;
    uint64_t p99_lat = lat_idx > 0 ? all_latencies[(size_t)(lat_idx * 0.99)] : 0;

    uint64_t sum_lat = 0;
    for (size_t k = 0; k < lat_idx; k++) sum_lat += all_latencies[k];
    double avg_lat = lat_idx > 0 ? (double)sum_lat / lat_idx : 0;

    printf("\n\033[1;32mResults:\033[0m\n");
    printf("  Time Taken for Tests: \033[1;36m%.4f seconds\033[0m\n", duration);
    printf("  Successful Requests : \033[1;32m%llu\033[0m\n", (unsigned long long)total_success);
    printf("  Failed Requests     : \033[1;31m%llu\033[0m\n", (unsigned long long)total_fail);
    printf("  Requests Per Second : \033[1;33m%.2f req/sec\033[0m  \033[1;35m[QUANTIFIED METRIC]\033[0m\n", rps);
    printf("  Network Throughput  : \033[1;34m%.2f MB/sec\033[0m\n", throughput_mb);
    printf("\n\033[1;33mLatency Distribution (micro-seconds / ms):\033[0m\n");
    printf("  Min Latency         : %.2f ms (%llu us)\n", min_lat / 1000.0, (unsigned long long)min_lat);
    printf("  Avg Latency         : %.2f ms (%.0f us)\n", avg_lat / 1000.0, avg_lat);
    printf("  p50 (Median)        : %.2f ms (%llu us)\n", p50_lat / 1000.0, (unsigned long long)p50_lat);
    printf("  p95                 : %.2f ms (%llu us)\n", p95_lat / 1000.0, (unsigned long long)p95_lat);
    printf("  p99                 : %.2f ms (%llu us)\n", p99_lat / 1000.0, (unsigned long long)p99_lat);
    printf("  Max Latency         : %.2f ms (%llu us)\n", max_lat / 1000.0, (unsigned long long)max_lat);
    printf("---------------------------------------------------------\n");

    free(threads);
    free(args);
    free(all_latencies);

    return 0;
}
