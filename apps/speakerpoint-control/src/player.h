/* Media engine: a thin client for MPD (127.0.0.1:6600), which owns playback,
 * the queue and library indexing. Album art is parsed from ID3 locally.
 * Music lives under /media/usb (auto-mounted from a USB drive). */
#ifndef SPKR_PLAYER_H
#define SPKR_PLAYER_H

#include <stddef.h>

/* One-time startup work (kick USB detection). */
void player_start(void);

/* Optional fd for the daemon's select() set, or -1 when there's nothing to
 * watch (MPD is queried on demand, so state is always read live). */
int player_status_fd(void);
void player_poll(void);

/* Periodic housekeeping (call ~1/sec): pauses media if an AirPlay stream has
 * taken over the codec. */
void player_tick(void);

/* JSON now-playing: { source, state, file, title, artist, album, elapsed, duration, hasArt } */
void player_now_json(char *out, size_t out_len);

/* The same shape for whatever is actually feeding the output: an AirPlay
 * session, the line-in loopback, or USB media. What GET /api/now and MQTT's
 * state/now both report. */
void now_playing_json(char *out, size_t out_len);

/* JSON library: { usb, updating, tracks:[...] } (rescans /media/usb, cached). */
void player_library_json(char *out, size_t out_len);

int player_transport(const char *cmd); /* play|pause|stop|next|prev */
int player_release(void);              /* stop + close the ALSA device so another source can use it */
int player_seek(int seconds);
int player_play_index(int index);
int player_rescan(void);               /* re-enumerate USB + drop the library cache */
void player_invalidate_library(void);  /* drop the library cache only, no USB re-enumeration */
int player_delete_track(const char *path); /* absolute /media/usb path */

/* Embedded album art for uri (an absolute /media/usb path). Returns a malloc'd
 * buffer (caller frees), sets *len and *mime; NULL if none. */
void *player_albumart(const char *uri, size_t *len, const char **mime);

#endif
