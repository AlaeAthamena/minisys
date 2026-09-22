#include "minisys.h"
#include "net.h"
#include "http.h"
#include "kvstore.h"
#include "threadpool.h"
#include <signal.h>

static volatile bool g_running = true;

static void signal_handler(int sig) {
    (void)sig;
    g_running = false;
}

static void print_banner(int port, int threads, const char *db_path, const char *web_root) {
    printf("\033[1;36m");
    printf("===============================================================\n");
    printf("  _ __ ___  |_ _ __ (_) ___ _   _ ___ \n");
    printf(" | '_ ` _ \\| | '_ \\| |/ __| | | / __|\n");
    printf(" | | | | | | | | | | |\\__ \\ |_| \\__ \\\n");
    printf(" |_| |_| |_|_|_| |_|_||___/\\__, |___/\n");
    printf("                           |___/     v%s\n", MINISYS_VERSION);
    printf(" High-Performance C Raw Socket HTTP Server & KV Database Engine\n");
    printf("===============================================================\033[0m\n");
    printf(" -> Listening on Port    : \033[1;32mhttp://127.0.0.1:%d\033[0m\n", port);
    printf(" -> Thread Pool Workers : \033[1;33m%d threads\033[0m\n", threads);
    printf(" -> Persistence Log     : \033[1;35m%s\033[0m\n", db_path ? db_path : "(In-Memory Only)");
    printf(" -> Web UI Control Panel: \033[1;34m%s\033[0m\n", web_root);
    printf("---------------------------------------------------------------\n");
    printf(" Ready to accept connections... Press Ctrl+C to terminate.\n\n");
}

int main(int argc, char *argv[]) {
    int port = DEFAULT_PORT;
    int threads = DEFAULT_THREAD_POOL_SIZE;
    const char *db_path = "minisys.aof";
    const char *web_root = "web";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            threads = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
            db_path = argv[++i];
        } else if (strcmp(argv[i], "-w") == 0 && i + 1 < argc) {
            web_root = argv[++i];
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [-p port] [-t threads] [-d aof_file] [-w web_root]\n", argv[0]);
            return 0;
        }
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    stats_init();

    kv_store_t *store = kv_store_create(1024, 50000, db_path);
    if (!store) {
        fprintf(stderr, "Failed to initialize key-value store database\n");
        return 1;
    }

    threadpool_t *pool = threadpool_create(threads);
    if (!pool) {
        fprintf(stderr, "Failed to create thread pool\n");
        kv_store_free(store);
        return 1;
    }

    int server_fd = create_server_socket(port);
    if (server_fd < 0) {
        fprintf(stderr, "Failed to create server socket on port %d\n", port);
        threadpool_destroy(pool);
        kv_store_free(store);
        return 1;
    }

    print_banner(port, threads, db_path, web_root);

    while (g_running) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);

        if (client_fd < 0) {
            if (!g_running) break;
            continue;
        }

        http_conn_ctx_t *ctx = malloc(sizeof(http_conn_ctx_t));
        if (!ctx) {
            close(client_fd);
            continue;
        }

        ctx->client_fd = client_fd;
        ctx->kv_store = store;
        ctx->web_root = web_root;

        if (!threadpool_add(pool, http_handle_connection, ctx)) {
            close(client_fd);
            free(ctx);
        }
    }

    printf("\nShutting down server daemon gracefully...\n");
    close(server_fd);
    threadpool_destroy(pool);
    kv_store_free(store);
    printf("Server stopped cleanly. Goodbye!\n");

    return 0;
}
