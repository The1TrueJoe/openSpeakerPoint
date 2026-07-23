/* Lightweight MP3 media engine built on mpg123's remote-control mode (-R),
 * plus in-C ID3v2 tag and embedded album-art parsing. Replaces the heavier
 * MPD stack to keep the netbooted initramfs small. Music lives under
 * /media/usb (auto-mounted from a USB drive). */
#ifndef SPKR_PLAYER_H
#define SPKR_PLAYER_H

#include <stddef.h>

/* Spawn the mpg123 -R child. Safe to call once at startup. */
void player_start(void);

/* The mpg123 status pipe fd to add to the daemon's select() set, or -1.
 * When it becomes readable, call player_poll(). */
int player_status_fd(void);
void player_poll(void);

/* JSON now-playing: { state, file, title, artist, album, elapsed, duration, hasArt } */
void player_now_json(char *out, size_t out_len);

/* JSON library: { usb, updating, tracks:[...] } (rescans /media/usb, cached). */
void player_library_json(char *out, size_t out_len);

int player_transport(const char *cmd); /* play|pause|stop|next|prev */
int player_pause(void);                /* force pause (used before test tones) */
int player_seek(int seconds);
int player_play_index(int index);
int player_rescan(void);               /* drop the library cache */

/* Embedded album art for uri (an absolute /media/usb path). Returns a malloc'd
 * buffer (caller frees), sets *len and *mime; NULL if none. */
void *player_albumart(const char *uri, size_t *len, const char **mime);

#endif
