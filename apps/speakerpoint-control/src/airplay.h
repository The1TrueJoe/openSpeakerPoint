/* AirPlay status: reads shairport-sync's session-active flag (toggled by
 * airplay-hook around each stream) and its metadata pipe (title/artist/
 * album, when the source sends any). */
#ifndef SPKR_AIRPLAY_H
#define SPKR_AIRPLAY_H

#include <stddef.h>

/* Opens (creating if needed) the metadata FIFO. Safe to call once at startup. */
void airplay_start(void);

/* Drains and parses whatever's waiting on the metadata pipe. Call ~1/sec. */
void airplay_poll(void);

/* True while shairport-sync has an active AirPlay session. */
int airplay_active(void);

/* JSON now-playing for the active session: { source, state, file, title,
 * artist, album, elapsed, duration, hasArt }. */
void airplay_now_json(char *out, size_t out_len);

#endif
