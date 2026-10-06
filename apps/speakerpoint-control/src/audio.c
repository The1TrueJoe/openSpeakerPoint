#include "audio.h"
#include "player.h"

#include <fcntl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

/* /data is the osp-data jffs2 on an installed box (S03osp-data), tmpfs on a
 * netbooted one, so settings survive a power cycle exactly when the box is
 * installed. */
#define STATE_PATH "/data/speakerpoint-control.state"
#define APPLY_BIN "/usr/bin/speakerpoint-audio-apply"

/* The D2-41051 amp (speakers): master volume register 0x000000, a signed
 * 24-bit gain where 0x800000 is 0 dB. */
#define D2_I2C "/dev/i2c-0"
#define D2_ADDR 0x59
/* The codec's Master/Front attenuators bottom out here. */
#define CODEC_FLOOR_DB -46.5
/* A volume drag sends many changes; write the state file once it settles. */
#define SAVE_DELAY_S 2

/* Defaults for a first boot (or a netboot, where /data is tmpfs). "both"
 * (not "off") so audio works out of the box without having to pick an output
 * first. */
static int s_volume = 70;
static char s_output[8] = "both";
static char s_source[8] = "media";

static bool s_dirty;
static time_t s_changed_at;

/* The volume curve, the one place it lives: an audio taper, gain = (v/100)^2,
 * i.e. 40*log10(100/v) dB of attenuation (60% -> -8.9 dB, 10% -> -40 dB), 0 =
 * mute. The apply script gets the resulting values rather than working them
 * out itself (it has only a 5%-step table, for running it by hand). */
static unsigned d2_word(int v)
{
    if (v <= 0) return 0;                   /* full cut */
    double g = (v / 100.0) * (v / 100.0);
    long x = lround(g * 8388608.0);         /* 2^23 */
    return (unsigned)(0x1000000L - x) & 0xffffff;
}

static double vol_db(int v)
{
    if (v <= 0) return -100.0;
    double db = -40.0 * log10(100.0 / v);
    if (db > -0.05) return 0.0;             /* print "0.0", not "-0.0" */
    return db < CODEC_FLOOR_DB ? CODEC_FLOOR_DB : db;
}

/* One register write to the amp, straight over i2c-dev: microseconds, where
 * the apply script's i2ctransfer + mixer batch takes seconds on this CPU. */
static int d2_write(unsigned reg, unsigned val)
{
    unsigned char b[6] = {
        (unsigned char)(reg >> 16), (unsigned char)(reg >> 8), (unsigned char)reg,
        (unsigned char)(val >> 16), (unsigned char)(val >> 8), (unsigned char)val,
    };
    struct i2c_msg msg = { .addr = D2_ADDR, .flags = 0, .len = sizeof(b), .buf = b };
    struct i2c_rdwr_ioctl_data x = { .msgs = &msg, .nmsgs = 1 };
    int fd = open(D2_I2C, O_RDWR);
    if (fd < 0) return -1;
    int r = ioctl(fd, I2C_RDWR, &x);
    close(fd);
    return r < 0 ? -1 : 0;
}

/* The codec's Master and Front (RCA, and the S/PDIF feed in "both"), in one
 * amixer process. */
static void codec_volume(int v)
{
    FILE *p = popen("amixer -c0 -q -s >/dev/null 2>&1", "w");
    if (!p) return;
    if (v <= 0) {
        fputs("sset Master 0% mute\nsset Front 0% mute\n", p);
    } else {
        double db = vol_db(v);
        fprintf(p, "sset Master %.1fdB unmute\nsset Front %.1fdB unmute\n", db, db);
    }
    pclose(p);
}

/* Volume alone, without re-applying the routing: on the amp in "amp" mode,
 * on the codec where the analog path is live (the amp then sits at 0 dB). */
static void volume_hw(void)
{
    if (!strcmp(s_output, "amp")) {
        if (d2_write(0x000000, d2_word(s_volume)) < 0)
            fprintf(stderr, "speakerpoint-control: D2 volume write failed\n");
    } else if (!strcmp(s_output, "rca") || !strcmp(s_output, "both")) {
        codec_volume(s_volume);
    }
}

static int clamp_volume(int v)
{
    if (v < 0) return 0;
    if (v > 100) return 100;
    return v;
}

bool audio_valid_output(const char *mode)
{
    return mode && (!strcmp(mode, "off") || !strcmp(mode, "rca") ||
                    !strcmp(mode, "amp") || !strcmp(mode, "both"));
}

bool audio_valid_source(const char *src)
{
    return src && (!strcmp(src, "media") || !strcmp(src, "linein"));
}

const char *audio_source(void)
{
    return s_source;
}

const char *audio_output(void)
{
    return s_output;
}

int audio_volume(void)
{
    return s_volume;
}

static void run_apply(const char *fmt, ...)
{
    char cmd[256];
    char args[192];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(args, sizeof(args), fmt, ap);
    va_end(ap);

    snprintf(cmd, sizeof(cmd), "%s %s >/dev/null 2>&1", APPLY_BIN, args);
    (void)system(cmd);
}

/* Push the COMPLETE desired state to the hardware in one call. Everything
 * routes through here - there are no partial/incremental updates, so the
 * hardware can never end up in a combination the daemon didn't intend (which
 * is how the amp previously ended up hard-muted at the chip while every ALSA
 * control still read back as unmuted). */
static void apply_all(void)
{
    run_apply("apply %s %s %d %06x %.1f", s_output, s_source, s_volume, d2_word(s_volume), vol_db(s_volume));
}

static void save_state(void)
{
    FILE *f = fopen(STATE_PATH, "w");
    if (!f) return;
    fprintf(f, "%d %s %s\n", s_volume, s_output, s_source);
    fclose(f);
}

void audio_load(void)
{
    FILE *f = fopen(STATE_PATH, "r");
    if (!f) return;
    int volume;
    char output[8], source[8];
    int n = fscanf(f, "%d %7s %7s", &volume, output, source);
    if (n >= 2) {
        s_volume = clamp_volume(volume);
        if (audio_valid_output(output)) {
            snprintf(s_output, sizeof(s_output), "%s", output);
        }
    }
    if (n >= 3 && audio_valid_source(source)) {
        snprintf(s_source, sizeof(s_source), "%s", source);
    }
    fclose(f);
}

void audio_apply_startup(void)
{
    apply_all();
}

void audio_reapply(void)
{
    apply_all();
}

void audio_set_output(const char *mode)
{
    if (!audio_valid_output(mode)) return;
    snprintf(s_output, sizeof(s_output), "%s", mode);
    apply_all();
    save_state();
}

void audio_set_volume(int volume)
{
    s_volume = clamp_volume(volume);
    volume_hw();
    s_dirty = true;
    s_changed_at = time(NULL);
}

void audio_tick(void)
{
    if (s_dirty && time(NULL) - s_changed_at >= SAVE_DELAY_S) {
        save_state();
        s_dirty = false;
    }
}

void audio_set_source(const char *src)
{
    if (!audio_valid_source(src)) return;
    snprintf(s_source, sizeof(s_source), "%s", src);
    /* The codec is exclusive (no software mixer), so hand it over cleanly. */
    if (!strcmp(src, "linein")) {
        player_release();
    }
    apply_all();
    save_state();
}

void audio_tone(const char *channel)
{
    const char *ch = channel ? channel : "both";
    if (strcmp(ch, "left") && strcmp(ch, "right") && strcmp(ch, "both")) ch = "both";

    /* speaker-test needs the ALSA device exclusively; free it from MPD. */
    player_release();
    run_apply("tone %s", ch);
}

void audio_tone_stop(void)
{
    run_apply("tone-stop");
}

void audio_state_json(char *out, size_t out_len)
{
    snprintf(out, out_len,
             "{\"ok\":true,\"volume\":%d,\"output\":\"%s\",\"source\":\"%s\"}",
             s_volume, s_output, s_source);
}

void audio_status_text(char *out, size_t out_len)
{
    FILE *p = popen(APPLY_BIN " status 2>&1", "r");
    if (!p) {
        snprintf(out, out_len, "error: could not run %s status\n", APPLY_BIN);
        return;
    }
    size_t o = 0;
    while (o + 1 < out_len) {
        size_t n = fread(out + o, 1, out_len - 1 - o, p);
        if (n == 0) break;
        o += n;
    }
    out[o] = '\0';
    pclose(p);
}
