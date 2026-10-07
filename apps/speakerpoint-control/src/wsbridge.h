/* MQTT over websockets for the dashboard, without a websocket-capable broker.
 *
 * mosquitto 2.1's websockets need OpenSSL (for the handshake's SHA-1), and
 * OpenSSL does not fit osp-rootfs. So the browser's websocket ends here
 * instead: GET /mqtt with Upgrade: websocket is answered on this daemon's own
 * HTTP port, and each websocket is relayed byte for byte to the local broker
 * on 127.0.0.1:1883 - MQTT-over-websockets is plain MQTT inside binary frames,
 * so nothing here parses MQTT. */
#ifndef SPKR_WSBRIDGE_H
#define SPKR_WSBRIDGE_H

#include <sys/select.h>

#define WS_PATH "/mqtt"

/* Is this request (raw header block) a websocket upgrade? */
int ws_is_upgrade(const char *raw);

/* Complete the handshake on fd and open its broker connection. Takes fd over
 * on success (returns 0); on failure the caller still owns and closes it. */
int ws_accept(int fd, const char *raw);

int ws_fdset(fd_set *rfds, int maxfd);
void ws_service(fd_set *rfds);
void ws_close_all(void);
/* In a forked child: drop the inherited sockets without ending the sessions,
 * which stay the parent's. */
void ws_forget(void);

#endif
