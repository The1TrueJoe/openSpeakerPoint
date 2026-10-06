#include "wsbridge.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define BROKER_PORT 1883
#define MAX_SESSIONS 4
/* Largest websocket frame accepted from a browser. The dashboard's packets
 * are commands of a few bytes; this is generous. */
#define IN_CAP 16384

typedef struct {
    int ws, br;                 /* browser websocket, broker TCP; -1 = free */
    size_t inlen;
    unsigned char in[IN_CAP];
} session_t;

static session_t s_sess[MAX_SESSIONS];
static int s_init;

static void init(void)
{
    if (s_init) return;
    for (int i = 0; i < MAX_SESSIONS; i++) s_sess[i].ws = s_sess[i].br = -1;
    s_init = 1;
}

/* --- SHA-1 + base64, for Sec-WebSocket-Accept only ------------------------ */

static uint32_t rol(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

static void sha1(const unsigned char *msg, size_t len, unsigned char out[20])
{
    uint32_t h[5] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 };
    size_t total = ((len + 8) / 64 + 1) * 64;
    unsigned char buf[192];          /* key (<= 64) + GUID (36) fits in 2 blocks */
    if (total > sizeof(buf)) return;
    memset(buf, 0, total);
    memcpy(buf, msg, len);
    buf[len] = 0x80;
    uint64_t bits = (uint64_t)len * 8;
    for (int i = 0; i < 8; i++) buf[total - 1 - i] = (unsigned char)(bits >> (8 * i));

    for (size_t off = 0; off < total; off += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; i++)
            w[i] = (uint32_t)buf[off + 4 * i] << 24 | (uint32_t)buf[off + 4 * i + 1] << 16 |
                   (uint32_t)buf[off + 4 * i + 2] << 8 | buf[off + 4 * i + 3];
        for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i < 20)      { f = (b & c) | (~b & d);          k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d;                   k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
            else             { f = b ^ c ^ d;                   k = 0xCA62C1D6; }
            uint32_t t = rol(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rol(b, 30); b = a; a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }
    for (int i = 0; i < 5; i++) {
        out[4 * i] = (unsigned char)(h[i] >> 24);
        out[4 * i + 1] = (unsigned char)(h[i] >> 16);
        out[4 * i + 2] = (unsigned char)(h[i] >> 8);
        out[4 * i + 3] = (unsigned char)h[i];
    }
}

static void base64(const unsigned char *in, size_t len, char *out)
{
    static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t o = 0;
    for (size_t i = 0; i < len; i += 3) {
        uint32_t v = (uint32_t)in[i] << 16;
        if (i + 1 < len) v |= (uint32_t)in[i + 1] << 8;
        if (i + 2 < len) v |= in[i + 2];
        out[o++] = tbl[(v >> 18) & 63];
        out[o++] = tbl[(v >> 12) & 63];
        out[o++] = i + 1 < len ? tbl[(v >> 6) & 63] : '=';
        out[o++] = i + 2 < len ? tbl[v & 63] : '=';
    }
    out[o] = '\0';
}

/* --- handshake --------------------------------------------------------------- */

/* Value of header `name` in the raw request, trimmed; 0 if found. */
static int header(const char *raw, const char *name, char *out, size_t out_len)
{
    size_t nl = strlen(name);
    const char *p = strstr(raw, "\r\n");
    while (p && p[2] != '\r') {
        p += 2;
        if (!strncasecmp(p, name, nl) && p[nl] == ':') {
            const char *v = p + nl + 1;
            while (*v == ' ' || *v == '\t') v++;
            size_t n = strcspn(v, "\r");
            while (n && (v[n - 1] == ' ' || v[n - 1] == '\t')) n--;
            if (n >= out_len) return -1;
            memcpy(out, v, n);
            out[n] = '\0';
            return 0;
        }
        p = strstr(p, "\r\n");
    }
    return -1;
}

int ws_is_upgrade(const char *raw)
{
    char v[64];
    return !header(raw, "Upgrade", v, sizeof(v)) && strcasestr(v, "websocket");
}

static int write_all(int fd, const void *data, size_t len)
{
    const unsigned char *p = data;
    while (len) {
        ssize_t n = send(fd, p, len, MSG_NOSIGNAL);
        if (n <= 0) return -1;
        p += n;
        len -= (size_t)n;
    }
    return 0;
}

static int broker_connect(void)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons(BROKER_PORT) };
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, (struct sockaddr *)&a, sizeof(a)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

int ws_accept(int fd, const char *raw)
{
    init();
    char key[64], proto[128];
    session_t *s = NULL;
    for (int i = 0; i < MAX_SESSIONS; i++)
        if (s_sess[i].ws < 0) { s = &s_sess[i]; break; }

    if (!s || header(raw, "Sec-WebSocket-Key", key, sizeof(key))) {
        const char *r = "HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        write_all(fd, r, strlen(r));
        return -1;
    }
    int br = broker_connect();
    if (br < 0) {
        const char *r = "HTTP/1.1 502 Bad Gateway\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        write_all(fd, r, strlen(r));
        return -1;
    }

    unsigned char cat[128], digest[20];
    char accept[32];
    int n = snprintf((char *)cat, sizeof(cat), "%s258EAFA5-E914-47DA-95CA-C5AB0DC85B11", key);
    sha1(cat, (size_t)n, digest);
    base64(digest, sizeof(digest), accept);

    /* mqtt.js asks for the "mqtt" subprotocol, and a browser drops the
     * connection unless the server agrees to one it offered. */
    int want_mqtt = !header(raw, "Sec-WebSocket-Protocol", proto, sizeof(proto)) && strstr(proto, "mqtt");
    char resp[256];
    n = snprintf(resp, sizeof(resp),
                 "HTTP/1.1 101 Switching Protocols\r\n"
                 "Upgrade: websocket\r\nConnection: Upgrade\r\n"
                 "Sec-WebSocket-Accept: %s\r\n%s\r\n",
                 accept, want_mqtt ? "Sec-WebSocket-Protocol: mqtt\r\n" : "");
    if (write_all(fd, resp, (size_t)n) < 0) {
        close(br);
        return -1;
    }

    /* A stalled browser must not stall the whole (single-threaded) daemon. */
    struct timeval tv = { .tv_sec = 2 };
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    s->ws = fd;
    s->br = br;
    s->inlen = 0;
    return 0;
}

/* --- relay ------------------------------------------------------------------- */

static void end(session_t *s, int send_close)
{
    if (send_close) {
        static const unsigned char bye[2] = { 0x88, 0x00 };
        write_all(s->ws, bye, sizeof(bye));
    }
    close(s->ws);
    close(s->br);
    s->ws = s->br = -1;
}

/* One unmasked server frame. */
static int send_frame(int fd, int opcode, const unsigned char *data, size_t len)
{
    unsigned char h[4];
    size_t hl = 2;
    h[0] = (unsigned char)(0x80 | opcode);
    if (len < 126) {
        h[1] = (unsigned char)len;
    } else {             /* callers never send more than 64K at once */
        h[1] = 126;
        h[2] = (unsigned char)(len >> 8);
        h[3] = (unsigned char)len;
        hl = 4;
    }
    return write_all(fd, h, hl) || write_all(fd, data, len) ? -1 : 0;
}

/* Browser -> broker: unwrap every complete frame in the buffer. 0 = keep. */
static int from_browser(session_t *s)
{
    ssize_t n = recv(s->ws, s->in + s->inlen, IN_CAP - s->inlen, 0);
    if (n <= 0) return -1;
    s->inlen += (size_t)n;

    while (s->inlen >= 2) {
        unsigned char *f = s->in;
        int op = f[0] & 0x0f;
        uint64_t len = f[1] & 0x7f;
        size_t hl = 2;
        if (!(f[1] & 0x80)) return -1;          /* clients must mask */
        if (len == 126) {
            if (s->inlen < 4) break;
            len = (uint64_t)f[2] << 8 | f[3];
            hl = 4;
        } else if (len == 127) {
            if (s->inlen < 10) break;
            len = 0;
            for (int i = 0; i < 8; i++) len = len << 8 | f[2 + i];
            hl = 10;
        }
        hl += 4;
        if (len > IN_CAP - hl) return -1;       /* bigger than we take */
        if (s->inlen < hl + len) break;

        unsigned char *mask = f + hl - 4, *p = f + hl;
        for (uint64_t i = 0; i < len; i++) p[i] ^= mask[i & 3];

        switch (op) {
        case 0x0: case 0x1: case 0x2:           /* MQTT bytes */
            if (write_all(s->br, p, (size_t)len)) return -1;
            break;
        case 0x8:                               /* close */
            return -1;
        case 0x9:                               /* ping */
            if (send_frame(s->ws, 0xA, p, (size_t)len)) return -1;
            break;
        case 0xA:                               /* pong */
            break;
        default:
            return -1;
        }
        size_t used = hl + (size_t)len;
        memmove(s->in, s->in + used, s->inlen - used);
        s->inlen -= used;
    }
    if (s->inlen == IN_CAP) return -1;
    return 0;
}

/* Broker -> browser: whatever arrived, as one binary frame. */
static int from_broker(session_t *s)
{
    unsigned char buf[4096];
    ssize_t n = recv(s->br, buf, sizeof(buf), 0);
    if (n <= 0) return -1;
    return send_frame(s->ws, 0x2, buf, (size_t)n);
}

int ws_fdset(fd_set *rfds, int maxfd)
{
    init();
    for (int i = 0; i < MAX_SESSIONS; i++) {
        session_t *s = &s_sess[i];
        if (s->ws < 0) continue;
        FD_SET(s->ws, rfds);
        FD_SET(s->br, rfds);
        if (s->ws > maxfd) maxfd = s->ws;
        if (s->br > maxfd) maxfd = s->br;
    }
    return maxfd;
}

void ws_service(fd_set *rfds)
{
    for (int i = 0; i < MAX_SESSIONS; i++) {
        session_t *s = &s_sess[i];
        if (s->ws < 0) continue;
        if (FD_ISSET(s->br, rfds) && from_broker(s)) {
            end(s, 1);
            continue;
        }
        if (FD_ISSET(s->ws, rfds) && from_browser(s))
            end(s, 1);
    }
}

void ws_close_all(void)
{
    init();
    for (int i = 0; i < MAX_SESSIONS; i++)
        if (s_sess[i].ws >= 0) end(&s_sess[i], 1);
}

void ws_forget(void)
{
    init();
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (s_sess[i].ws < 0) continue;
        close(s_sess[i].ws);
        close(s_sess[i].br);
        s_sess[i].ws = s_sess[i].br = -1;
    }
}
