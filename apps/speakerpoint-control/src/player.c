#include "player.h"

#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#define MUSIC_DIR "/media/usb"
#define MAX_TRACKS 400
#define SCAN_CACHE_SECS 3
#define TAG_LEN 256
#define PATH_LEN 512

typedef struct {
    char file[PATH_LEN];
    char title[TAG_LEN];
    char artist[TAG_LEN];
    char album[TAG_LEN];
} track_t;

/* ------------------------------------------------------------------ */
/* State                                                              */
/* ------------------------------------------------------------------ */
static int to_mpg = -1;     /* write commands to mpg123 stdin */
static int from_mpg = -1;   /* read @ status from mpg123 stdout */
static pid_t mpg_pid = -1;

static track_t s_lib[MAX_TRACKS];
static int s_count = 0;
static time_t s_scanned = 0;

static int s_index = -1;    /* currently loaded library index */
static int s_state = 0;     /* 0 stop, 1 pause, 2 play */
static double s_pos = 0, s_dur = 0;

static char s_linebuf[1024];
static size_t s_linelen = 0;

/* ------------------------------------------------------------------ */
/* JSON helpers                                                       */
/* ------------------------------------------------------------------ */
static void json_escape(const char *in, char *out, size_t out_len)
{
    size_t o = 0;
    for (; in && *in && o + 2 < out_len; in++) {
        unsigned char ch = (unsigned char)*in;
        if (ch == '"' || ch == '\\') { out[o++] = '\\'; out[o++] = (char)ch; }
        else if (ch >= 0x20) out[o++] = (char)ch;
    }
    out[o] = '\0';
}

static int json_str_or_null(char *dst, size_t cap, const char *val)
{
    if (!val || !*val) return snprintf(dst, cap, "null");
    char esc[TAG_LEN * 2];
    json_escape(val, esc, sizeof(esc));
    return snprintf(dst, cap, "\"%s\"", esc);
}

/* ------------------------------------------------------------------ */
/* ID3v2 parsing                                                      */
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

/* Decode an ID3 text-frame payload (enc byte + text) into a UTF-8-ish C
 * string. Handles Latin-1 (0) and UTF-8 (3) directly; UTF-16 (1/2) is
 * down-sampled to its low bytes, which is lossy but keeps ASCII titles
 * readable without pulling in an iconv dependency. */
static void decode_text(const unsigned char *data, size_t len, char *out, size_t out_len)
{
    if (len == 0) { out[0] = '\0'; return; }
    int enc = data[0];
    const unsigned char *t = data + 1;
    size_t tl = len - 1;
    size_t o = 0;

    if (enc == 1 || enc == 2) {          /* UTF-16 */
        size_t i = 0;
        if (tl >= 2 && ((t[0] == 0xff && t[1] == 0xfe) || (t[0] == 0xfe && t[1] == 0xff)))
            i = 2;                        /* skip BOM */
        int le = !(tl >= 2 && t[0] == 0xfe && t[1] == 0xff) && enc != 2;
        for (; i + 1 < tl && o + 1 < out_len; i += 2) {
            unsigned char c = le ? t[i] : t[i + 1];
            if (c == 0) break;
            if (c >= 0x20) out[o++] = (char)c;
        }
    } else {                             /* Latin-1 or UTF-8: copy printable */
        for (size_t i = 0; i < tl && o + 1 < out_len; i++) {
            if (t[i] == 0) break;
            if ((unsigned char)t[i] >= 0x20 || (unsigned char)t[i] >= 0x80)
                out[o++] = (char)t[i];
        }
    }
    out[o] = '\0';
}

/* Walk the ID3v2 tag at the start of path. For each frame, invoke cb.
 * Returns 0 if a tag was found. cb returns non-zero to stop early. */
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

/* --- tag collection --- */
typedef struct { char *title, *artist, *album; size_t cap; } tags_ctx;

static int tags_cb(const char *id, const unsigned char *data, unsigned len, void *ctx)
{
    tags_ctx *t = ctx;
    if (!strcmp(id, "TIT2") || !strcmp(id, "TT2")) decode_text(data, len, t->title, t->cap);
    else if (!strcmp(id, "TPE1") || !strcmp(id, "TP1")) decode_text(data, len, t->artist, t->cap);
    else if (!strcmp(id, "TALB") || !strcmp(id, "TAL")) decode_text(data, len, t->album, t->cap);
    return 0;
}

static void id3_read_tags(const char *path, char *title, char *artist, char *album)
{
    title[0] = artist[0] = album[0] = '\0';
    tags_ctx ctx = { title, artist, album, TAG_LEN };
    id3_walk(path, tags_cb, &ctx);
}

/* --- album art extraction --- */
typedef struct { void *buf; size_t len; char mime[64]; } art_ctx;

static int art_cb(const char *id, const unsigned char *data, unsigned len, void *ctx)
{
    art_ctx *a = ctx;
    if (strcmp(id, "APIC") && strcmp(id, "PIC")) return 0;
    if (len < 4) return 0;

    unsigned p = 1;                       /* skip text encoding */
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
        p++;                              /* NUL after MIME */
    }
    if (p >= len) return 0;
    p += 1;                               /* picture type byte */
    while (p < len && data[p]) p++;       /* description */
    p++;                                  /* NUL after description */
    if (p >= len) return 0;

    size_t plen = len - p;
    a->buf = malloc(plen);
    if (!a->buf) return 1;
    memcpy(a->buf, data + p, plen);
    a->len = plen;
    return 1;                             /* stop after first picture */
}

/* ------------------------------------------------------------------ */
/* Library scan                                                       */
/* ------------------------------------------------------------------ */
static int is_mp3(const char *name)
{
    const char *dot = strrchr(name, '.');
    return dot && !strcasecmp(dot, ".mp3");
}

static void scan_dir(const char *dir, int depth)
{
    if (depth > 6 || s_count >= MAX_TRACKS) return;
    DIR *d = opendir(dir);
    if (!d) return;

    struct dirent *e;
    while ((e = readdir(d)) && s_count < MAX_TRACKS) {
        if (e->d_name[0] == '.') continue;
        char path[PATH_LEN];
        if ((size_t)snprintf(path, sizeof(path), "%s/%s", dir, e->d_name) >= sizeof(path))
            continue;

        if (e->d_type == DT_DIR) {
            scan_dir(path, depth + 1);
        } else if (is_mp3(e->d_name)) {
            track_t *t = &s_lib[s_count];
            snprintf(t->file, sizeof(t->file), "%s", path);
            id3_read_tags(path, t->title, t->artist, t->album);
            s_count++;
        }
    }
    closedir(d);
}

static void ensure_library(void)
{
    time_t now = time(NULL);
    if (s_count > 0 && now - s_scanned < SCAN_CACHE_SECS) return;
    s_count = 0;
    scan_dir(MUSIC_DIR, 0);
    s_scanned = now;
}

static int usb_present(void)
{
    /* /media/usb is a mountpoint only when a drive is mounted. */
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

/* ------------------------------------------------------------------ */
/* mpg123 -R child                                                    */
/* ------------------------------------------------------------------ */
static void mpg_send(const char *cmd)
{
    if (to_mpg < 0) return;
    (void)!write(to_mpg, cmd, strlen(cmd));
}

void player_start(void)
{
    int in_pipe[2], out_pipe[2];   /* in: daemon->mpg123, out: mpg123->daemon */
    if (pipe(in_pipe) < 0 || pipe(out_pipe) < 0) return;

    mpg_pid = fork();
    if (mpg_pid < 0) return;

    if (mpg_pid == 0) {
        dup2(in_pipe[0], STDIN_FILENO);
        dup2(out_pipe[1], STDOUT_FILENO);
        close(in_pipe[0]); close(in_pipe[1]);
        close(out_pipe[0]); close(out_pipe[1]);
        execlp("mpg123", "mpg123", "-R", "--quiet", (char *)NULL);
        _exit(127);
    }

    close(in_pipe[0]);
    close(out_pipe[1]);
    to_mpg = in_pipe[1];
    from_mpg = out_pipe[0];
    fcntl(from_mpg, F_SETFL, O_NONBLOCK);
    signal(SIGCHLD, SIG_IGN);
}

int player_status_fd(void)
{
    return from_mpg;
}

static void advance(int delta);

static void parse_status_line(const char *line)
{
    if (line[0] != '@' || line[1] == '\0') return;
    switch (line[1]) {
    case 'F': {  /* @F <frame> <framesleft> <sec> <secleft> */
        double cur = 0, left = 0;
        int a, b;
        if (sscanf(line + 2, "%d %d %lf %lf", &a, &b, &cur, &left) >= 4) {
            s_pos = cur;
            s_dur = cur + left;
            if (s_state == 0) s_state = 2;
        }
        break;
    }
    case 'P': {  /* @P 0 stopped, 1 paused, 2 playing */
        int p = atoi(line + 2);
        if (p == 2) s_state = 2;
        else if (p == 1) s_state = 1;
        else {       /* stopped: natural end -> advance, else user stop */
            if (s_state == 2 && s_dur > 0 && s_pos >= s_dur - 1.5) advance(1);
            else s_state = 0;
        }
        break;
    }
    default:
        break;   /* @I track info, @E error, @R version - ignored */
    }
}

void player_poll(void)
{
    if (from_mpg < 0) return;
    char buf[512];
    ssize_t n;
    while ((n = read(from_mpg, buf, sizeof(buf))) > 0) {
        for (ssize_t i = 0; i < n; i++) {
            if (buf[i] == '\n') {
                s_linebuf[s_linelen] = '\0';
                parse_status_line(s_linebuf);
                s_linelen = 0;
            } else if (s_linelen < sizeof(s_linebuf) - 1) {
                s_linebuf[s_linelen++] = buf[i];
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Transport                                                          */
/* ------------------------------------------------------------------ */
static void load_index(int index)
{
    if (index < 0 || index >= s_count) return;
    char cmd[PATH_LEN + 8];
    s_index = index;
    s_pos = 0;
    s_dur = 0;
    s_state = 2;
    snprintf(cmd, sizeof(cmd), "LOAD %s\n", s_lib[index].file);
    mpg_send(cmd);
}

static void advance(int delta)
{
    ensure_library();
    if (s_count == 0) { s_state = 0; return; }
    int next = s_index + delta;
    if (next < 0) next = 0;
    if (next >= s_count) { s_state = 0; mpg_send("STOP\n"); return; }
    load_index(next);
}

int player_play_index(int index)
{
    ensure_library();
    if (index < 0 || index >= s_count) return -1;
    load_index(index);
    return 0;
}

int player_transport(const char *cmd)
{
    if (!cmd) return -1;
    if (!strcmp(cmd, "play")) {
        if (s_state == 1) mpg_send("PAUSE\n");         /* resume */
        else if (s_index >= 0) load_index(s_index);    /* (re)start current */
        else return player_play_index(0);
    } else if (!strcmp(cmd, "pause")) {
        if (s_state == 2) mpg_send("PAUSE\n");
    } else if (!strcmp(cmd, "stop")) {
        s_state = 0;
        mpg_send("STOP\n");
    } else if (!strcmp(cmd, "next")) {
        advance(1);
    } else if (!strcmp(cmd, "prev")) {
        advance(-1);
    } else {
        return -1;
    }
    return 0;
}

int player_pause(void)
{
    if (s_state == 2) mpg_send("PAUSE\n");
    return 0;
}

int player_seek(int seconds)
{
    char cmd[32];
    if (seconds < 0) seconds = 0;
    snprintf(cmd, sizeof(cmd), "JUMP %ds\n", seconds);
    mpg_send(cmd);
    s_pos = seconds;
    return 0;
}

int player_rescan(void)
{
    s_scanned = 0;   /* force next ensure_library() to rescan */
    return 0;
}

/* ------------------------------------------------------------------ */
/* JSON output                                                        */
/* ------------------------------------------------------------------ */
void player_now_json(char *out, size_t out_len)
{
    const char *state = s_state == 2 ? "play" : s_state == 1 ? "pause" : "stop";
    const track_t *t = (s_index >= 0 && s_index < s_count) ? &s_lib[s_index] : NULL;
    char jf[PATH_LEN * 2], jt[TAG_LEN * 2], ja[TAG_LEN * 2], jal[TAG_LEN * 2];

    json_str_or_null(jf, sizeof(jf), t ? t->file : NULL);
    json_str_or_null(jt, sizeof(jt), t ? t->title : NULL);
    json_str_or_null(ja, sizeof(ja), t ? t->artist : NULL);
    json_str_or_null(jal, sizeof(jal), t ? t->album : NULL);

    snprintf(out, out_len,
             "{\"state\":\"%s\",\"file\":%s,\"title\":%s,\"artist\":%s,\"album\":%s,"
             "\"elapsed\":%d,\"duration\":%d,\"hasArt\":%s}",
             (s_state == 0) ? "stop" : state, jf, jt, ja, jal,
             (int)(s_pos + 0.5), (int)(s_dur + 0.5), t ? "true" : "false");
}

void player_library_json(char *out, size_t out_len)
{
    ensure_library();
    size_t o = 0;
    o += snprintf(out + o, out_len - o, "{\"usb\":%s,\"updating\":false,\"tracks\":[",
                  usb_present() ? "true" : "false");

    for (int i = 0; i < s_count && out_len - o > 1400; i++) {
        char jf[PATH_LEN * 2], jt[TAG_LEN * 2], ja[TAG_LEN * 2], jal[TAG_LEN * 2];
        const char *name = strrchr(s_lib[i].file, '/');
        json_str_or_null(jf, sizeof(jf), s_lib[i].file);
        json_str_or_null(jt, sizeof(jt), s_lib[i].title[0] ? s_lib[i].title : (name ? name + 1 : s_lib[i].file));
        json_str_or_null(ja, sizeof(ja), s_lib[i].artist);
        json_str_or_null(jal, sizeof(jal), s_lib[i].album);
        o += snprintf(out + o, out_len - o,
                      "%s{\"file\":%s,\"title\":%s,\"artist\":%s,\"album\":%s,"
                      "\"duration\":0,\"hasArt\":true}",
                      i ? "," : "", jf, jt, ja, jal);
    }
    snprintf(out + o, out_len - o, "]}");
}

void *player_albumart(const char *uri, size_t *len, const char **mime)
{
    static char mime_buf[64];
    art_ctx ctx;
    memset(&ctx, 0, sizeof(ctx));
    snprintf(ctx.mime, sizeof(ctx.mime), "image/jpeg");

    /* Only serve art for files inside the music dir. */
    if (strncmp(uri, MUSIC_DIR, strlen(MUSIC_DIR)) != 0) return NULL;
    id3_walk(uri, art_cb, &ctx);
    if (!ctx.buf) return NULL;

    *len = ctx.len;
    snprintf(mime_buf, sizeof(mime_buf), "%s", ctx.mime[0] ? ctx.mime : "image/jpeg");
    *mime = mime_buf;
    return ctx.buf;
}
