/* Line-in measurement: record the RCA input for a few seconds and report, per
 * channel, the RMS and peak level (dBFS) and the dominant frequency.
 *
 * This is the end-to-end test instrument: play a known tone into the line
 * input from another device and this says whether it arrived, on which
 * channel, how loud, and at what pitch — no ears required. */
#ifndef SPKR_MEASURE_H
#define SPKR_MEASURE_H

#include <stddef.h>

#define MEASURE_MAX_SECONDS 10

/* Record `seconds` (clamped 1..MEASURE_MAX_SECONDS) from the line input and
 * write the result as JSON into out:
 *   {"ok":true,"seconds":1,"rate":48000,"frames":48000,
 *    "left":{"rms_dbfs":-12.1,"peak_dbfs":-9.0,"freq_hz":1000.2},
 *    "right":{...}}
 * or {"ok":false,"error":"..."}. Blocks for the duration of the recording.
 * Returns 0 when a measurement was made. */
int measure_linein(int seconds, char *out, size_t out_len);

#endif
