/* Media engine: a thin client for MPD (127.0.0.1:6600).
 *
 * MPD owns playback, the queue, and library indexing. This used to drive
 * mpg123 in remote-control mode by hand, which never gave reliable
 * pause/seek/queue and failed silently when the child wasn't running.
 *
 * Album art is still extracted here by parsing ID3 directly off the file
 * rather than going through MPD's binary art protocol - the files are local
 * and this code already works, so it avoids a chunk of protocol handling.
 */
#include "player.h"
#include "airplay.h"
#include "jsonutil.h"

#include <arpa/inet.h>
#include <dirent.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define MUSIC_DIR "/media/usb"
#define MPD_HOST "127.0.0.1"
#define MPD_PORT 6600
#define TAG_LEN 256
#define PATH_LEN 512
#define MAX_TRACKS 500

static time_t s_rescan_at = 0;

/* ------------------------------------------------------------------ */
/* MPD connection                                                     */
/* ------------------------------------------------------------------ */
typedef struct {
    int fd;
    char buf[4096];
    size_t len, pos;
} conn_t;

static int conn_fill(conn_t *c)
{
    if (c->pos < c->len) return 0;
    ssize_t n = recv(c->fd, c->buf, sizeof(c->buf), 0);
    if (n <= 0) return -1;
    c->len = (size_t)n;
    c->pos = 0;
    return 0;
}

/* Read one '\n'-terminated line (newline stripped). 0 on success. */
static int conn_line(conn_t *c, char *out, size_t cap)
{
    size_t o = 0;
    for (;;) {
        if (c->pos >= c->len && conn_fill(c) < 0) return -1;
        char ch = c->buf[c->pos++];
        if (ch == '\n') break;
        if (o + 1 < cap) out[o++] = ch;
    }
    out[o] = '\0';
    return 0;
}

static void conn_close(conn_t *c)
{
    if (c->fd >= 0) close(c->fd);
    c->fd = -1;
}

static int conn_open(conn_t *c)
{
    struct sockaddr_in addr;
    struct timeval tv = { .tv_sec = 2, .tv_usec = 0 };
    char greeting[128];

    c->fd = socket(AF_INET, SOCK_STREAM, 0);
    if (c->fd < 0) return -1;
    setsockopt(c->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(c->fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(MPD_PORT);
    addr.sin_addr.s_addr = inet_addr(MPD_HOST);

    if (connect(c->fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        conn_close(c);
        return -1;
    }
    c->len = c->pos = 0;
    if (conn_line(c, greeting, sizeof(greeting)) != 0) { /* "OK MPD <ver>" */
        conn_close(c);
        return -1;
    }
    return 0;
}

static int conn_send(conn_t *c, const char *s)
{
    size_t len = strlen(s);
    return send(c->fd, s, len, 0) == (ssize_t)len ? 0 : -1;
}

/* Quote an argument per the MPD protocol. */
static void mpd_quote(const char *in, char *out, size_t cap)
{
    size_t o = 0;
    if (o + 1 < cap) out[o++] = '"';
    for (; *in && o + 2 < cap; in++) {
        if (*in == '"' || *in == '\\') out[o++] = '\\';
        out[o++] = *in;
    }
    if (o + 1 < cap) out[o++] = '"';
    out[o] = '\0';
}

/* Fire a command, discard the payload, return 0 if MPD said OK. */
static int mpd_cmd(const char *cmd)
{
    conn_t c;
    char line[512];
    int rc = -1;

    if (conn_open(&c) < 0) return -1;
    if (conn_send(&c, cmd) == 0) {
        while (conn_line(&c, line, sizeof(line)) == 0) {
            if (!strcmp(line, "OK")) { rc = 0; break; }
            if (!strncmp(line, "ACK", 3)) break;
        }
    }
    conn_close(&c);
    return rc;
}

/* ------------------------------------------------------------------ */
/* ID3v2 album art (files are local; avoids MPD's binary art protocol)  */
/* ------------------------------------------------------------------ */
static unsigned syncsafe(const unsigned char *p)
{
    return ((unsigned)(p[0] & 0x7f) << 21) | ((unsigned)(p[1] & 0x7f) << 14) |
           ((unsigned)(p[2] & 0x7f) << 7) | (unsigned)(p[3] & 0x7f);
}
static unsigned be32(const unsigned char *p)
{
    return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) |
           ((unsigned)p[2] << 8) | (unsigned)p[3];
}

typedef int (*frame_cb)(const char *id, const unsigned char *data, unsigned len, void *ctx);

static int id3_walk(const char *path, frame_cb cb, void *ctx)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    unsigned char hdr[10];
    if (fread(hdr, 1, 10, f) != 10 || memcmp(hdr, "ID3", 3) != 0) { fclose(f); return -1; }
    int ver = hdr[3];
    unsigned tag_size = syncsafe(hdr + 6);
    if (tag_size == 0 || tag_size > 8u * 1024 * 1024) { fclose(f); return -1; }

    unsigned char *buf = malloc(tag_size);
    if (!buf) { fclose(f); return -1; }
    if (fread(buf, 1, tag_size, f) != tag_size) { free(buf); fclose(f); return -1; }
    fclose(f);

    unsigned pos = 0;
    int fidlen = (ver == 2) ? 3 : 4;
    int fhdrlen = (ver == 2) ? 6 : 10;
    int stop = 0;

    while (!stop && pos + (unsigned)fhdrlen <= tag_size) {
        char id[5] = {0};
        memcpy(id, buf + pos, fidlen);
        if (id[0] == 0) break;

        unsigned fsize;
        if (ver == 2) fsize = ((unsigned)buf[pos + 3] << 16) |
                              ((unsigned)buf[pos + 4] << 8) | buf[pos + 5];
        else if (ver == 4) fsize = syncsafe(buf + pos + 4);
        else fsize = be32(buf + pos + 4);

        pos += fhdrlen;
        if (fsize == 0 || pos + fsize > tag_size) break;

        stop = cb(id, buf + pos, fsize, ctx);
        pos += fsize;
    }
    free(buf);
    return 0;
}

typedef struct { void *buf; size_t len; char mime[64]; } art_ctx;

static int art_cb(const char *id, const unsigned char *data, unsigned len, void *ctx)
{
    art_ctx *a = ctx;
    if (strcmp(id, "APIC") && strcmp(id, "PIC")) return 0;
    if (len < 4) return 0;

    unsigned p = 1;                       /* text encoding */
    if (!strcmp(id, "PIC")) {
        p += 3;                           /* v2.2: 3-char image format */
    } else {
        while (p < len && data[p]) p++;   /* MIME string */
        if (p < len) {
            size_t mlen = p - 1;
            if (mlen >= sizeof(a->mime)) mlen = sizeof(a->mime) - 1;
            memcpy(a->mime, data + 1, mlen);
            a->mime[mlen] = '\0';
        }
        p++;
    }
    if (p >= len) return 0;
    p += 1;                               /* picture type */
    while (p < len && data[p]) p++;       /* description */
    p++;
    if (p >= len) return 0;

    size_t plen = len - p;
    a->buf = malloc(plen);
    if (!a->buf) return 1;
    memcpy(a->buf, data + p, plen);
    a->len = plen;
    return 1;
}

void *player_albumart(const char *uri, size_t *len, const char **mime)
{
    static char mime_buf[64];
    char path[PATH_LEN];
    art_ctx ctx;

    memset(&ctx, 0, sizeof(ctx));
    snprintf(ctx.mime, sizeof(ctx.mime), "image/jpeg");

    /* MPD URIs are relative to music_directory; also accept an absolute
     * path, but never outside the music dir. */
    if (!strncmp(uri, MUSIC_DIR, strlen(MUSIC_DIR))) {
        snprintf(path, sizeof(path), "%s", uri);
    } else {
        if (strstr(uri, "..")) return NULL;
        snprintf(path, sizeof(path), "%s/%s", MUSIC_DIR, uri);
    }

    id3_walk(path, art_cb, &ctx);
    if (!ctx.buf) return NULL;

    *len = ctx.len;
    snprintf(mime_buf, sizeof(mime_buf), "%s", ctx.mime[0] ? ctx.mime : "image/jpeg");
    *mime = mime_buf;
    return ctx.buf;
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                          */
/* ------------------------------------------------------------------ */
static void run_usb_detect(void)
{
    (void)system("/usr/bin/usb-detect >/dev/null 2>&1 &");
    s_rescan_at = time(NULL);
}

void player_start(void)
{
    run_usb_detect();
}

/* No child process to watch any more - MPD is queried on demand, so state is
 * always read live rather than tracked incrementally. */
int player_status_fd(void) { return -1; }
void player_poll(void) { }

void player_tick(void)
{
    airplay_poll();
    /* AirPlay needs the exclusive codec - hand it over. */
    if (airplay_active()) {
        mpd_cmd("pause 1\n");
    }
}

/* ------------------------------------------------------------------ */
/* Transport                                                          */
/* ------------------------------------------------------------------ */
int player_transport(const char *cmd)
{
    if (!cmd) return -1;
    if (!strcmp(cmd, "play"))  return mpd_cmd("play\n");
    if (!strcmp(cmd, "pause")) return mpd_cmd("pause 1\n");
    if (!strcmp(cmd, "stop"))  return mpd_cmd("stop\n");
    if (!strcmp(cmd, "next"))  return mpd_cmd("next\n");
    if (!strcmp(cmd, "prev"))  return mpd_cmd("previous\n");
    return -1;
}

int player_release(void)
{
    /* Stop (not pause) so MPD closes the ALSA device: the codec is exclusive
     * (no software mixer), so another source can't open it otherwise. */
    return mpd_cmd("stop\n");
}

int player_seek(int seconds)
{
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "seekcur %d\n", seconds < 0 ? 0 : seconds);
    return mpd_cmd(cmd);
}

int player_play_index(int index)
{
    char cmd[128];
    if (index < 0) index = 0;
    /* Queue the whole library once so next/prev work, then jump to the
     * chosen track. */
    snprintf(cmd, sizeof(cmd),
             "command_list_begin\nclear\nadd \"\"\nplay %d\ncommand_list_end\n", index);
    return mpd_cmd(cmd);
}

int player_rescan(void)
{
    run_usb_detect();
    mpd_cmd("update\n");
    return 0;
}

void player_invalidate_library(void)
{
    /* Database refresh only - must NOT re-enumerate USB (that unbinds the
     * controller and corrupts an in-flight write; see usb-detect). */
    mpd_cmd("update\n");
    s_rescan_at = time(NULL);
}

int player_delete_track(const char *path)
{
    char full[PATH_LEN], cmd[PATH_LEN + 96];

    if (!strncmp(path, MUSIC_DIR, strlen(MUSIC_DIR))) {
        snprintf(full, sizeof(full), "%s", path);
    } else {
        if (strstr(path, "..")) return -1;
        snprintf(full, sizeof(full), "%s/%s", MUSIC_DIR, path);
    }
    if (strstr(full, "..")) return -1;

    snprintf(cmd, sizeof(cmd), "mount -o remount,rw %s 2>/dev/null", MUSIC_DIR);
    (void)system(cmd);
    int rc = unlink(full);
    snprintf(cmd, sizeof(cmd), "sync; mount -o remount,ro %s 2>/dev/null", MUSIC_DIR);
    (void)system(cmd);

    if (rc == 0) player_invalidate_library();
    return rc;
}

/* ------------------------------------------------------------------ */
/* Status / library                                                   */
/* ------------------------------------------------------------------ */
void player_now_json(char *out, size_t out_len)
{
    conn_t c;
    char line[1024];
    char state[8] = "stop";
    char file[PATH_LEN] = "", title[TAG_LEN] = "", artist[TAG_LEN] = "", album[TAG_LEN] = "";
    double elapsed = 0, duration = 0;

    if (conn_open(&c) < 0) {
        snprintf(out, out_len,
                 "{\"source\":\"media\",\"state\":\"stop\",\"file\":null,\"title\":null,"
                 "\"artist\":null,\"album\":null,\"elapsed\":0,\"duration\":0,\"hasArt\":false}");
        return;
    }

    conn_send(&c, "command_list_begin\nstatus\ncurrentsong\ncommand_list_end\n");
    while (conn_line(&c, line, sizeof(line)) == 0) {
        if (!strcmp(line, "OK") || !strncmp(line, "ACK", 3)) break;
        char *colon = strchr(line, ':');
        if (!colon) continue;
        *colon = '\0';
        const char *k = line, *v = colon + 1;
        while (*v == ' ') v++;

        if (!strcmp(k, "state")) snprintf(state, sizeof(state), "%s", v);
        else if (!strcmp(k, "elapsed")) elapsed = atof(v);
        else if (!strcmp(k, "duration")) duration = atof(v);
        else if (!strcmp(k, "file")) snprintf(file, sizeof(file), "%s", v);
        else if (!strcmp(k, "Title")) snprintf(title, sizeof(title), "%s", v);
        else if (!strcmp(k, "Artist")) snprintf(artist, sizeof(artist), "%s", v);
        else if (!strcmp(k, "Album")) snprintf(album, sizeof(album), "%s", v);
    }
    conn_close(&c);

    char jf[PATH_LEN * 2], jt[TAG_LEN * 2], ja[TAG_LEN * 2], jal[TAG_LEN * 2];
    json_str_or_null(jf, sizeof(jf), file[0] ? file : NULL);
    json_str_or_null(jt, sizeof(jt), title[0] ? title : NULL);
    json_str_or_null(ja, sizeof(ja), artist[0] ? artist : NULL);
    json_str_or_null(jal, sizeof(jal), album[0] ? album : NULL);

    snprintf(out, out_len,
             "{\"source\":\"media\",\"state\":\"%s\",\"file\":%s,\"title\":%s,"
             "\"artist\":%s,\"album\":%s,\"elapsed\":%d,\"duration\":%d,\"hasArt\":%s}",
             state, jf, jt, ja, jal,
             (int)(elapsed + 0.5), (int)(duration + 0.5), file[0] ? "true" : "false");
}

static int usb_present(void)
{
    DIR *d = opendir(MUSIC_DIR);
    if (!d) return 0;
    struct dirent *e;
    int any = 0;
    while ((e = readdir(d))) {
        if (e->d_name[0] != '.') { any = 1; break; }
    }
    closedir(d);
    return any;
}

void player_library_json(char *out, size_t out_len)
{
    conn_t c;
    char line[1024];
    size_t o = 0;
    int count = 0, updating = 0, have_track = 0;
    char file[PATH_LEN] = "", title[TAG_LEN] = "", artist[TAG_LEN] = "", album[TAG_LEN] = "";
    double dur = 0;

    /* A DB update in flight, or one we just asked for. */
    if (conn_open(&c) == 0) {
        conn_send(&c, "status\n");
        while (conn_line(&c, line, sizeof(line)) == 0) {
            if (!strcmp(line, "OK") || !strncmp(line, "ACK", 3)) break;
            if (!strncmp(line, "updating_db:", 12)) updating = 1;
        }
        conn_close(&c);
    }
    if (time(NULL) - s_rescan_at < 6) updating = 1;

    o += snprintf(out + o, out_len - o, "{\"usb\":%s,\"updating\":%s,\"tracks\":[",
                  usb_present() ? "true" : "false", updating ? "true" : "false");

    if (conn_open(&c) < 0) {
        snprintf(out, out_len, "{\"usb\":false,\"updating\":false,\"tracks\":[]}");
        return;
    }

    conn_send(&c, "listallinfo\n");
    for (;;) {
        if (conn_line(&c, line, sizeof(line)) != 0) break;
        int done = (!strcmp(line, "OK") || !strncmp(line, "ACK", 3));
        char *colon = done ? NULL : strchr(line, ':');

        /* A new "file:" (or the end) flushes the previous entry. */
        if ((colon && !strncmp(line, "file:", 5)) || done) {
            if (have_track && count < MAX_TRACKS && out_len - o > 1600) {
                char jf[PATH_LEN * 2], jt[TAG_LEN * 2], ja[TAG_LEN * 2], jal[TAG_LEN * 2];
                const char *base = strrchr(file, '/');
                json_str_or_null(jf, sizeof(jf), file);
                json_str_or_null(jt, sizeof(jt),
                                 title[0] ? title : (base ? base + 1 : file));
                json_str_or_null(ja, sizeof(ja), artist);
                json_str_or_null(jal, sizeof(jal), album);
                o += snprintf(out + o, out_len - o,
                              "%s{\"file\":%s,\"title\":%s,\"artist\":%s,\"album\":%s,"
                              "\"duration\":%d,\"hasArt\":true}",
                              count ? "," : "", jf, jt, ja, jal, (int)(dur + 0.5));
                count++;
            }
            file[0] = title[0] = artist[0] = album[0] = '\0';
            dur = 0;
            have_track = 0;
        }
        if (done) break;
        if (!colon) continue;

        *colon = '\0';
        const char *k = line, *v = colon + 1;
        while (*v == ' ') v++;

        if (!strcmp(k, "file")) { snprintf(file, sizeof(file), "%s", v); have_track = 1; }
        else if (!strcmp(k, "Title")) snprintf(title, sizeof(title), "%s", v);
        else if (!strcmp(k, "Artist")) snprintf(artist, sizeof(artist), "%s", v);
        else if (!strcmp(k, "Album")) snprintf(album, sizeof(album), "%s", v);
        else if (!strcmp(k, "duration")) dur = atof(v);
        else if (!strcmp(k, "Time") && dur == 0) dur = atof(v);
    }
    conn_close(&c);

    snprintf(out + o, out_len - o, "]}");
    (void)mpd_quote;   /* reserved for future quoted-arg commands */
}
