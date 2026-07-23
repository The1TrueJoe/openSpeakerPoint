/*
 * speakerpoint-control - HTTP control daemon for the SpeakerPoint.
 *
 * Exposes audio routing/volume/test-tones and (via an mpg123 -R child) USB
 * MP3 playback with ID3 metadata and album art, over a small JSON API consumed
 * by the React dashboard.
 *
 *   GET  /api/health
 *   GET  /api/state                         { volume, output }
 *   POST /api/output?value=off|rca|amp|both
 *   POST /api/volume?value=0-100
 *   POST /api/tone?channel=&type=&freq=
 *   POST /api/tone/stop
 *   GET  /api/now                           now-playing
 *   POST /api/transport?cmd=play|pause|stop|next|prev
 *   POST /api/seek?value=<seconds>
 *   GET  /api/library                       USB media library
 *   POST /api/play?index=<n>
 *   POST /api/rescan
 *   GET  /api/albumart?file=<uri>           image bytes
 */
#include "audio.h"
#include "httpio.h"
#include "player.h"

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

    if (is(r, "GET", "/api/state")) {
        char body[128];
        audio_state_json(body, sizeof(body));
        http_send_json(fd, 200, "OK", body);
        return;
    }

    if (is(r, "POST", "/api/output")) {
        if (http_query(r->query, "value", val, sizeof(val)) || !audio_valid_output(val)) {
            http_send_json(fd, 400, "Bad Request", "{\"ok\":false,\"error\":\"bad output\"}");
            return;
        }
        audio_set_output(val);
        char body[128];
        audio_state_json(body, sizeof(body));
        http_send_json(fd, 200, "OK", body);
        return;
    }

    if (is(r, "POST", "/api/volume")) {
        if (http_query(r->query, "value", val, sizeof(val))) {
            http_send_json(fd, 400, "Bad Request", "{\"ok\":false,\"error\":\"missing value\"}");
            return;
        }
        audio_set_volume(atoi(val));
        char body[128];
        audio_state_json(body, sizeof(body));
        http_send_json(fd, 200, "OK", body);
        return;
    }

    if (is(r, "POST", "/api/tone")) {
        char ch[16] = "both";
        http_query(r->query, "channel", ch, sizeof(ch));
        audio_tone(ch);
        http_send_json(fd, 200, "OK", "{\"ok\":true}");
        return;
    }

    if (is(r, "POST", "/api/tone/stop")) {
        audio_tone_stop();
        http_send_json(fd, 200, "OK", "{\"ok\":true}");
        return;
    }

    if (is(r, "GET", "/api/now")) {
        char body[2048];
        player_now_json(body, sizeof(body));
        http_send_json(fd, 200, "OK", body);
        return;
    }

    if (is(r, "POST", "/api/transport")) {
        if (http_query(r->query, "cmd", val, sizeof(val)) || player_transport(val)) {
            http_send_json(fd, 400, "Bad Request", "{\"ok\":false,\"error\":\"transport failed\"}");
            return;
        }
        http_send_json(fd, 200, "OK", "{\"ok\":true}");
        return;
    }

    if (is(r, "POST", "/api/seek")) {
        if (http_query(r->query, "value", val, sizeof(val))) {
            http_send_json(fd, 400, "Bad Request", "{\"ok\":false,\"error\":\"missing value\"}");
            return;
        }
        player_seek(atoi(val));
        http_send_json(fd, 200, "OK", "{\"ok\":true}");
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

    if (is(r, "POST", "/api/play")) {
        if (http_query(r->query, "index", val, sizeof(val)) || player_play_index(atoi(val))) {
            http_send_json(fd, 400, "Bad Request", "{\"ok\":false,\"error\":\"play failed\"}");
            return;
        }
        http_send_json(fd, 200, "OK", "{\"ok\":true}");
        return;
    }

    if (is(r, "POST", "/api/rescan")) {
        player_rescan();
        http_send_json(fd, 200, "OK", "{\"ok\":true}");
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
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(server_fd, &rfds);
        int maxfd = server_fd;

        /* Also watch mpg123's status pipe so playback position/state stay
         * current between HTTP requests. */
        int pfd = player_status_fd();
        if (pfd >= 0) {
            FD_SET(pfd, &rfds);
            if (pfd > maxfd) maxfd = pfd;
        }

        if (select(maxfd + 1, &rfds, NULL, NULL, NULL) < 0) {
            if (errno == EINTR) continue;
            break;
        }

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
            ssize_t nread = recv(client_fd, req_buf, REQ_BUF_SIZE, 0);
            if (nread > 0) {
                req_buf[nread] = '\0';
                http_request_t req;
                if (http_parse(req_buf, &req) == 0) {
                    dispatch(client_fd, &req);
                } else {
                    http_send_json(client_fd, 400, "Bad Request", "{\"ok\":false}");
                }
            }
            close(client_fd);
        }
    }

    close(server_fd);
    return 0;
}
