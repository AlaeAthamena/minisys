#ifndef HTTP_H
#define HTTP_H

#include "minisys.h"
#include "kvstore.h"
#include "net.h"

#define MAX_HEADERS 32
#define MAX_HEADER_LEN 1024

typedef enum {
    HTTP_GET,
    HTTP_POST,
    HTTP_PUT,
    HTTP_DELETE,
    HTTP_OPTIONS,
    HTTP_UNKNOWN
} http_method_t;

typedef struct {
    char key[128];
    char value[MAX_HEADER_LEN];
} http_header_t;

typedef struct {
    http_method_t method;
    char method_str[16];
    char path[MAX_PATH_LEN];
    char query[MAX_PATH_LEN];
    int version_major;
    int version_minor;
    http_header_t headers[MAX_HEADERS];
    int header_count;
    char *body;
    size_t body_len;
    bool keep_alive;
} http_request_t;

typedef struct {
    int client_fd;
    kv_store_t *kv_store;
    const char *web_root;
} http_conn_ctx_t;

// API functions
void http_handle_connection(void *arg);
bool http_parse_request(const char *raw_req, size_t len, http_request_t *req);
void http_send_response(int client_fd, int status_code, const char *status_text,
                        const char *content_type, const char *body, size_t body_len,
                        bool keep_alive);
void http_send_json(int client_fd, int status_code, const char *json_body, bool keep_alive);
void http_send_error(int client_fd, int status_code, const char *message, bool keep_alive);
const char *http_get_header(const http_request_t *req, const char *name);

#endif // HTTP_H
