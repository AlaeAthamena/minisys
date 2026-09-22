#include "http.h"
#include <ctype.h>
#include <sys/stat.h>

static void url_decode(char *dst, const char *src) {
    char a, b;
    while (*src) {
        if ((*src == '%') && ((a = src[1]) && (b = src[2])) && (isxdigit(a) && isxdigit(b))) {
            if (a >= 'a' && a <= 'f') a = a - 'a' + 10;
            else if (a >= 'A' && a <= 'F') a = a - 'A' + 10;
            else a = a - '0';

            if (b >= 'a' && b <= 'f') b = b - 'a' + 10;
            else if (b >= 'A' && b <= 'F') b = b - 'A' + 10;
            else b = b - '0';

            *dst++ = 16 * a + b;
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

static const char *get_mime_type(const char *path) {
    const char *ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";
    if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0) return "text/html; charset=utf-8";
    if (strcmp(ext, ".css") == 0) return "text/css; charset=utf-8";
    if (strcmp(ext, ".js") == 0) return "application/javascript; charset=utf-8";
    if (strcmp(ext, ".json") == 0) return "application/json; charset=utf-8";
    if (strcmp(ext, ".png") == 0) return "image/png";
    if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) return "image/jpeg";
    if (strcmp(ext, ".svg") == 0) return "image/svg+xml";
    if (strcmp(ext, ".ico") == 0) return "image/x-icon";
    return "text/plain";
}

const char *http_get_header(const http_request_t *req, const char *name) {
    for (int i = 0; i < req->header_count; i++) {
        if (strcasecmp(req->headers[i].key, name) == 0) {
            return req->headers[i].value;
        }
    }
    return NULL;
}

bool http_parse_request(const char *raw_req, size_t len, http_request_t *req) {
    if (!raw_req || len == 0 || !req) return false;
    memset(req, 0, sizeof(http_request_t));
    req->keep_alive = true; // default for HTTP/1.1

    char *buf = strndup(raw_req, len);
    if (!buf) return false;

    char *line_save = NULL;
    char *first_line = strtok_r(buf, "\r\n", &line_save);
    if (!first_line) {
        free(buf);
        return false;
    }

    // Parse request line: METHOD PATH VERSION
    char full_path[MAX_PATH_LEN] = {0};
    char version_str[16] = {0};
    if (sscanf(first_line, "%15s %1023s %15s", req->method_str, full_path, version_str) < 2) {
        free(buf);
        return false;
    }

    if (strcmp(req->method_str, "GET") == 0) req->method = HTTP_GET;
    else if (strcmp(req->method_str, "POST") == 0) req->method = HTTP_POST;
    else if (strcmp(req->method_str, "PUT") == 0) req->method = HTTP_PUT;
    else if (strcmp(req->method_str, "DELETE") == 0) req->method = HTTP_DELETE;
    else if (strcmp(req->method_str, "OPTIONS") == 0) req->method = HTTP_OPTIONS;
    else req->method = HTTP_UNKNOWN;

    // Separate path and query string
    char *q = strchr(full_path, '?');
    if (q) {
        *q = '\0';
        strncpy(req->path, full_path, MAX_PATH_LEN - 1);
        url_decode(req->query, q + 1);
    } else {
        strncpy(req->path, full_path, MAX_PATH_LEN - 1);
    }

    // Parse headers
    char *hdr_line;
    while ((hdr_line = strtok_r(NULL, "\r\n", &line_save)) != NULL) {
        if (strlen(hdr_line) == 0) break; // empty line marks end of headers

        char *colon = strchr(hdr_line, ':');
        if (colon && req->header_count < MAX_HEADERS) {
            *colon = '\0';
            char *val = colon + 1;
            while (*val == ' ') val++; // skip leading whitespace

            strncpy(req->headers[req->header_count].key, hdr_line, 127);
            strncpy(req->headers[req->header_count].value, val, MAX_HEADER_LEN - 1);
            req->header_count++;
        }
    }

    const char *conn_hdr = http_get_header(req, "Connection");
    if (conn_hdr && strcasecmp(conn_hdr, "close") == 0) {
        req->keep_alive = false;
    }

    // Find body start in raw_req
    const char *body_start = strstr(raw_req, "\r\n\r\n");
    if (body_start) {
        body_start += 4;
        size_t header_len = body_start - raw_req;
        if (len > header_len) {
            req->body_len = len - header_len;
            req->body = malloc(req->body_len + 1);
            if (req->body) {
                memcpy(req->body, body_start, req->body_len);
                req->body[req->body_len] = '\0';
            }
        }
    }

    free(buf);
    return true;
}

void http_send_response(int client_fd, int status_code, const char *status_text,
                        const char *content_type, const char *body, size_t body_len,
                        bool keep_alive) {
    char header_buf[BUFFER_SIZE];
    int header_len = snprintf(header_buf, sizeof(header_buf),
                              "HTTP/1.1 %d %s\r\n"
                              "Server: minisys-c/%s\r\n"
                              "Content-Type: %s\r\n"
                              "Content-Length: %zu\r\n"
                              "Access-Control-Allow-Origin: *\r\n"
                              "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n"
                              "Access-Control-Allow-Headers: Content-Type\r\n"
                              "Connection: %s\r\n"
                              "\r\n",
                              status_code, status_text, MINISYS_VERSION,
                              content_type, body_len,
                              keep_alive ? "keep-alive" : "close");

    write(client_fd, header_buf, header_len);
    if (body && body_len > 0) {
        write(client_fd, body, body_len);
    }

    pthread_mutex_lock(&g_stats.lock);
    g_stats.bytes_sent += header_len + body_len;
    pthread_mutex_unlock(&g_stats.lock);
}

void http_send_json(int client_fd, int status_code, const char *json_body, bool keep_alive) {
    http_send_response(client_fd, status_code, "OK", "application/json; charset=utf-8",
                       json_body, strlen(json_body), keep_alive);
}

void http_send_error(int client_fd, int status_code, const char *message, bool keep_alive) {
    char json[512];
    snprintf(json, sizeof(json), "{\"error\": true, \"code\": %d, \"message\": \"%s\"}", status_code, message);
    http_send_response(client_fd, status_code, "Error", "application/json; charset=utf-8", json, strlen(json), keep_alive);
}

static void handle_static_file(int client_fd, const char *web_root, const char *req_path, bool keep_alive) {
    char filepath[MAX_PATH_LEN];
    const char *rel_path = req_path;

    if (strcmp(rel_path, "/") == 0) {
        rel_path = "/index.html";
    }

    snprintf(filepath, sizeof(filepath), "%s%s", web_root, rel_path);

    struct stat st;
    if (stat(filepath, &st) < 0 || S_ISDIR(st.st_mode)) {
        http_send_error(client_fd, 404, "File Not Found", keep_alive);
        return;
    }

    FILE *fp = fopen(filepath, "rb");
    if (!fp) {
        http_send_error(client_fd, 500, "Failed to read file", keep_alive);
        return;
    }

    char *file_buf = malloc(st.st_size);
    if (!file_buf) {
        fclose(fp);
        http_send_error(client_fd, 500, "Memory Allocation Failed", keep_alive);
        return;
    }

    fread(file_buf, 1, st.st_size, fp);
    fclose(fp);

    http_send_response(client_fd, 200, "OK", get_mime_type(filepath), file_buf, st.st_size, keep_alive);
    free(file_buf);
}

static void get_query_param(const char *query, const char *param, char *out_val, size_t max_len) {
    out_val[0] = '\0';
    if (!query || !param) return;

    char key_eq[128];
    snprintf(key_eq, sizeof(key_eq), "%s=", param);

    const char *p = strstr(query, key_eq);
    if (p) {
        p += strlen(key_eq);
        const char *amp = strchr(p, '&');
        size_t len = amp ? (size_t)(amp - p) : strlen(p);
        if (len >= max_len) len = max_len - 1;
        strncpy(out_val, p, len);
        out_val[len] = '\0';
    }
}

void http_handle_connection(void *arg) {
    http_conn_ctx_t *ctx = (http_conn_ctx_t *)arg;
    int fd = ctx->client_fd;
    kv_store_t *store = ctx->kv_store;
    const char *web_root = ctx->web_root;

    stats_increment_conn();

    char buf[BUFFER_SIZE];
    ssize_t nread = read(fd, buf, sizeof(buf) - 1);
    if (nread <= 0) {
        close(fd);
        free(ctx);
        return;
    }
    buf[nread] = '\0';

    pthread_mutex_lock(&g_stats.lock);
    g_stats.bytes_received += nread;
    pthread_mutex_unlock(&g_stats.lock);

    stats_increment_req();

    http_request_t req;
    if (!http_parse_request(buf, nread, &req)) {
        http_send_error(fd, 400, "Bad Request", false);
        close(fd);
        free(ctx);
        return;
    }

    if (req.method == HTTP_OPTIONS) {
        http_send_response(fd, 200, "OK", "text/plain", "", 0, req.keep_alive);
    } else if (strcmp(req.path, "/api/stats") == 0) {
        time_t uptime = time(NULL) - g_stats.start_time;
        pthread_rwlock_rdlock(&store->lock);
        size_t total_keys = store->count;
        pthread_rwlock_unlock(&store->lock);

        char json[1024];
        snprintf(json, sizeof(json),
                 "{"
                 "\"version\": \"%s\","
                 "\"uptime_seconds\": %ld,"
                 "\"total_connections\": %llu,"
                 "\"total_requests\": %llu,"
                 "\"total_keys\": %zu,"
                 "\"get_hits\": %llu,"
                 "\"get_misses\": %llu,"
                 "\"set_ops\": %llu,"
                 "\"del_ops\": %llu,"
                 "\"bytes_sent\": %llu,"
                 "\"bytes_received\": %llu"
                 "}",
                 MINISYS_VERSION, (long)uptime,
                 (unsigned long long)g_stats.total_connections,
                 (unsigned long long)g_stats.total_requests,
                 total_keys,
                 (unsigned long long)g_stats.get_hits,
                 (unsigned long long)g_stats.get_misses,
                 (unsigned long long)g_stats.set_ops,
                 (unsigned long long)g_stats.del_ops,
                 (unsigned long long)g_stats.bytes_sent,
                 (unsigned long long)g_stats.bytes_received);

        http_send_json(fd, 200, json, req.keep_alive);

    } else if (strcmp(req.path, "/api/kv") == 0) {
        if (req.method == HTTP_GET) {
            char key_param[MAX_KEY_LEN] = {0};
            get_query_param(req.query, "key", key_param, sizeof(key_param));

            if (strlen(key_param) > 0) {
                char val[MAX_VAL_LEN] = {0};
                size_t val_len = 0;
                if (kv_get(store, key_param, val, sizeof(val), &val_len)) {
                    pthread_mutex_lock(&g_stats.lock);
                    g_stats.get_hits++;
                    pthread_mutex_unlock(&g_stats.lock);

                    char json[MAX_VAL_LEN + 512];
                    snprintf(json, sizeof(json), "{\"key\": \"%s\", \"value\": \"%s\"}", key_param, val);
                    http_send_json(fd, 200, json, req.keep_alive);
                } else {
                    pthread_mutex_lock(&g_stats.lock);
                    g_stats.get_misses++;
                    pthread_mutex_unlock(&g_stats.lock);

                    http_send_error(fd, 404, "Key Not Found", req.keep_alive);
                }
            } else {
                // Return key list
                char **keys = NULL;
                size_t count = kv_keys(store, &keys);

                size_t buf_cap = count * (MAX_KEY_LEN + 10) + 256;
                char *json = malloc(buf_cap);
                if (!json) {
                    http_send_error(fd, 500, "Allocation Error", req.keep_alive);
                } else {
                    size_t pos = snprintf(json, buf_cap, "{\"count\": %zu, \"keys\": [", count);
                    for (size_t i = 0; i < count; i++) {
                        pos += snprintf(json + pos, buf_cap - pos, "%s\"%s\"", (i > 0 ? ", " : ""), keys[i]);
                    }
                    snprintf(json + pos, buf_cap - pos, "]}");
                    http_send_json(fd, 200, json, req.keep_alive);
                    free(json);
                }
                kv_free_keys(keys, count);
            }

        } else if (req.method == HTTP_POST || req.method == HTTP_PUT) {
            // Parse JSON body or query param
            char key[MAX_KEY_LEN] = {0};
            char val[MAX_VAL_LEN] = {0};
            int ttl = 0;

            if (req.body && strlen(req.body) > 0) {
                // simple JSON field extraction
                char *k_ptr = strstr(req.body, "\"key\"");
                char *v_ptr = strstr(req.body, "\"value\"");
                char *t_ptr = strstr(req.body, "\"ttl\"");

                if (k_ptr) sscanf(k_ptr, "\"key\"%*[: ]\"%255[^\"]\"", key);
                if (v_ptr) sscanf(v_ptr, "\"value\"%*[: ]\"%4095[^\"]\"", val);
                if (t_ptr) sscanf(t_ptr, "\"ttl\"%*[: ]%d", &ttl);
            }

            if (strlen(key) == 0) {
                get_query_param(req.query, "key", key, sizeof(key));
                get_query_param(req.query, "value", val, sizeof(val));
            }

            if (strlen(key) == 0) {
                http_send_error(fd, 400, "Missing 'key' parameter", req.keep_alive);
            } else {
                kv_set(store, key, val, ttl);
                pthread_mutex_lock(&g_stats.lock);
                g_stats.set_ops++;
                pthread_mutex_unlock(&g_stats.lock);

                char json[512];
                snprintf(json, sizeof(json), "{\"success\": true, \"key\": \"%s\", \"value\": \"%s\", \"ttl\": %d}", key, val, ttl);
                http_send_json(fd, 200, json, req.keep_alive);
            }

        } else if (req.method == HTTP_DELETE) {
            char key[MAX_KEY_LEN] = {0};
            get_query_param(req.query, "key", key, sizeof(key));

            if (strlen(key) == 0 && req.body) {
                char *k_ptr = strstr(req.body, "\"key\"");
                if (k_ptr) sscanf(k_ptr, "\"key\"%*[: ]\"%255[^\"]\"", key);
            }

            if (strlen(key) == 0) {
                http_send_error(fd, 400, "Missing 'key' parameter", req.keep_alive);
            } else {
                bool deleted = kv_del(store, key);
                if (deleted) {
                    pthread_mutex_lock(&g_stats.lock);
                    g_stats.del_ops++;
                    pthread_mutex_unlock(&g_stats.lock);

                    char json[256];
                    snprintf(json, sizeof(json), "{\"success\": true, \"deleted\": \"%s\"}", key);
                    http_send_json(fd, 200, json, req.keep_alive);
                } else {
                    http_send_error(fd, 404, "Key Not Found", req.keep_alive);
                }
            }
        } else {
            http_send_error(fd, 451, "Method Not Allowed", req.keep_alive);
        }
    } else {
        // Serve static web UI files
        handle_static_file(fd, web_root, req.path, req.keep_alive);
    }

    if (req.body) free(req.body);
    close(fd);
    free(ctx);
}
