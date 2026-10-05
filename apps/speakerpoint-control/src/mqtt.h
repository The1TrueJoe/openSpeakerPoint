/* MQTT: the SpeakerPoint's control channel, on the box's own mosquitto.
 *
 * Everything you control or watch goes here; HTTP (main.c) keeps only what
 * MQTT carries badly - file upload, album-art bytes, the library listing - the
 * same split openHC makes (MQTT for live state and control, REST for bulk and
 * rarely-changing data).
 *
 * Same conventions as openHC: everything hangs off <prefix>/<hostname>
 * (prefix "openspeakerpoint" unless SPK_MQTT_PREFIX says otherwise):
 *
 *   <base>/status                  online|offline (retained; offline is the will)
 *   <base>/state/audio/output      off|rca|amp|both          (retained)
 *   <base>/state/audio/source      media|linein              (retained)
 *   <base>/state/audio/volume      0-100                     (retained)
 *   <base>/state/now               now-playing JSON          (retained)
 *   <base>/state/library           {usb, updating, rev}      (retained; re-fetch
 *                                  GET /api/library when rev changes)
 *   <base>/state/system/restore    {available, state}        (retained)
 *   <base>/cmd/audio/output        off|rca|amp|both
 *   <base>/cmd/audio/source        media|linein
 *   <base>/cmd/audio/volume        0-100
 *   <base>/cmd/transport           play|pause|stop|next|prev
 *   <base>/cmd/seek                seconds
 *   <base>/cmd/play                library index
 *   <base>/cmd/library/rescan      (any payload)
 *   <base>/cmd/library/delete      absolute /media/usb path
 *   <base>/cmd/tone                left|right|both|stop
 *   <base>/cmd/audio/measure       seconds (1-10, default 1) -> event/audio/measure
 *   <base>/cmd/system/restore      confirm  -> back to stock Control4 (one-way;
 *                                  osp-restore, the box reboots)
 *   <base>/event/audio/measure     line-in measurement JSON  (not retained)
 *   <base>/event/system/restore    {started}                 (not retained)
 *   <base>/error                   {topic, message}          (not retained)
 *
 * State is retained so a client that connects late is told the truth at once;
 * events are not, so a measurement is never replayed to the next subscriber.
 * Scalars go out bare, structured values as JSON.
 *
 * The client runs inside the daemon's own select() loop (no thread), so a
 * command is handled on the same thread as an HTTP request and the two can
 * never race on audio state. */
#ifndef SPKR_MQTT_H
#define SPKR_MQTT_H

#include <sys/select.h>

/* Create the client and try a first connect. Never fatal: without a broker the
 * daemon runs REST-only and keeps retrying. */
void mqtt_start(void);

/* select() integration: add the client's socket to the read (and, when it has
 * queued output, write) set; returns the new max fd. */
int mqtt_fdset(fd_set *rfds, fd_set *wfds, int maxfd);
/* Service the socket after select(). */
void mqtt_service(fd_set *rfds, fd_set *wfds);

/* ~1/sec: keepalive, reconnect, and publish any state that changed since the
 * last publish (whoever changed it: MQTT, REST or the media engine). */
void mqtt_tick(void);

/* Publish state that changed right now, rather than on the next tick. */
void mqtt_sync(void);

void mqtt_stop(void);

#endif
