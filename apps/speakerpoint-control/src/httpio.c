#include "httpio.h"

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

int http_parse(const char *raw, http_request_t *req)
{
    char target[768];

    memset(req, 0, sizeof(*req));
    if (sscanf(raw, "%7s %767s", req->method, target) != 2) {
        return -1;
    }

    char *q = strchr(target, '?');
    if (q) {
        *q = '\0';
        snprintf(req->query, sizeof(req->query), "%s", q + 1);
    }
    snprintf(req->path, sizeof(req->path), "%s", target);
    return 0;
}

static int hexval(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void url_decode(const char *in, size_t in_len, char *out, size_t out_len)
{
    size_t o = 0;
    for (size_t i = 0; i < in_len && o + 1 < out_len; i++) {
        int c = in[i];
        if (c == '+') {
            out[o++] = ' ';
        } else if (c == '%' && i + 2 < in_len) {
            int hi = hexval(in[i + 1]);
            int lo = hexval(in[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out[o++] = (char)((hi << 4) | lo);
                i += 2;
            } else {
                out[o++] = (char)c;
            }
        } else {
            out[o++] = (char)c;
        }
    }
    out[o] = '\0';
}

int http_query(const char *query, const char *key, char *out, size_t out_len)
{
    size_t key_len = strlen(key);
    const char *p = query;

    while (p && *p) {
        if (strncmp(p, key, key_len) == 0 && p[key_len] == '=') {
            const char *val = p + key_len + 1;
            const char *end = strchr(val, '&');
            size_t len = end ? (size_t)(end - val) : strlen(val);
            url_decode(val, len, out, out_len);
            return 0;
        }
        p = strchr(p, '&');
        if (p) p++;
    }
    return -1;
}

static void send_all(int fd, const void *buf, size_t len)
{
    const char *p = buf;
    while (len > 0) {
        ssize_t n = send(fd, p, len, 0);
        if (n <= 0) return;
        p += n;
        len -= (size_t)n;
    }
}

static void send_headers(int fd, int code, const char *status,
                         const char *content_type, size_t body_len)
{
    char header[384];
    int n = snprintf(header, sizeof(header),
                     "HTTP/1.1 %d %s\r\n"
                     "Content-Type: %s\r\n"
                     "Access-Control-Allow-Origin: *\r\n"
                     "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                     "Access-Control-Allow-Headers: Content-Type\r\n"
                     "Cache-Control: no-store\r\n"
                     "Connection: close\r\n"
                     "Content-Length: %zu\r\n\r\n",
                     code, status, content_type, body_len);
    if (n > 0) send_all(fd, header, (size_t)n);
}

void http_send_json(int fd, int code, const char *status, const char *json)
{
    size_t len = json ? strlen(json) : 0;
    send_headers(fd, code, status, "application/json", len);
    if (len) send_all(fd, json, len);
}

void http_send_status(int fd, int code, const char *status, const char *json)
{
    http_send_json(fd, code, status, json);
}

void http_send_binary(int fd, const char *content_type, const void *data, size_t len)
{
    send_headers(fd, 200, "OK", content_type, len);
    if (len) send_all(fd, data, len);
}

void http_send_empty(int fd, int code, const char *status)
{
    send_headers(fd, code, status, "text/plain", 0);
}
