/* Audio routing, volume and test tones, driven via speakerpoint-audio-apply. */
#ifndef SPKR_AUDIO_H
#define SPKR_AUDIO_H

#include <stddef.h>
#include <stdbool.h>

void audio_load(void);              /* read persisted state */
void audio_apply_startup(void);     /* init hardware + push saved state */
void audio_reapply(void);           /* push the current state again (e.g. after a measurement borrowed the codec) */

bool audio_valid_output(const char *mode);
void audio_set_output(const char *mode);
void audio_set_volume(int volume);  /* clamped 0-100; volume only, no re-route */
void audio_tick(void);              /* call about once a second: saves a settled volume */

/* Tone on the amp's DSP (Control4's own controls): bass and treble -14..14
 * (0 flat), loudness on/off. Saved like the volume. */
void audio_set_bass(int v);
void audio_set_treble(int v);
void audio_set_loudness(bool on);
int audio_bass(void);
int audio_treble(void);
bool audio_loudness(void);
const char *audio_output(void);
int audio_volume(void);

/* Active input source feeding the output: "media" (USB via MPD) or "linein"
 * (external line-in loopback). Exclusive - selecting one stops the other. */
bool audio_valid_source(const char *src);
void audio_set_source(const char *src);
const char *audio_source(void);

/* Pink noise on the given channel (left|right|both); no tone-type choice -
 * pink is the only test signal exposed. */
void audio_tone(const char *channel);
void audio_tone_stop(void);

/* Serialise current routing state as JSON into out. */
void audio_state_json(char *out, size_t out_len);

/* Read back what the hardware ACTUALLY has set (mixer switches, AC97
 * registers, D2 amp registers) as plain key=value text, for diagnosing
 * intent-vs-reality mismatches. */
void audio_status_text(char *out, size_t out_len);

#endif
