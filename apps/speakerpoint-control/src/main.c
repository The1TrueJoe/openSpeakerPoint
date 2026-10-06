/*
 * speakerpoint-control - control daemon for the SpeakerPoint.
 *
 * Controls and live state are on MQTT, on the box's own mosquitto (mqtt.h);
 * HTTP keeps only what MQTT carries badly, for the React dashboard:
 *
 *   GET  /api/health
 *   GET  /api/audio/status                  hardware readback (diagnostic text)
 *   GET  /api/library                       USB media library (re-fetched when
 *                                           MQTT state/library's rev changes)
 *   POST /api/upload?name=<f>               raw-body MP3 upload to USB
 *   GET  /api/albumart?file=<uri>           image bytes
 *   GET  /mqtt (websocket)                  MQTT over websockets for the
 *                                           dashboard, relayed to the broker
 *                                           (wsbridge.h)
 */
#include "audio.h"
#include "airplay.h"
#include "httpio.h"
#include "mqtt.h"
#include "player.h"
#include "wsbridge.h"

#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#define LISTEN_PORT 8081
#define REQ_BUF_SIZE 4096
#define LIBRARY_BUF_SIZE (192 * 1024)

static volatile sig_atomic_t keep_running = 1;

static void on_signal(int sig)
{
    (void)sig;
    keep_running = 0;
}

static int is(const http_request_t *r, const char *method, const char *path)
{
    return !strcmp(r->method, method) && !strcmp(r->path, path);
}

#define UPLOAD_DIR "/media/usb"
#define MAX_UPLOAD (128UL * 1024 * 1024)

/* Reduce a client-supplied name to a safe basename in out; 0 on success. */
static int safe_name(const char *in, char *out, size_t out_len)
{
    const char *slash = strrchr(in, '/');
    const char *base = slash ? slash + 1 : in;
    if (base[0] == '\0' || base[0] == '.' || strstr(base, "..")) return -1;
    size_t n = 0;
    for (const char *p = base; *p && n + 1 < out_len; p++) {
        unsigned char c = (unsigned char)*p;
        if (c < 0x20 || c == '/' || c == '\\') return -1;
        out[n++] = (char)c;
    }
    out[n] = '\0';
    return n ? 0 : -1;
}

static long parse_content_length(const char *raw)
{
    const char *h = strcasestr(raw, "\r\ncontent-length:");
    if (!h) return -1;
    h += strlen("\r\ncontent-length:");
    while (*h == ' ') h++;
    return atol(h);
}

/* Read from fd into buf (capacity cap-1) until "\r\n\r\n" is seen (the full
 * header block, which a single recv() is not guaranteed to contain - it can
 * legitimately split across TCP segments, especially when a large body
 * follows). Returns total bytes read (headers plus any body bytes that
 * happened to arrive alongside them), or -1 if the header block never
 * completes within cap or the peer closes first. */
static ssize_t recv_headers(int fd, char *buf, size_t cap)
{
    size_t total = 0;
    while (total < cap - 1) {
        ssize_t n = recv(fd, buf + total, cap - 1 - total, 0);
        if (n <= 0) return -1;
        total += (size_t)n;
        buf[total] = '\0';
        if (memmem(buf, total, "\r\n\r\n", 4)) return (ssize_t)total;
    }
    return -1;
}

/* Stream a raw-body file upload (POST /api/upload?name=foo.mp3) onto the USB
 * drive, briefly remounting it read-write so normal playback stays read-only
 * and yank-safe. */
static void handle_upload(int fd, const http_request_t *r, char *raw, ssize_t nread)
{
    char rawname[256], name[128], path[300], tmp_path[300];
    if (http_query(r->query, "name", rawname, sizeof(rawname)) ||
        safe_name(rawname, name, sizeof(name))) {
        http_send_json(fd, 400, "Bad Request", "{\"ok\":false,\"error\":\"bad name\"}");
        return;
    }

    long clen = parse_content_length(raw);
    char *body = strstr(raw, "\r\n\r\n");
    if (clen < 0 || (unsigned long)clen > MAX_UPLOAD || !body) {
        http_send_json(fd, 400, "Bad Request", "{\"ok\":false,\"error\":\"bad body\"}");
        return;
    }
    body += 4;
    size_t have = (size_t)(nread - (body - raw));

    (void)system("mount -o remount,rw " UPLOAD_DIR " 2>/dev/null");

    /* Write to a temp file and rename into place only on success, so a
     * failed or interrupted transfer can never leave a truncated track
     * sitting in the library. */
    snprintf(path, sizeof(path), "%s/%s", UPLOAD_DIR, name);
    snprintf(tmp_path, sizeof(tmp_path), "%s/.upload.part", UPLOAD_DIR);

    FILE *f = fopen(tmp_path, "wb");
    if (!f) {
        (void)system("mount -o remount,ro " UPLOAD_DIR " 2>/dev/null");
        http_send_json(fd, 500, "Error", "{\"ok\":false,\"error\":\"no writable drive\"}");
        return;
    }

    int failed = 0;
    size_t written = 0, since_sync = 0;

    /* 32KB writes, flushed every 256KB. The EP93xx OHCI is full-speed only,
     * and letting the page cache accumulate a large dirty window makes the
     * block layer issue oversized requests that the link can't complete in
     * time (SCSI reports "Unaligned partial completion" and the write
     * fails). Pacing the writes keeps each request small. */
    if (have) {
        size_t w = have > (size_t)clen ? (size_t)clen : have;
        if (fwrite(body, 1, w, f) != w) failed = 1;
        written = w;
        since_sync = w;
    }

    char buf[32768];
    while (!failed && written < (size_t)clen) {
        size_t want = (size_t)clen - written;
        if (want > sizeof(buf)) want = sizeof(buf);
        ssize_t n = recv(fd, buf, want, 0);
        if (n <= 0) break;
        if (fwrite(buf, 1, (size_t)n, f) != (size_t)n) { failed = 1; break; }
        written += (size_t)n;
        since_sync += (size_t)n;
        if (since_sync >= 256 * 1024) {
            if (fflush(f) != 0) { failed = 1; break; }
            fsync(fileno(f));
            since_sync = 0;
        }
    }

    if (fflush(f) != 0) failed = 1;
    if (fsync(fileno(f)) != 0) failed = 1;
    if (fclose(f) != 0) failed = 1;

    if (!failed && written == (size_t)clen && rename(tmp_path, path) == 0) {
        (void)system("sync; mount -o remount,ro " UPLOAD_DIR " 2>/dev/null");
        /* The drive never went anywhere - just invalidate the cached
         * listing, don't re-enumerate USB (see player_invalidate_library). */
        player_invalidate_library();
        http_send_json(fd, 200, "OK", "{\"ok\":true}");
    } else {
        unlink(tmp_path);
        (void)system("sync; mount -o remount,ro " UPLOAD_DIR " 2>/dev/null");
        http_send_json(fd, 500, "Error",
                       "{\"ok\":false,\"error\":\"write failed - drive may be full or faulty\"}");
    }
}

static void dispatch(int fd, const http_request_t *r)
{
    char val[512];

    if (!strcmp(r->method, "OPTIONS")) {
        http_send_empty(fd, 204, "No Content");
        return;
    }

    if (is(r, "GET", "/api/health")) {
        http_send_json(fd, 200, "OK", "{\"ok\":true,\"service\":\"speakerpoint-control\"}");
        return;
    }

    /* Actual hardware readback (mixer + AC97 + D2 amp registers), so a
     * mismatch between what we asked for and what the chips have is
     * directly visible instead of having to be inferred. */
    if (is(r, "GET", "/api/audio/status")) {
        char body[2048];
        audio_status_text(body, sizeof(body));
        http_send_binary(fd, "text/plain", body, strlen(body));
        return;
    }

    if (is(r, "GET", "/api/library")) {
        char *body = malloc(LIBRARY_BUF_SIZE);
        if (!body) {
            http_send_json(fd, 500, "Error", "{\"ok\":false}");
            return;
        }
        player_library_json(body, LIBRARY_BUF_SIZE);
        http_send_json(fd, 200, "OK", body);
        free(body);
        return;
    }

    if (is(r, "GET", "/api/albumart")) {
        if (http_query(r->query, "file", val, sizeof(val))) {
            http_send_empty(fd, 400, "Bad Request");
            return;
        }
        size_t len = 0;
        const char *mime = "image/jpeg";
        void *art = player_albumart(val, &len, &mime);
        if (!art) {
            http_send_empty(fd, 404, "Not Found");
            return;
        }
        http_send_binary(fd, mime, art, len);
        free(art);
        return;
    }

    http_send_json(fd, 404, "Not Found", "{\"ok\":false,\"error\":\"not found\"}");
}

int main(void)
{
    int server_fd;
    struct sockaddr_in addr;

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);

    audio_load();
    audio_apply_startup();
    player_start();
    airplay_start();
    mqtt_start();

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }

    {
        int opt = 1;
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(LISTEN_PORT);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(server_fd);
        return 1;
    }
    if (listen(server_fd, 8) < 0) {
        perror("listen");
        close(server_fd);
        return 1;
    }

    while (keep_running) {
        fd_set rfds, wfds;
        FD_ZERO(&rfds);
        FD_ZERO(&wfds);
        FD_SET(server_fd, &rfds);
        int maxfd = server_fd;
        maxfd = mqtt_fdset(&rfds, &wfds, maxfd);
        maxfd = ws_fdset(&rfds, maxfd);

        /* Optional media-engine fd, if it has one to watch. */
        int pfd = player_status_fd();
        if (pfd >= 0) {
            FD_SET(pfd, &rfds);
            if (pfd > maxfd) maxfd = pfd;
        }

        /* 1s timeout so player_tick() runs even when nothing else happens. */
        struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
        int n = select(maxfd + 1, &rfds, &wfds, NULL, &tv);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        player_tick();
        audio_tick();
        mqtt_tick();
        if (n == 0) continue;   /* timeout only */

        mqtt_service(&rfds, &wfds);
        ws_service(&rfds);

        if (pfd >= 0 && FD_ISSET(pfd, &rfds)) {
            player_poll();
        }

        if (FD_ISSET(server_fd, &rfds)) {
            int client_fd = accept(server_fd, NULL, NULL);
            if (client_fd < 0) {
                if (errno == EINTR) continue;
                break;
            }

            char req_buf[REQ_BUF_SIZE + 1];
            ssize_t nread = recv_headers(client_fd, req_buf, sizeof(req_buf));
            if (nread > 0) {
                http_request_t req;
                if (http_parse(req_buf, &req) == 0) {
                    if (is(&req, "GET", WS_PATH) && ws_is_upgrade(req_buf)) {
                        if (ws_accept(client_fd, req_buf) == 0) continue;   /* now a session */
                    } else if (is(&req, "POST", "/api/upload")) {
                        /* A big file over a slow USB link can take a long
                         * time. This daemon is single-threaded/single-
                         * connection, so handling it inline would block the
                         * whole event loop - including /api/now polling and
                         * every other control endpoint - for the entire
                         * transfer, which looks exactly like the daemon
                         * crashing from the dashboard's side. Fork so the
                         * transfer runs in the background instead.
                         *
                         * This is scoped to /api/upload specifically, not
                         * every connection: every other endpoint mutates
                         * in-process state (playback position, output
                         * routing, media state, ...) that must persist
                         * across requests, and a forked child's changes to
                         * that state vanish when it exits - handling those
                         * inline in this process is required for
                         * correctness. The one piece of state the upload
                         * path touches, the library scan-cache flag, only
                         * governs a few seconds of cache staleness even if
                         * the child's reset of it is lost, so it's safe to
                         * let go. SIGCHLD is ignored (see player_start()),
                         * so the child is reaped automatically. */
                        pid_t pid = fork();
                        if (pid == 0) {
                            close(server_fd);
                            ws_forget();
                            handle_upload(client_fd, &req, req_buf, nread);
                            close(client_fd);
                            _exit(0);
                        }
                        if (pid > 0) {
                            close(client_fd);
                            continue;
                        }
                        /* fork failed (rare): fall back to handling inline. */
                        handle_upload(client_fd, &req, req_buf, nread);
                    } else {
                        dispatch(client_fd, &req);
                        mqtt_sync();   /* a REST change shows on MQTT at once */
                    }
                } else {
                    http_send_json(client_fd, 400, "Bad Request", "{\"ok\":false}");
                }
            }
            close(client_fd);
        }
    }

    ws_close_all();
    mqtt_stop();
    close(server_fd);
    return 0;
}
