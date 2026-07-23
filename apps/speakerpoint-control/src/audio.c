#include "audio.h"
#include "player.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STATE_PATH "/var/run/speakerpoint-control.state"
#define APPLY_BIN "/usr/bin/speakerpoint-audio-apply"

static int s_volume = 35;
static char s_output[8] = "off";

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

static void save_state(void)
{
    FILE *f = fopen(STATE_PATH, "w");
    if (!f) return;
    fprintf(f, "%d %s\n", s_volume, s_output);
    fclose(f);
}

void audio_load(void)
{
    FILE *f = fopen(STATE_PATH, "r");
    if (!f) return;
    int volume;
    char output[8];
    if (fscanf(f, "%d %7s", &volume, output) == 2) {
        s_volume = clamp_volume(volume);
        if (audio_valid_output(output)) {
            snprintf(s_output, sizeof(s_output), "%s", output);
        }
    }
    fclose(f);
}

void audio_apply_startup(void)
{
    run_apply("init");
    run_apply("volume %d", s_volume);
    run_apply("output %s", s_output);
}

void audio_set_output(const char *mode)
{
    if (!audio_valid_output(mode)) return;
    snprintf(s_output, sizeof(s_output), "%s", mode);
    run_apply("output %s", s_output);
    save_state();
}

void audio_set_volume(int volume)
{
    s_volume = clamp_volume(volume);
    run_apply("volume %d", s_volume);
    save_state();
}

void audio_tone(const char *channel)
{
    const char *ch = channel ? channel : "both";
    if (strcmp(ch, "left") && strcmp(ch, "right") && strcmp(ch, "both")) ch = "both";

    /* speaker-test needs the ALSA device exclusively; free it from mpg123. */
    player_pause();
    run_apply("tone %s", ch);
}

void audio_tone_stop(void)
{
    run_apply("tone-stop");
}

void audio_state_json(char *out, size_t out_len)
{
    snprintf(out, out_len, "{\"ok\":true,\"volume\":%d,\"output\":\"%s\"}", s_volume, s_output);
}
