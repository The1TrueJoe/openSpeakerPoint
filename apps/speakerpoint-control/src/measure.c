#include "measure.h"
#include "audio.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RATE 48000
#define CHANNELS 2
/* The first 100 ms are discarded: the DC blocker below needs to settle, and
 * the codec's capture path can start with a click. */
#define SETTLE_FRAMES (RATE / 10)
/* Below this the "frequency" is just the noise floor's zero crossings. */
#define FREQ_FLOOR_DBFS -60.0
/* Zero-crossing hysteresis, in raw counts (about -50 dBFS): a crossing only
 * counts once the signal has swung through the whole band, so noise riding
 * on the zero line doesn't add crossings. */
#define HYST 100.0

typedef struct {
    double prev_x, y;       /* DC blocker state */
    double sumsq;
    int peak;
    int armed;              /* went below -HYST since the last crossing */
    long crossings;
} chan_t;

static double dbfs(double v)
{
    return v > 0 ? 20.0 * log10(v / 32768.0) : -120.0;
}

static void chan_json(char *out, size_t len, const char *name, const chan_t *c, long frames)
{
    double rms = frames ? sqrt(c->sumsq / (double)frames) : 0;
    double rms_db = dbfs(rms);
    double secs = (double)frames / RATE;
    if (rms_db > FREQ_FLOOR_DBFS && secs > 0) {
        snprintf(out, len, "\"%s\":{\"rms_dbfs\":%.1f,\"peak_dbfs\":%.1f,\"freq_hz\":%.1f}",
                 name, rms_db, dbfs(c->peak), (double)c->crossings / secs);
    } else {
        snprintf(out, len, "\"%s\":{\"rms_dbfs\":%.1f,\"peak_dbfs\":%.1f,\"freq_hz\":null}",
                 name, rms_db, dbfs(c->peak));
    }
}

int measure_linein(int seconds, char *out, size_t out_len)
{
    if (seconds < 1) seconds = 1;
    if (seconds > MEASURE_MAX_SECONDS) seconds = MEASURE_MAX_SECONDS;

    /* Free the capture side (a running line-in loopback owns it) and select
     * the Line input at a fixed 0 dB gain, so levels mean the same thing from
     * one measurement to the next. */
    (void)system("/usr/bin/speakerpoint-audio-apply capture >/dev/null 2>&1");

    char cmd[160];
    snprintf(cmd, sizeof(cmd),
             "arecord -q -D sp_capture -f S16_LE -c %d -r %d -d %d -t raw 2>/dev/null",
             CHANNELS, RATE, seconds + 1);   /* +1 s covers the settle window */
    FILE *p = popen(cmd, "r");
    if (!p) {
        audio_reapply();
        snprintf(out, out_len, "{\"ok\":false,\"error\":\"could not start arecord\"}");
        return -1;
    }

    chan_t ch[CHANNELS];
    memset(ch, 0, sizeof(ch));
    long seen = 0, frames = 0;
    const long want = (long)seconds * RATE;
    int16_t buf[4096];
    size_t n;

    while (frames < want && (n = fread(buf, sizeof(int16_t), 4096, p)) > 0) {
        for (size_t i = 0; i + CHANNELS <= n && frames < want; i += CHANNELS) {
            seen++;
            for (int c = 0; c < CHANNELS; c++) {
                chan_t *k = &ch[c];
                double x = buf[i + c];
                /* One-pole DC blocker: the codec's input has an offset, and
                 * an offset turns RMS into "how far from zero" rather than
                 * "how loud", and stops a sine crossing zero at all. */
                k->y = x - k->prev_x + 0.995 * k->y;
                k->prev_x = x;
                if (seen <= SETTLE_FRAMES) continue;
                k->sumsq += k->y * k->y;
                int a = abs(buf[i + c]);
                if (a > k->peak) k->peak = a;
                if (k->y < -HYST) {
                    k->armed = 1;
                } else if (k->armed && k->y > HYST) {
                    k->armed = 0;
                    k->crossings++;
                }
            }
            if (seen > SETTLE_FRAMES) frames++;
        }
    }
    pclose(p);
    audio_reapply();   /* back to the routing the user chose */

    if (frames < want / 2) {
        snprintf(out, out_len,
                 "{\"ok\":false,\"error\":\"capture returned %ld of %ld frames\"}", frames, want);
        return -1;
    }

    char l[128], r[128];
    chan_json(l, sizeof(l), "left", &ch[0], frames);
    chan_json(r, sizeof(r), "right", &ch[1], frames);
    snprintf(out, out_len,
             "{\"ok\":true,\"seconds\":%d,\"rate\":%d,\"frames\":%ld,%s,%s}",
             seconds, RATE, frames, l, r);
    return 0;
}
