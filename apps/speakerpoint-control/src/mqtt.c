#include "mqtt.h"
#include "audio.h"
#include "measure.h"
#include "player.h"

#include <ctype.h>
#include <mosquitto.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define KEEPALIVE 30
#define RETRY_SECONDS 5

static struct mosquitto *s_mosq;
static char s_base[160];
static char s_host[64] = "127.0.0.1";
static int s_port = 1883;
static int s_connected;
static time_t s_next_retry;
static time_t s_last_publish;

/* What was last published, so a tick only sends what changed. Cleared on
 * every (re)connect: the broker keeps retained state in RAM only. */
static struct {
    int valid;
    char output[8];
    char source[8];
    int volume;
    char now[2048];
    char library[128];
    char restore[160];
} s_last;
static time_t s_restore_checked;

#define RESTORE_BIN "/usr/sbin/osp-restore"
#define RESTORE_RECHECK 300

/* {available, state} from `osp-restore status` (reads only the payload's
 * manifest). Rechecked every few minutes: it changes only on an install. */
static void restore_state_json(char *out, size_t len)
{
    char line[160] = "";
    FILE *p = popen(RESTORE_BIN " status 2>/dev/null | grep '^state: '", "r");
    if (p) {
        if (!fgets(line, sizeof(line), p)) line[0] = '\0';
        pclose(p);
    }
    line[strcspn(line, "\n")] = '\0';
    const char *st = !strncmp(line, "state: ", 7) ? line + 7 : "none";
    int ready = !strncmp(st, "ready", 5);
    snprintf(out, len, "{\"available\":%s,\"state\":\"%s\"}", ready ? "true" : "false", st);
}

static const char *env_or(const char *k, const char *d)
{
    const char *v = getenv(k);
    return v && *v ? v : d;
}

static void publish(const char *sub, const char *payload, int retain)
{
    char topic[256];
    snprintf(topic, sizeof(topic), "%s/%s", s_base, sub);
    mosquitto_publish(s_mosq, NULL, topic, (int)strlen(payload), payload, 1, retain);
}

static void publish_error(const char *topic, const char *msg)
{
    char body[512];
    snprintf(body, sizeof(body), "{\"topic\":\"%s\",\"message\":\"%s\"}", topic, msg);
    publish("error", body, 0);
}

static void publish_changed(void)
{
    if (!s_connected) return;
    s_last_publish = time(NULL);

    const char *out = audio_output();
    if (!s_last.valid || strcmp(out, s_last.output)) {
        publish("state/audio/output", out, 1);
        snprintf(s_last.output, sizeof(s_last.output), "%s", out);
    }
    const char *src = audio_source();
    if (!s_last.valid || strcmp(src, s_last.source)) {
        publish("state/audio/source", src, 1);
        snprintf(s_last.source, sizeof(s_last.source), "%s", src);
    }
    int vol = audio_volume();
    if (!s_last.valid || vol != s_last.volume) {
        char v[8];
        snprintf(v, sizeof(v), "%d", vol);
        publish("state/audio/volume", v, 1);
        s_last.volume = vol;
    }
    char now[sizeof(s_last.now)];
    now_playing_json(now, sizeof(now));
    if (!s_last.valid || strcmp(now, s_last.now)) {
        publish("state/now", now, 1);
        snprintf(s_last.now, sizeof(s_last.now), "%s", now);
    }
    char lib[sizeof(s_last.library)];
    player_library_state_json(lib, sizeof(lib));
    if (!s_last.valid || strcmp(lib, s_last.library)) {
        publish("state/library", lib, 1);
        snprintf(s_last.library, sizeof(s_last.library), "%s", lib);
    }
    if (!s_last.valid || time(NULL) - s_restore_checked >= RESTORE_RECHECK) {
        char rs[sizeof(s_last.restore)];
        restore_state_json(rs, sizeof(rs));
        s_restore_checked = time(NULL);
        if (!s_last.valid || strcmp(rs, s_last.restore)) {
            publish("state/system/restore", rs, 1);
            snprintf(s_last.restore, sizeof(s_last.restore), "%s", rs);
        }
    }
    s_last.valid = 1;
}

static void on_connect(struct mosquitto *m, void *ud, int rc)
{
    (void)ud;
    if (rc != 0) return;
    s_connected = 1;
    char sub[200];
    snprintf(sub, sizeof(sub), "%s/cmd/#", s_base);
    mosquitto_subscribe(m, NULL, sub, 1);
    publish("status", "online", 1);
    s_last.valid = 0;
    publish_changed();
    fprintf(stderr, "speakerpoint-control: mqtt connected to %s:%d under %s\n", s_host, s_port, s_base);
}

static void on_disconnect(struct mosquitto *m, void *ud, int rc)
{
    (void)m; (void)ud; (void)rc;
    s_connected = 0;
    s_next_retry = time(NULL) + RETRY_SECONDS;
}

/* Copy and trim a payload into a NUL-terminated buffer. */
static void payload_str(const struct mosquitto_message *msg, char *out, size_t len)
{
    size_t n = msg->payloadlen > 0 ? (size_t)msg->payloadlen : 0;
    if (n >= len) n = len - 1;
    memcpy(out, msg->payload ? msg->payload : "", n);
    out[n] = '\0';
    while (n && isspace((unsigned char)out[n - 1])) out[--n] = '\0';
    size_t i = 0;
    while (out[i] && isspace((unsigned char)out[i])) i++;
    if (i) memmove(out, out + i, n - i + 1);
}

static int is_int(const char *s)
{
    if (!*s) return 0;
    for (const char *p = s; *p; p++) {
        if (!isdigit((unsigned char)*p)) return 0;
    }
    return 1;
}

static void on_message(struct mosquitto *m, void *ud, const struct mosquitto_message *msg)
{
    (void)m; (void)ud;
    char prefix[200];
    snprintf(prefix, sizeof(prefix), "%s/cmd/", s_base);
    size_t plen = strlen(prefix);
    if (strncmp(msg->topic, prefix, plen)) return;
    const char *cmd = msg->topic + plen;
    char val[64];
    payload_str(msg, val, sizeof(val));

    if (!strcmp(cmd, "audio/output")) {
        if (!audio_valid_output(val)) { publish_error(msg->topic, "expected off|rca|amp|both"); return; }
        audio_set_output(val);
    } else if (!strcmp(cmd, "audio/source")) {
        if (!audio_valid_source(val)) { publish_error(msg->topic, "expected media|linein"); return; }
        audio_set_source(val);
    } else if (!strcmp(cmd, "audio/volume")) {
        if (!is_int(val) || atoi(val) > 100) { publish_error(msg->topic, "expected 0-100"); return; }
        audio_set_volume(atoi(val));
    } else if (!strcmp(cmd, "transport")) {
        if (player_transport(val)) { publish_error(msg->topic, "expected play|pause|stop|next|prev"); return; }
    } else if (!strcmp(cmd, "seek")) {
        if (!is_int(val)) { publish_error(msg->topic, "expected seconds"); return; }
        player_seek(atoi(val));
    } else if (!strcmp(cmd, "play")) {
        if (!is_int(val) || player_play_index(atoi(val))) { publish_error(msg->topic, "expected a library index"); return; }
    } else if (!strcmp(cmd, "library/rescan")) {
        player_rescan();
    } else if (!strcmp(cmd, "library/delete")) {
        char path[512];
        payload_str(msg, path, sizeof(path));
        if (player_delete_track(path)) { publish_error(msg->topic, "delete failed"); return; }
    } else if (!strcmp(cmd, "system/restore")) {
        /* Acted on only for the literal "confirm": it wipes openSpeakerPoint.
         * osp-restore hands itself to a RAM phase and resets the box, so it
         * is started detached and this daemon is stopped along the way. */
        if (strcmp(val, "confirm")) { publish_error(msg->topic, "send \"confirm\" to return to stock"); return; }
        publish("event/system/restore", "{\"started\":true}", 0);
        (void)system("setsid " RESTORE_BIN " stock --yes </dev/null >/dev/null 2>&1 &");
        return;
    } else if (!strcmp(cmd, "tone")) {
        if (!strcmp(val, "stop")) {
            audio_tone_stop();
        } else if (!strcmp(val, "left") || !strcmp(val, "right") || !strcmp(val, "both") || !*val) {
            audio_tone(*val ? val : "both");
        } else {
            publish_error(msg->topic, "expected left|right|both|stop");
            return;
        }
    } else if (!strcmp(cmd, "audio/measure")) {
        int secs = 1;
        if (*val) {
            if (!is_int(val)) { publish_error(msg->topic, "expected seconds (1-10)"); return; }
            secs = atoi(val);
        }
        char body[512];
        measure_linein(secs, body, sizeof(body));
        publish("event/audio/measure", body, 0);
    } else {
        publish_error(msg->topic, "unrecognised command");
        return;
    }
    publish_changed();
}

/* A blocking connect, but to the broker on this box: it either answers at
 * once or refuses at once. Failure just schedules a retry. */
static void try_connect(void)
{
    if (mosquitto_connect(s_mosq, s_host, s_port, KEEPALIVE) != MOSQ_ERR_SUCCESS) {
        s_next_retry = time(NULL) + RETRY_SECONDS;
    }
}

void mqtt_start(void)
{
    char host[64] = "speakerpoint";
    gethostname(host, sizeof(host) - 1);
    snprintf(s_base, sizeof(s_base), "%s/%s", env_or("SPK_MQTT_PREFIX", "openspeakerpoint"), host);
    snprintf(s_host, sizeof(s_host), "%s", env_or("SPK_MQTT_HOST", "127.0.0.1"));
    s_port = atoi(env_or("SPK_MQTT_PORT", "1883"));

    mosquitto_lib_init();
    char id[96];
    snprintf(id, sizeof(id), "%s-control", host);
    s_mosq = mosquitto_new(id, true, NULL);
    if (!s_mosq) {
        fprintf(stderr, "speakerpoint-control: mqtt disabled (mosquitto_new failed)\n");
        return;
    }
    /* Retained will: if the daemon dies, subscribers are told rather than
     * trusting state that stopped being updated. */
    char will[200];
    snprintf(will, sizeof(will), "%s/status", s_base);
    mosquitto_will_set(s_mosq, will, 7, "offline", 1, true);
    mosquitto_connect_callback_set(s_mosq, on_connect);
    mosquitto_disconnect_callback_set(s_mosq, on_disconnect);
    mosquitto_message_callback_set(s_mosq, on_message);
    try_connect();
}

int mqtt_fdset(fd_set *rfds, fd_set *wfds, int maxfd)
{
    if (!s_mosq) return maxfd;
    int sock = mosquitto_socket(s_mosq);
    if (sock < 0) return maxfd;
    FD_SET(sock, rfds);
    if (mosquitto_want_write(s_mosq)) FD_SET(sock, wfds);
    return sock > maxfd ? sock : maxfd;
}

void mqtt_service(fd_set *rfds, fd_set *wfds)
{
    if (!s_mosq) return;
    int sock = mosquitto_socket(s_mosq);
    if (sock < 0) return;
    int rc = MOSQ_ERR_SUCCESS;
    if (FD_ISSET(sock, rfds)) rc = mosquitto_loop_read(s_mosq, 1);
    if (rc == MOSQ_ERR_SUCCESS && FD_ISSET(sock, wfds)) rc = mosquitto_loop_write(s_mosq, 1);
    if (rc == MOSQ_ERR_CONN_LOST || rc == MOSQ_ERR_NO_CONN) {
        s_connected = 0;
        s_next_retry = time(NULL) + RETRY_SECONDS;
    }
}

void mqtt_tick(void)
{
    if (!s_mosq) return;
    time_t now = time(NULL);
    if (mosquitto_socket(s_mosq) < 0) {
        if (now >= s_next_retry) try_connect();
        return;
    }
    mosquitto_loop_misc(s_mosq);   /* keepalive pings */
    /* Once a second at most: now-playing asks MPD, which is not free. */
    if (now != s_last_publish) publish_changed();
}

void mqtt_sync(void)
{
    if (s_mosq) publish_changed();
}

void mqtt_stop(void)
{
    if (!s_mosq) return;
    if (s_connected) {
        /* A clean disconnect does not fire the will, so say it ourselves. */
        publish("status", "offline", 1);
        for (int i = 0; i < 10 && mosquitto_want_write(s_mosq); i++) {
            mosquitto_loop_write(s_mosq, 1);
        }
        mosquitto_disconnect(s_mosq);
    }
    mosquitto_destroy(s_mosq);
    mosquitto_lib_cleanup();
    s_mosq = NULL;
}
