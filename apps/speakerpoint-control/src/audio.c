#include "audio.h"
#include "player.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STATE_PATH "/var/run/speakerpoint-control.state"
#define APPLY_BIN "/usr/bin/speakerpoint-audio-apply"

/* Defaults for a fresh boot: STATE_PATH lives on tmpfs, so it's gone after
 * every power cycle and these are what the device actually comes up with.
 * "both" (not "off") so audio works out of the box without having to pick an
 * output first. */
static int s_volume = 70;
static char s_output[8] = "both";
static char s_source[8] = "media";

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
    run_apply("apply %s %s %d", s_output, s_source, s_volume);
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
    apply_all();
    save_state();
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
