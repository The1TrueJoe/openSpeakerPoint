/* Audio routing, volume and test tones, driven via speakerpoint-audio-apply. */
#ifndef SPKR_AUDIO_H
#define SPKR_AUDIO_H

#include <stddef.h>
#include <stdbool.h>

void audio_load(void);              /* read persisted state */
void audio_apply_startup(void);     /* init hardware + push saved state */

bool audio_valid_output(const char *mode);
void audio_set_output(const char *mode);
void audio_set_volume(int volume);  /* clamped 0-100 */

/* Pink noise on the given channel (left|right|both); no tone-type choice -
 * pink is the only test signal exposed. */
void audio_tone(const char *channel);
void audio_tone_stop(void);

/* Serialise current routing state as JSON into out. */
void audio_state_json(char *out, size_t out_len);

#endif
