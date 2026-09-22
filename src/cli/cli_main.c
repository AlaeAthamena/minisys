#include "minisys.h"
#include <ctype.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

static void print_help(void) {
    printf("\033[1;33mAvailable CLI Commands:\033[0m\n");
    printf("  \033[1;32mSET <key> <value> [ttl_sec]\033[0m  - Store key-value pair with optional TTL\n");
    printf("  \033[1;32mGET <key>\033[0m                   - Retrieve value for key\n");
    printf("  \033[1;32mDEL <key>\033[0m                   - Delete key from database\n");
    printf("  \033[1;32mKEYS\033[0m                        - List all active keys in database\n");
    printf("  \033[1;32mEXPIRE <key> <ttl_sec>\033[0m      - Set expiration time for key\n");
    printf("  \033[1;32mSTATS\033[0m                       - Fetch server performance metrics\n");
    printf("  \033[1;32mHELP\033[0m                        - Display this help summary\n");
    printf("  \033[1;32mEXIT / QUIT\033[0m                 - Disconnect and exit REPL\n\n");
}

static bool send_http_request(const char *host, int port, const char *method, const char *path, const char *body, char *response_out, size_t max_resp) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return false;

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    inet_pton(AF_INET, host, &serv_addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        close(sock);
        return false;
    }

    char req[BUFFER_SIZE];
    int req_len = snprintf(req, sizeof(req),
                           "%s %s HTTP/1.1\r\n"
                           "Host: %s:%d\r\n"
                           "Content-Type: application/json\r\n"
                           "Content-Length: %zu\r\n"
                           "Connection: close\r\n"
                           "\r\n"
                           "%s",
                           method, path, host, port, body ? strlen(body) : 0, body ? body : "");

    write(sock, req, req_len);

    ssize_t n = read(sock, response_out, max_resp - 1);
    if (n > 0) {
        response_out[n] = '\0';
    } else {
        response_out[0] = '\0';
    }

    close(sock);
    return true;
}

int main(int argc, char *argv[]) {
    const char *host = "127.0.0.1";
    int port = DEFAULT_PORT;

    if (argc >= 2) host = argv[1];
    if (argc >= 3) port = atoi(argv[2]);

    printf("\033[1;36m==================================================\n");
    printf("  minisys CLI Interactive Shell (v%s)\n", MINISYS_VERSION);
    printf("  Connected to daemon at %s:%d\n", host, port);
    printf("  Type 'HELP' for available commands or 'EXIT' to quit.\n");
    printf("==================================================\033[0m\n\n");

    char line[1024];
    char resp[BUFFER_SIZE];

    while (1) {
        printf("\033[1;34mminisys>\033[0m ");
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) break;
        line[strcspn(line, "\r\n")] = '\0';

        if (strlen(line) == 0) continue;

        char cmd[32] = {0};
        char arg1[MAX_KEY_LEN] = {0};
        char arg2[MAX_VAL_LEN] = {0};
        int ttl = 0;

        int scanned = sscanf(line, "%31s %255s %4095s %d", cmd, arg1, arg2, &ttl);

        for (int i = 0; cmd[i]; i++) cmd[i] = toupper(cmd[i]);

        if (strcmp(cmd, "EXIT") == 0 || strcmp(cmd, "QUIT") == 0) {
            printf("Goodbye!\n");
            break;
        } else if (strcmp(cmd, "HELP") == 0) {
            print_help();
        } else if (strcmp(cmd, "STATS") == 0) {
            if (send_http_request(host, port, "GET", "/api/stats", NULL, resp, sizeof(resp))) {
                char *json_start = strstr(resp, "\r\n\r\n");
                printf("\033[1;32m%s\033[0m\n", json_start ? json_start + 4 : resp);
            } else {
                printf("\033[1;31mError: Unable to connect to server at %s:%d\033[0m\n", host, port);
            }
        } else if (strcmp(cmd, "KEYS") == 0) {
            if (send_http_request(host, port, "GET", "/api/kv", NULL, resp, sizeof(resp))) {
                char *json_start = strstr(resp, "\r\n\r\n");
                printf("\033[1;32m%s\033[0m\n", json_start ? json_start + 4 : resp);
            } else {
                printf("\033[1;31mError: Unable to connect to server at %s:%d\033[0m\n", host, port);
            }
        } else if (strcmp(cmd, "GET") == 0) {
            if (scanned < 2) {
                printf("\033[1;31mUsage: GET <key>\033[0m\n");
            } else {
                char path[MAX_PATH_LEN];
                snprintf(path, sizeof(path), "/api/kv?key=%s", arg1);
                if (send_http_request(host, port, "GET", path, NULL, resp, sizeof(resp))) {
                    char *json_start = strstr(resp, "\r\n\r\n");
                    printf("\033[1;32m%s\033[0m\n", json_start ? json_start + 4 : resp);
                } else {
                    printf("\033[1;31mError: Unable to connect to server at %s:%d\033[0m\n", host, port);
                }
            }
        } else if (strcmp(cmd, "SET") == 0) {
            if (scanned < 3) {
                printf("\033[1;31mUsage: SET <key> <value> [ttl_sec]\033[0m\n");
            } else {
                char json_payload[BUFFER_SIZE];
                snprintf(json_payload, sizeof(json_payload), "{\"key\": \"%s\", \"value\": \"%s\", \"ttl\": %d}", arg1, arg2, ttl);
                if (send_http_request(host, port, "POST", "/api/kv", json_payload, resp, sizeof(resp))) {
                    char *json_start = strstr(resp, "\r\n\r\n");
                    printf("\033[1;32m%s\033[0m\n", json_start ? json_start + 4 : resp);
                } else {
                    printf("\033[1;31mError: Unable to connect to server at %s:%d\033[0m\n", host, port);
                }
            }
        } else if (strcmp(cmd, "DEL") == 0) {
            if (scanned < 2) {
                printf("\033[1;31mUsage: DEL <key>\033[0m\n");
            } else {
                char path[MAX_PATH_LEN];
                snprintf(path, sizeof(path), "/api/kv?key=%s", arg1);
                if (send_http_request(host, port, "DELETE", path, NULL, resp, sizeof(resp))) {
                    char *json_start = strstr(resp, "\r\n\r\n");
                    printf("\033[1;32m%s\033[0m\n", json_start ? json_start + 4 : resp);
                } else {
                    printf("\033[1;31mError: Unable to connect to server at %s:%d\033[0m\n", host, port);
                }
            }
        } else {
            printf("\033[1;31mUnknown command '%s'. Type 'HELP' for command list.\033[0m\n", cmd);
        }
    }

    return 0;
}
