/* Minimal HTTP request parsing and response helpers. */
#ifndef SPKR_HTTPIO_H
#define SPKR_HTTPIO_H

#include <stddef.h>

typedef struct {
    char method[8];
    char path[256];
    char query[512];
} http_request_t;

/* Parse the request line of a raw HTTP request. Returns 0 on success. */
int http_parse(const char *raw, http_request_t *req);

/* Extract a query-string parameter value into out (URL-decoded).
 * Returns 0 on success, -1 if the key is absent. */
int http_query(const char *query, const char *key, char *out, size_t out_len);

/* Response helpers (all set permissive CORS + Connection: close). */
void http_send_json(int fd, int code, const char *status, const char *json);
void http_send_status(int fd, int code, const char *status, const char *json);
void http_send_binary(int fd, const char *content_type, const void *data, size_t len);
void http_send_empty(int fd, int code, const char *status);

#endif
