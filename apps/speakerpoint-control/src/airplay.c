#include "airplay.h"
#include "jsonutil.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Set explicitly in /etc/shairport-sync.conf (metadata.pipe_name). */
#define PIPE_PATH "/tmp/shairport-sync-metadata"
#define FLAG_PATH "/run/airplay.active"
#define BUF_MAX 16384
#define FIELD_LEN 256

static int s_fd = -1;
static char s_buf[BUF_MAX];
static size_t s_buflen = 0;

static char s_title[FIELD_LEN];
static char s_artist[FIELD_LEN];
static char s_album[FIELD_LEN];
static int s_was_active = 0;

void airplay_start(void)
{
    mkfifo(PIPE_PATH, 0600); /* ignore EEXIST */
    /* O_RDWR (not O_RDONLY) so read() blocks/returns EAGAIN between writer
     * sessions instead of hitting EOF and needing the fd reopened. */
    s_fd = open(PIPE_PATH, O_RDWR | O_NONBLOCK);
}

int airplay_active(void)
{
    return access(FLAG_PATH, F_OK) == 0;
}

static int hexval(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* An 8-hex-digit FourCC (e.g. "6173616c") -> its 4 ASCII bytes ("asal"). */
static void fourcc_decode(const char *hex8, char out[5])
{
    for (int i = 0; i < 4; i++) {
        int hi = hexval((unsigned char)hex8[i * 2]);
        int lo = hexval((unsigned char)hex8[i * 2 + 1]);
        out[i] = (hi >= 0 && lo >= 0) ? (char)((hi << 4) | lo) : '?';
    }
    out[4] = '\0';
}

static int b64val(int c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static size_t b64_decode(const char *in, size_t inlen, unsigned char *out, size_t outcap)
{
    size_t o = 0;
    int vals[4], nv = 0;
    for (size_t i = 0; i < inlen; i++) {
        int v = b64val((unsigned char)in[i]);
        if (v < 0) continue;
        vals[nv++] = v;
        if (nv == 4) {
            if (o < outcap) out[o++] = (unsigned char)((vals[0] << 2) | (vals[1] >> 4));
            if (o < outcap) out[o++] = (unsigned char)(((vals[1] & 0xF) << 4) | (vals[2] >> 2));
            if (o < outcap) out[o++] = (unsigned char)(((vals[2] & 0x3) << 6) | vals[3]);
            nv = 0;
        }
    }
    if (nv >= 2) {
        if (o < outcap) out[o++] = (unsigned char)((vals[0] << 2) | (vals[1] >> 4));
        if (nv >= 3 && o < outcap) out[o++] = (unsigned char)(((vals[1] & 0xF) << 4) | (vals[2] >> 2));
    }
    return o;
}

static int extract_tag(const char *block, const char *tag, char *out, size_t outcap)
{
    char open_tag[16], close_tag[16];
    snprintf(open_tag, sizeof(open_tag), "<%s>", tag);
    snprintf(close_tag, sizeof(close_tag), "</%s>", tag);

    const char *s = strstr(block, open_tag);
    if (!s) return -1;
    s += strlen(open_tag);
    const char *e = strstr(s, close_tag);
    if (!e) return -1;

    size_t len = (size_t)(e - s);
    if (len >= outcap) len = outcap - 1;
    memcpy(out, s, len);
    out[len] = '\0';
    return 0;
}

/* Process one complete "<item>...</item>" block (item[itemlen] is the byte
 * right after it, already known to exist in the buffer). */
static void process_item(char *item, size_t itemlen)
{
    char saved = item[itemlen];
    item[itemlen] = '\0';

    char typehex[16] = "", codehex[16] = "";
    extract_tag(item, "type", typehex, sizeof(typehex));
    extract_tag(item, "code", codehex, sizeof(codehex));

    char type[5] = "", code[5] = "";
    if (strlen(typehex) == 8) fourcc_decode(typehex, type);
    if (strlen(codehex) == 8) fourcc_decode(codehex, code);

    char decoded[FIELD_LEN] = "";
    const char *dtag = strstr(item, "<data");
    if (dtag) {
        const char *gt = strchr(dtag, '>');
        const char *dend = gt ? strstr(gt, "</data>") : NULL;
        if (gt && dend) {
            unsigned char raw[FIELD_LEN];
            size_t rn = b64_decode(gt + 1, (size_t)(dend - (gt + 1)), raw, sizeof(raw) - 1);
            memcpy(decoded, raw, rn);
            decoded[rn] = '\0';
        }
    }

    if (!strcmp(type, "core") && !strcmp(code, "minm")) {
        snprintf(s_title, sizeof(s_title), "%s", decoded);
    } else if (!strcmp(type, "core") && !strcmp(code, "asar")) {
        snprintf(s_artist, sizeof(s_artist), "%s", decoded);
    } else if (!strcmp(type, "core") && !strcmp(code, "asal")) {
        snprintf(s_album, sizeof(s_album), "%s", decoded);
    }

    item[itemlen] = saved;
}

void airplay_poll(void)
{
    if (s_fd >= 0) {
        char tmp[2048];
        ssize_t n;
        while ((n = read(s_fd, tmp, sizeof(tmp))) > 0) {
            size_t room = BUF_MAX - s_buflen - 1;
            size_t take = (size_t)n > room ? room : (size_t)n;
            memcpy(s_buf + s_buflen, tmp, take);
            s_buflen += take;
            s_buf[s_buflen] = '\0';
        }

        char *pos = s_buf;
        for (;;) {
            char *start = strstr(pos, "<item>");
            if (!start) break;
            char *end = strstr(start, "</item>");
            if (!end) break;
            end += 7; /* strlen("</item>") */
            process_item(start, (size_t)(end - start));
            pos = end;
        }

        size_t consumed = (size_t)(pos - s_buf);
        if (consumed > 0) {
            size_t remain = s_buflen - consumed;
            memmove(s_buf, pos, remain);
            s_buflen = remain;
            s_buf[s_buflen] = '\0';
        }
        if (s_buflen >= BUF_MAX - 1) { /* malformed/stuck stream: drop it */
            s_buflen = 0;
            s_buf[0] = '\0';
        }
    }

    int now_active = airplay_active();
    if (s_was_active && !now_active) {
        s_title[0] = s_artist[0] = s_album[0] = '\0';
    }
    s_was_active = now_active;
}

void airplay_now_json(char *out, size_t out_len)
{
    char jt[FIELD_LEN * 2], ja[FIELD_LEN * 2], jal[FIELD_LEN * 2];
    json_str_or_null(jt, sizeof(jt), s_title[0] ? s_title : NULL);
    json_str_or_null(ja, sizeof(ja), s_artist[0] ? s_artist : NULL);
    json_str_or_null(jal, sizeof(jal), s_album[0] ? s_album : NULL);

    snprintf(out, out_len,
             "{\"source\":\"airplay\",\"state\":\"play\",\"file\":null,"
             "\"title\":%s,\"artist\":%s,\"album\":%s,\"elapsed\":0,"
             "\"duration\":0,\"hasArt\":false}",
             jt, ja, jal);
}
