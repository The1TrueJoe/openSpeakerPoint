/*
 * SDDP announcer for the SpeakerPoint - Control4's discovery protocol, so
 * Composer finds the box automatically and pairs it with the driver (no manual
 * device add, no typing an IP).
 *
 * SDDP is HTTP-like text over UDP multicast 239.255.255.250:1902. We mirror the
 * openHC controller's c4sddp (which uses the Rust `sddp` crate; that crate can't
 * cross-compile to this box's ARMv4T, so the wire format is reimplemented here):
 *   - multicast NOTIFY ALIVE on start and every SDDP_INTERVAL seconds,
 *   - answer each matching SEARCH with a unicast SDDP/1.0 200 OK,
 *   - multicast NOTIFY OFFLINE on shutdown,
 *   - re-announce from the new address when the box's IP changes.
 *
 * The Type (openhc:speakerpoint) must match the driver's <search_types>.
 * Host-side self-check: cc -D_GNU_SOURCE -DSDDP_SELFTEST -o /tmp/t sddp.c && /tmp/t
 */
#include "sddp.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define SDDP_GROUP    "239.255.255.250"
#define SDDP_PORT     1902
#define SDDP_TYPE     "openhc:speakerpoint"
#define SDDP_MAXAGE   1800   /* seconds listeners remember us without hearing again */
#define SDDP_INTERVAL 300    /* re-announce period (well under Max-Age) */
#define SDDP_IPCHECK  30     /* how often to re-check our address */

static int s_sock = -1;
static struct in_addr s_ip;          /* current announce address (0 = none) */
static char s_host[80];
static time_t s_last_alive, s_last_ipcheck;

/* The address this box reaches the SDDP group from - the one Composer should
 * use. A connected UDP socket's local address is that source address. */
static int detect_ip(struct in_addr *out)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in g;
    memset(&g, 0, sizeof g);
    g.sin_family = AF_INET;
    g.sin_port = htons(SDDP_PORT);
    inet_pton(AF_INET, SDDP_GROUP, &g.sin_addr);
    int rc = -1;
    if (connect(fd, (struct sockaddr *)&g, sizeof g) == 0) {
        struct sockaddr_in me;
        socklen_t ml = sizeof me;
        if (getsockname(fd, (struct sockaddr *)&me, &ml) == 0 && me.sin_addr.s_addr != 0) {
            *out = me.sin_addr;
            rc = 0;
        }
    }
    close(fd);
    return rc;
}

/* Header block after the start line. tran != NULL only on a SEARCH reply, where
 * it is echoed verbatim after Host. Every string value is quoted; Max-Age and
 * the echoed Tran are not. Header order matches the sddp crate's. */
static int fmt_headers(char *buf, size_t cap, struct in_addr ip, const char *tran)
{
    char ips[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &ip, ips, sizeof ips);
    int n = 0;
    n += snprintf(buf + n, cap - n, "From: \"%s:%d\"\r\n", ips, SDDP_PORT);
    n += snprintf(buf + n, cap - n, "Host: \"%s\"\r\n", s_host);
    if (tran && *tran) n += snprintf(buf + n, cap - n, "Tran: %s\r\n", tran);
    n += snprintf(buf + n, cap - n, "Max-Age: %d\r\n", SDDP_MAXAGE);
    n += snprintf(buf + n, cap - n, "Type: \"%s\"\r\n", SDDP_TYPE);
    n += snprintf(buf + n, cap - n, "Primary-Proxy: \"avswitch\"\r\n");
    n += snprintf(buf + n, cap - n, "Proxies: \"avswitch\"\r\n");
    n += snprintf(buf + n, cap - n, "Manufacturer: \"openHC\"\r\n");
    n += snprintf(buf + n, cap - n, "Model: \"SpeakerPoint\"\r\n");
    n += snprintf(buf + n, cap - n, "Driver: \"openspeakerpoint.c4z\"\r\n");
    n += snprintf(buf + n, cap - n, "Config-URL: \"http://%s:8081/\"\r\n", ips);
    n += snprintf(buf + n, cap - n, "\r\n");
    return n;
}

static int fmt_notify(char *buf, size_t cap, const char *kind)
{
    int n = snprintf(buf, cap, "NOTIFY %s SDDP/1.0\r\n", kind);
    return n + fmt_headers(buf + n, cap - n, s_ip, NULL);
}

static int fmt_response(char *buf, size_t cap, const char *tran)
{
    int n = snprintf(buf, cap, "SDDP/1.0 200 OK\r\n");
    return n + fmt_headers(buf + n, cap - n, s_ip, tran);
}

static void group_addr(struct sockaddr_in *g)
{
    memset(g, 0, sizeof *g);
    g->sin_family = AF_INET;
    g->sin_port = htons(SDDP_PORT);
    inet_pton(AF_INET, SDDP_GROUP, &g->sin_addr);
}

static void send_notify(const char *kind)
{
    if (s_sock < 0) return;
    char msg[768];
    int n = fmt_notify(msg, sizeof msg, kind);
    struct sockaddr_in g;
    group_addr(&g);
    sendto(s_sock, msg, (size_t)n, 0, (struct sockaddr *)&g, sizeof g);
}

static int open_socket(struct in_addr ip)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -1;
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
#ifdef SO_REUSEPORT
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof one);
#endif
    /* Bind the wildcard address, not ip: a socket bound to a unicast address
     * never receives multicast on Linux. */
    struct sockaddr_in b;
    memset(&b, 0, sizeof b);
    b.sin_family = AF_INET;
    b.sin_port = htons(SDDP_PORT);
    b.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, (struct sockaddr *)&b, sizeof b) < 0) {
        close(fd);
        return -1;
    }
    struct ip_mreq mr;
    memset(&mr, 0, sizeof mr);
    inet_pton(AF_INET, SDDP_GROUP, &mr.imr_multiaddr);
    mr.imr_interface = ip;
    setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mr, sizeof mr);
    setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF, &ip, sizeof ip);
    return fd;
}

static void bring_up(struct in_addr ip)
{
    if (s_sock >= 0) close(s_sock);
    s_ip = ip;
    s_sock = open_socket(ip);
    if (s_sock >= 0) {
        send_notify("ALIVE");
        s_last_alive = time(NULL);
    }
}

void sddp_start(void)
{
    if (gethostname(s_host, sizeof s_host) != 0 || !s_host[0]) {
        strcpy(s_host, "speakerpoint");
    }
    s_host[sizeof s_host - 1] = '\0';
    s_ip.s_addr = 0;
    struct in_addr ip;
    if (detect_ip(&ip) == 0) bring_up(ip);
    s_last_ipcheck = time(NULL);
}

int sddp_fdset(fd_set *rfds, int maxfd)
{
    if (s_sock < 0) return maxfd;
    FD_SET(s_sock, rfds);
    return s_sock > maxfd ? s_sock : maxfd;
}

void sddp_tick(void)
{
    time_t now = time(NULL);
    if (now - s_last_ipcheck >= SDDP_IPCHECK) {
        s_last_ipcheck = now;
        struct in_addr ip;
        int have = detect_ip(&ip) == 0;
        if (have && ip.s_addr != s_ip.s_addr) {
            if (s_sock >= 0) send_notify("OFFLINE");  /* leaving the old address */
            bring_up(ip);
        } else if (!have && s_sock >= 0) {
            send_notify("OFFLINE");
            close(s_sock);
            s_sock = -1;
            s_ip.s_addr = 0;
        }
    }
    if (s_sock >= 0 && now - s_last_alive >= SDDP_INTERVAL) {
        send_notify("ALIVE");
        s_last_alive = now;
    }
}

/* target matches if it is "*" or exactly our Type (the crate's rule). */
static int target_matches(const char *target)
{
    while (*target == ' ') target++;
    return !strcmp(target, "*") || !strcmp(target, SDDP_TYPE);
}

void sddp_service(fd_set *rfds)
{
    if (s_sock < 0 || !FD_ISSET(s_sock, rfds)) return;
    char buf[1500];
    struct sockaddr_in from;
    socklen_t fl = sizeof from;
    ssize_t n = recvfrom(s_sock, buf, sizeof buf - 1, 0, (struct sockaddr *)&from, &fl);
    if (n <= 0) return;
    buf[n] = '\0';

    /* Only SEARCH is answered. First line: "SEARCH <target> SDDP/1.0". */
    char first[320];
    size_t flen = 0;
    for (ssize_t i = 0; i < n && buf[i] != '\r' && buf[i] != '\n' && flen < sizeof first - 1; i++) {
        first[flen++] = buf[i];
    }
    first[flen] = '\0';
    if (strncasecmp(first, "SEARCH ", 7)) return;
    char *vtag = strcasestr(first + 7, " SDDP/");
    if (!vtag) return;
    *vtag = '\0';
    char *target = first + 7;
    char *end = target + strlen(target);
    while (end > target && end[-1] == ' ') *--end = '\0';
    if (!target_matches(target)) return;

    /* Echo the searcher's Tran (verbatim, so quoting is preserved). */
    char tran[128] = "";
    char *t = strcasestr(buf, "\r\ntran:");
    if (t) {
        t += 7;
        while (*t == ' ') t++;
        size_t k = 0;
        while (*t && *t != '\r' && *t != '\n' && k < sizeof tran - 1) tran[k++] = *t++;
        tran[k] = '\0';
    }

    char msg[768];
    int m = fmt_response(msg, sizeof msg, tran[0] ? tran : NULL);
    sendto(s_sock, msg, (size_t)m, 0, (struct sockaddr *)&from, fl);
}

void sddp_stop(void)
{
    if (s_sock >= 0) {
        send_notify("OFFLINE");
        close(s_sock);
        s_sock = -1;
    }
}

#ifdef SDDP_SELFTEST
#include <assert.h>
int main(void)
{
    strcpy(s_host, "spk-test");
    inet_pton(AF_INET, "10.0.0.5", &s_ip);

    char buf[768];
    fmt_notify(buf, sizeof buf, "ALIVE");
    const char *want_alive =
        "NOTIFY ALIVE SDDP/1.0\r\n"
        "From: \"10.0.0.5:1902\"\r\n"
        "Host: \"spk-test\"\r\n"
        "Max-Age: 1800\r\n"
        "Type: \"openhc:speakerpoint\"\r\n"
        "Primary-Proxy: \"avswitch\"\r\n"
        "Proxies: \"avswitch\"\r\n"
        "Manufacturer: \"openHC\"\r\n"
        "Model: \"SpeakerPoint\"\r\n"
        "Driver: \"openspeakerpoint.c4z\"\r\n"
        "Config-URL: \"http://10.0.0.5:8081/\"\r\n"
        "\r\n";
    assert(!strcmp(buf, want_alive));

    fmt_response(buf, sizeof buf, "\"17\"");
    assert(strstr(buf, "SDDP/1.0 200 OK\r\n") == buf);
    assert(strstr(buf, "Host: \"spk-test\"\r\nTran: \"17\"\r\nMax-Age: 1800\r\n"));
    assert(strstr(buf, "Type: \"openhc:speakerpoint\"\r\n"));

    assert(target_matches("*"));
    assert(target_matches("openhc:speakerpoint"));
    assert(target_matches("  openhc:speakerpoint"));
    assert(!target_matches("openhc:device"));
    assert(!target_matches("acme:widget"));

    printf("sddp selfcheck OK\n");
    return 0;
}
#endif
