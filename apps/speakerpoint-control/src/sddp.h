#ifndef SDDP_H
#define SDDP_H

#include <sys/select.h>

/* SDDP (Control4 discovery) announcer. Multicasts NOTIFY ALIVE on udp/1902 so
 * Composer lists the box under discovered devices and pairs it with the driver
 * whose <search_types> carry the same Type (openhc:speakerpoint); answers
 * SEARCH; says NOTIFY OFFLINE on exit. Hooks into the main select() loop. */
void sddp_start(void);                 /* open socket, first ALIVE */
int  sddp_fdset(fd_set *rfds, int maxfd);
void sddp_tick(void);                  /* re-announce on interval; follow IP changes */
void sddp_service(fd_set *rfds);       /* answer a SEARCH */
void sddp_stop(void);                  /* NOTIFY OFFLINE */

#endif
