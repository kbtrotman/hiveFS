/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

/**
 * HiveFS - hive_guard_leasing
 *
 * Threaded JSON-over-length-prefixed leasing listener.
 *
 * Goal: make it easy to SEE lease ops and keep you honest with op types.
 * Later: swap JSON framing for a tiny libuv protocol without changing the
 * higher-level op dispatch and lease state model.
 */

#include "hive_guard_leasing.h"
#include "hive_guard_raft.h"
#include "hive_guard.h"

#include <netdb.h>

extern const struct hive_storage_node *hifs_get_storage_nodes(size_t *count);

static int get_env_int(const char *k, int def);
static const char *get_env_str(const char *k, const char *def);


/* -------------------------------------------------------------------------- */
/* Logging                                                                    */
/* -------------------------------------------------------------------------- */

static void lease_log(const char *lvl, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[leasing][%s] ", lvl);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

/* -------------------------------------------------------------------------- */
/* Minimal JSON parser (object + string/number/bool/null)                      */
/* Adapted from the style in hive_guard_tcp.c to keep dependencies at zero.   */
/* -------------------------------------------------------------------------- */

typedef struct JVal JVal;
typedef struct JField JField;

struct JVal {
    enum { JT_NULL, JT_BOOL, JT_NUM, JT_STR, JT_OBJ } t;
    double num;
    int boolean;
    char *str;
    JField *obj;
    size_t obj_len;
};

struct JField {
    char *k;
    JVal v;
};

static void jfree(JVal *v);

static const char *jstr(const JVal *v) { return (v && v->t == JT_STR) ? v->str : NULL; }
static long jnum_i(const JVal *v, long def) { return (v && v->t == JT_NUM) ? (long)v->num : def; }
static bool jbool(const JVal *v, bool def) {
    if (!v) return def;
    if (v->t == JT_BOOL) return v->boolean != 0;
    if (v->t == JT_NUM) return v->num != 0;
    return def;
}

static const JVal *jobj_get(const JVal *o, const char *k)
{
    if (!o || o->t != JT_OBJ || !k) return NULL;
    for (size_t i = 0; i < o->obj_len; i++) {
        if (o->obj[i].k && strcmp(o->obj[i].k, k) == 0) {
            return &o->obj[i].v;
        }
    }
    return NULL;
}

static const char *skip_ws(const char *s)
{
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    return s;
}

static char *parse_json_string(const char **ps)
{
    const char *s = skip_ws(*ps);
    if (*s != '"') return NULL;
    s++;

    /* Simple string parser: supports \" and \\ and \n \t \r. */
    size_t cap = 64, len = 0;
    char *out = (char *)malloc(cap);
    if (!out) return NULL;

    while (*s && *s != '"') {
        char c = *s++;
        if (c == '\\') {
            char e = *s++;
            switch (e) {
                case '"': c = '"'; break;
                case '\\': c = '\\'; break;
                case 'n': c = '\n'; break;
                case 't': c = '\t'; break;
                case 'r': c = '\r'; break;
                default: c = e; break;
            }
        }
        if (len + 1 >= cap) {
            cap *= 2;
            char *tmp = (char *)realloc(out, cap);
            if (!tmp) { free(out); return NULL; }
            out = tmp;
        }
        out[len++] = c;
    }

    if (*s != '"') { free(out); return NULL; }
    s++; /* closing quote */
    out[len] = '\0';
    *ps = s;
    return out;
}

static bool parse_json_number(const char **ps, double *out)
{
    const char *s = skip_ws(*ps);
    char *end = NULL;
    errno = 0;
    double v = strtod(s, &end);
    if (end == s || errno != 0) return false;
    *out = v;
    *ps = end;
    return true;
}

static bool match_kw(const char **ps, const char *kw)
{
    const char *s = skip_ws(*ps);
    size_t n = strlen(kw);
    if (strncmp(s, kw, n) != 0) return false;
    *ps = s + n;
    return true;
}

static bool parse_val(const char **ps, JVal *out);

static bool parse_obj(const char **ps, JVal *out)
{
    const char *s = skip_ws(*ps);
    if (*s != '{') return false;
    s++;
    s = skip_ws(s);

    out->t = JT_OBJ;
    out->obj = NULL;
    out->obj_len = 0;

    if (*s == '}') { *ps = s + 1; return true; }

    for (;;) {
        JField f;
        memset(&f, 0, sizeof(f));

        f.k = parse_json_string(&s);
        if (!f.k) return false;

        s = skip_ws(s);
        if (*s != ':') { free(f.k); return false; }
        s++;

        if (!parse_val(&s, &f.v)) { free(f.k); return false; }

        JField *tmp = (JField *)realloc(out->obj, (out->obj_len + 1) * sizeof(JField));
        if (!tmp) { free(f.k); jfree(&f.v); return false; }
        out->obj = tmp;
        out->obj[out->obj_len++] = f;

        s = skip_ws(s);
        if (*s == ',') { s++; continue; }
        if (*s == '}') { s++; break; }
        return false;
    }

    *ps = s;
    return true;
}

static bool parse_val(const char **ps, JVal *out)
{
    const char *s = skip_ws(*ps);

    if (*s == '{') {
        return parse_obj(ps, out);
    }
    if (*s == '"') {
        out->t = JT_STR;
        out->str = parse_json_string(ps);
        return out->str != NULL;
    }
    if (*s == '-' || (*s >= '0' && *s <= '9')) {
        out->t = JT_NUM;
        return parse_json_number(ps, &out->num);
    }
    if (match_kw(&s, "true")) { out->t = JT_BOOL; out->boolean = 1; *ps = s; return true; }
    if (match_kw(&s, "false")){ out->t = JT_BOOL; out->boolean = 0; *ps = s; return true; }
    if (match_kw(&s, "null")) { out->t = JT_NULL; *ps = s; return true; }

    return false;
}

static bool jparse(const char *json, JVal *out)
{
    memset(out, 0, sizeof(*out));
    const char *s = json;
    if (!parse_val(&s, out)) return false;
    s = skip_ws(s);
    return *s == '\0';
}

static bool parse_host_from_addr(const char *addr,
                                 char *host,
                                 size_t host_sz,
                                 int *port_out)
{
    if (!addr || !host || host_sz == 0) return false;
    host[0] = '\0';
    const char *colon = strrchr(addr, ':');
    int port = 0;
    if (colon) {
        size_t len = (size_t)(colon - addr);
        if (len >= host_sz) len = host_sz - 1;
        memcpy(host, addr, len);
        host[len] = '\0';
        port = atoi(colon + 1);
    } else {
        snprintf(host, host_sz, "%s", addr);
    }
    if (port_out) *port_out = port;
    return host[0] != '\0';
}

static void jfree(JVal *v)
{
    if (!v) return;
    if (v->t == JT_STR) {
        free(v->str);
        v->str = NULL;
    } else if (v->t == JT_OBJ) {
        for (size_t i = 0; i < v->obj_len; i++) {
            free(v->obj[i].k);
            jfree(&v->obj[i].v);
        }
        free(v->obj);
        v->obj = NULL;
        v->obj_len = 0;
    }
}

/* -------------------------------------------------------------------------- */
/* Length-prefixed framing                                                     */
/* -------------------------------------------------------------------------- */

static int read_full(int fd, void *buf, size_t n)
{
    uint8_t *p = (uint8_t *)buf;
    size_t got = 0;
    while (got < n) {
        ssize_t r = read(fd, p + got, n - got);
        if (r == 0) return 0; /* EOF */
        if (r < 0) {
            if (errno == EINTR) continue;
            return -errno;
        }
        got += (size_t)r;
    }
    return (int)got;
}

static int write_full(int fd, const void *buf, size_t n)
{
    const uint8_t *p = (const uint8_t *)buf;
    size_t sent = 0;
    while (sent < n) {
        ssize_t w = write(fd, p + sent, n - sent);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -errno;
        }
        sent += (size_t)w;
    }
    return (int)sent;
}

static int send_frame_json(int fd, const char *json)
{
    uint32_t len = (uint32_t)strlen(json);
    uint32_t be = htonl(len);
    int rc = write_full(fd, &be, sizeof(be));
    if (rc < 0) return rc;
    rc = write_full(fd, json, len);
    return (rc < 0) ? rc : 0;
}

static const struct hive_storage_node *lease_find_owner_node(uint32_t node_id)
{
    if (node_id == 0) return NULL;
    size_t count = 0;
    const struct hive_storage_node *nodes = hifs_get_storage_nodes(&count);
    if (!nodes) return NULL;
    for (size_t i = 0; i < count; ++i) {
        if (nodes[i].id == node_id) return &nodes[i];
    }
    return NULL;
}

static void lease_resolve_host_port(const struct hive_storage_node *node,
                                    char *host,
                                    size_t host_sz,
                                    int *port_out)
{
    if (!host || host_sz == 0) return;
    host[0] = '\0';
    int port = get_env_int("HIVE_LEASE_LISTEN_PORT", 7400);

    if (node && node->address[0]) {
        const char *addr = node->address;
        const char *colon = strrchr(addr, ':');
        if (colon) {
            size_t len = (size_t)(colon - addr);
            if (len >= host_sz) len = host_sz - 1;
            memcpy(host, addr, len);
            host[len] = '\0';
            int parsed = atoi(colon + 1);
            if (parsed > 0 && parsed < 65536) port = parsed;
        } else {
            snprintf(host, host_sz, "%s", addr);
        }
    }

    if (host[0] == '\0') {
        snprintf(host, host_sz, "%s", get_env_str("HIVE_LEASE_FORWARD_FALLBACK_HOST", "127.0.0.1"));
    }

    if (node && port == 0 && node->guard_port) {
        port = node->guard_port;
    }

    if (port_out) *port_out = port;
}

static int lease_connect_peer(const char *host, int port)
{
    if (!host || !*host || port <= 0) return -EINVAL;

    char portbuf[16];
    snprintf(portbuf, sizeof(portbuf), "%d", port);

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *res = NULL;
    int rc = getaddrinfo(host, portbuf, &hints, &res);
    if (rc != 0) {
        lease_log("WARN", "getaddrinfo host=%s port=%d rc=%d", host, port, rc);
        return -EHOSTUNREACH;
    }

    int sock = -1;
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (sock < 0) continue;
        if (connect(sock, ai->ai_addr, ai->ai_addrlen) == 0) break;
        close(sock);
        sock = -1;
    }
    freeaddrinfo(res);
    if (sock < 0) return -EHOSTUNREACH;

    struct timeval tv;
    tv.tv_sec = HG_LEASE_FORWARD_TIMEOUT_MS / 1000;
    tv.tv_usec = (HG_LEASE_FORWARD_TIMEOUT_MS % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    return sock;
}

static int forward_lease_renew_remote(int client_fd,
                                      const char *key,
                                      uint64_t token,
                                      uint64_t ttl_ms,
                                      uint32_t owner_node_id,
                                      uint32_t self_node_id)
{
    const struct hive_storage_node *node = lease_find_owner_node(owner_node_id);
    if (!node) {
        char out[512];
        snprintf(out, sizeof(out),
                 "{\"ok\":false,\"op\":\"LEASE_RENEW\",\"key\":\"%s\","
                 "\"error\":\"owner_unknown\",\"owner\":%u}",
                 key, (unsigned)owner_node_id);
        return send_frame_json(client_fd, out);
    }

    char host[128];
    int port = 0;
    lease_resolve_host_port(node, host, sizeof(host), &port);
    if (port <= 0) {
        char out[512];
        snprintf(out, sizeof(out),
                 "{\"ok\":false,\"op\":\"LEASE_RENEW\",\"key\":\"%s\","
                 "\"error\":\"owner_addr_invalid\",\"owner\":%u}",
                 key, (unsigned)owner_node_id);
        return send_frame_json(client_fd, out);
    }

    int sock = lease_connect_peer(host, port);
    if (sock < 0) {
        lease_log("WARN", "forward connect owner=%u host=%s port=%d failed",
                  (unsigned)owner_node_id, host, port);
        char out[512];
        snprintf(out, sizeof(out),
                 "{\"ok\":false,\"op\":\"LEASE_RENEW\",\"key\":\"%s\","
                 "\"error\":\"owner_unreachable\",\"owner\":%u}",
                 key, (unsigned)owner_node_id);
        return send_frame_json(client_fd, out);
    }

    char req[640];
    snprintf(req, sizeof(req),
             "{\"op\":\"LEASE_RENEW\",\"key\":\"%s\",\"token\":%llu,"
             "\"ttl_ms\":%llu,\"forwarded\":true,\"forwarded_by\":%u}",
             key,
             (unsigned long long)token,
             (unsigned long long)ttl_ms,
             (unsigned)self_node_id);

    int rc = send_frame_json(sock, req);
    if (rc != 0) {
        lease_log("WARN", "forward send to owner=%u failed rc=%d", (unsigned)owner_node_id, rc);
        close(sock);
        char out[512];
        snprintf(out, sizeof(out),
                 "{\"ok\":false,\"op\":\"LEASE_RENEW\",\"key\":\"%s\","
                 "\"error\":\"owner_send_failed\",\"owner\":%u}",
                 key, (unsigned)owner_node_id);
        return send_frame_json(client_fd, out);
    }

    uint32_t be_len = 0;
    rc = read_full(sock, &be_len, sizeof(be_len));
    if (rc <= 0) {
        lease_log("WARN", "forward recv len owner=%u rc=%d", (unsigned)owner_node_id, rc);
        close(sock);
        char out[512];
        snprintf(out, sizeof(out),
                 "{\"ok\":false,\"op\":\"LEASE_RENEW\",\"key\":\"%s\","
                 "\"error\":\"owner_recv_failed\",\"owner\":%u}",
                 key, (unsigned)owner_node_id);
        return send_frame_json(client_fd, out);
    }

    uint32_t len = ntohl(be_len);
    if (len == 0 || len > HG_LEASE_FORWARD_RESP_MAX) {
        lease_log("WARN", "forward resp len invalid=%u owner=%u", len, (unsigned)owner_node_id);
        close(sock);
        char out[512];
        snprintf(out, sizeof(out),
                 "{\"ok\":false,\"op\":\"LEASE_RENEW\",\"key\":\"%s\","
                 "\"error\":\"owner_resp_invalid\",\"owner\":%u}",
                 key, (unsigned)owner_node_id);
        return send_frame_json(client_fd, out);
    }

    char *resp = (char *)calloc(1, (size_t)len + 1);
    if (!resp) {
        close(sock);
        return send_frame_json(client_fd, "{\"ok\":false,\"error\":\"oom\"}");
    }
    rc = read_full(sock, resp, len);
    close(sock);
    if (rc <= 0) {
        free(resp);
        char out[512];
        snprintf(out, sizeof(out),
                 "{\"ok\":false,\"op\":\"LEASE_RENEW\",\"key\":\"%s\","
                 "\"error\":\"owner_resp_short\",\"owner\":%u}",
                 key, (unsigned)owner_node_id);
        return send_frame_json(client_fd, out);
    }
    resp[len] = '\0';

    rc = send_frame_json(client_fd, resp);
    free(resp);
    return rc;
}

static int send_forwarded_error(int fd, const char *op, const char *key, const char *error)
{
    char out[512];
    snprintf(out, sizeof(out),
             "{\"ok\":false,\"op\":\"%s\",\"key\":\"%s\",\"error\":\"%s\"}",
             op ? op : "", key ? key : "", error ? error : "forward_failed");
    return send_frame_json(fd, out);
}

static int forward_to_leader_json(int client_fd,
                                  const char *leader,
                                  const char *json,
                                  size_t len,
                                  const char *op,
                                  const char *key)
{
    if (!leader) return send_forwarded_error(client_fd, op, key, "leader_unknown");

    char host[128];
    int port = 0;
    if (!parse_host_from_addr(leader, host, sizeof(host), &port))
        return send_forwarded_error(client_fd, op, key, "leader_addr_bad");
    if (port <= 0)
        port = get_env_int("HIVE_LEASE_LISTEN_PORT", 7400);

    int sock = lease_connect_peer(host, port);
    if (sock < 0)
        return send_forwarded_error(client_fd, op, key, "leader_unreachable");

    uint32_t be = htonl((uint32_t)len);
    int rc = write_full(sock, &be, sizeof(be));
    if (rc >= 0)
        rc = write_full(sock, json, len);
    if (rc < 0) {
        close(sock);
        return send_forwarded_error(client_fd, op, key, "leader_send_failed");
    }

    uint32_t resp_len_be = 0;
    rc = read_full(sock, &resp_len_be, sizeof(resp_len_be));
    if (rc <= 0) {
        close(sock);
        return send_forwarded_error(client_fd, op, key, "leader_recv_len");
    }
    uint32_t resp_len = ntohl(resp_len_be);
    if (resp_len == 0 || resp_len > HG_LEASE_FORWARD_RESP_MAX) {
        close(sock);
        return send_forwarded_error(client_fd, op, key, "leader_resp_invalid");
    }
    char *resp = (char *)calloc(1, (size_t)resp_len + 1);
    if (!resp) {
        close(sock);
        return send_frame_json(client_fd, "{\"ok\":false,\"error\":\"oom\"}");
    }
    rc = read_full(sock, resp, resp_len);
    close(sock);
    if (rc <= 0) {
        free(resp);
        return send_forwarded_error(client_fd, op, key, "leader_resp_short");
    }
    resp[resp_len] = '\0';
    rc = send_frame_json(client_fd, resp);
    free(resp);
    return rc;
}

/* -------------------------------------------------------------------------- */
/* In-memory lease table (debug/simple)                                       */
/* Later: swap for hash map + raft-applied backing.                           */
/* -------------------------------------------------------------------------- */

typedef struct hg_lease_entry {
    char key[192];
    hg_lease_mode_t mode;
    uint32_t owner_node_id;
    uint64_t token;
    uint64_t expires_at_ms; /* soft hint */
} hg_lease_entry_t;

#define HG_LEASE_MAX 4096

static pthread_mutex_t g_lease_mu = PTHREAD_MUTEX_INITIALIZER;
static hg_lease_entry_t g_leases[HG_LEASE_MAX];
static size_t g_lease_count = 0;

static hg_lease_entry_t *lease_find_locked(const char *key)
{
    for (size_t i = 0; i < g_lease_count; i++) {
        if (strcmp(g_leases[i].key, key) == 0) return &g_leases[i];
    }
    return NULL;
}

static hg_lease_entry_t *lease_upsert_locked(const char *key)
{
    hg_lease_entry_t *e = lease_find_locked(key);
    if (e) return e;
    if (g_lease_count >= HG_LEASE_MAX) return NULL;
    e = &g_leases[g_lease_count++];
    memset(e, 0, sizeof(*e));
    strncpy(e->key, key, sizeof(e->key) - 1);
    return e;
}

void hg_leasing_debug_dump(void)
{
    pthread_mutex_lock(&g_lease_mu);
    lease_log("INFO", "leases=%zu", g_lease_count);
    for (size_t i = 0; i < g_lease_count; i++) {
        const hg_lease_entry_t *e = &g_leases[i];
        lease_log("INFO", "  key=%s owner=%u token=%llu mode=%s exp=%llu",
                 e->key,
                 (unsigned)e->owner_node_id,
                 (unsigned long long)e->token,
                 (e->mode == HG_LEASE_EXCLUSIVE ? "X" : "S"),
                 (unsigned long long)e->expires_at_ms);
    }
    pthread_mutex_unlock(&g_lease_mu);
}

/* -------------------------------------------------------------------------- */
/* Server globals                                                              */
/* -------------------------------------------------------------------------- */

static pthread_t g_thr;
static int g_listen_fd = -1;
static volatile int g_stop = 0;

static hg_leasing_hooks_t g_hooks;

static int get_env_int(const char *k, int def)
{
    const char *v = getenv(k);
    return (v && *v) ? atoi(v) : def;
}

static const char *get_env_str(const char *k, const char *def)
{
    const char *v = getenv(k);
    return (v && *v) ? v : def;
}

/* -------------------------------------------------------------------------- */
/* Op parsing                                                                   */
/* -------------------------------------------------------------------------- */

static hg_lease_op_t parse_op(const char *op)
{
    if (!op) return 0;
    if (strcmp(op, "LEASE_ACQUIRE") == 0) return HG_LEASE_ACQUIRE;
    if (strcmp(op, "LEASE_LOOKUP")  == 0) return HG_LEASE_LOOKUP;
    if (strcmp(op, "LEASE_RENEW")   == 0) return HG_LEASE_RENEW;
    if (strcmp(op, "LEASE_RELEASE") == 0) return HG_LEASE_RELEASE;
    return 0;
}

static hg_lease_mode_t parse_mode(const char *m)
{
    if (!m) return 0;
    if (strcmp(m, "SHARED") == 0) return HG_LEASE_SHARED;
    if (strcmp(m, "EXCLUSIVE") == 0) return HG_LEASE_EXCLUSIVE;
    return 0;
}

/* -------------------------------------------------------------------------- */
/* Core handlers (debug logic now; raft/delegation later)                      */
/* -------------------------------------------------------------------------- */

static int handle_lease_lookup(int fd, const char *key)
{
    pthread_mutex_lock(&g_lease_mu);
    hg_lease_entry_t *e = lease_find_locked(key);
    uint64_t tnow = now_ms();

    char out[512];
    if (!e) {
        pthread_mutex_unlock(&g_lease_mu);
        snprintf(out, sizeof(out),
                 "{\"ok\":true,\"op\":\"LEASE_LOOKUP\",\"key\":\"%s\",\"found\":false}",
                 key);
        return send_frame_json(fd, out);
    }

    bool expired = (e->expires_at_ms > 0 && e->expires_at_ms <= tnow);
    snprintf(out, sizeof(out),
             "{\"ok\":true,\"op\":\"LEASE_LOOKUP\",\"key\":\"%s\",\"found\":true,"
             "\"owner\":%u,\"token\":%llu,\"mode\":\"%s\",\"expired\":%s}",
             key,
             (unsigned)e->owner_node_id,
             (unsigned long long)e->token,
             (e->mode == HG_LEASE_EXCLUSIVE ? "EXCLUSIVE" : "SHARED"),
             expired ? "true" : "false");
    pthread_mutex_unlock(&g_lease_mu);
    return send_frame_json(fd, out);
}

static int handle_lease_acquire(int fd,
                               const char *key,
                               hg_lease_mode_t mode,
                               uint64_t ttl_ms,
                               uint32_t prefer_owner)
{
    /* TODO: Debug behavior (no raft yet):
     * - If free/expired: grant locally with owner=prefer_owner (or 0) and token++.
     * - If held: return current owner/token.
     *
     * When we wire raft:
     * - If not leader: forward/redirect to leader.
     * - If leader: append GRANT entry choosing owner and token.
     */

    bool is_leader = (g_hooks.is_leader ? g_hooks.is_leader(g_hooks.ctx) : false);
    const char *leader = (g_hooks.leader_addr ? g_hooks.leader_addr(g_hooks.ctx) : NULL);

    if (!is_leader && leader) {
        char out[512];
        snprintf(out, sizeof(out),
                 "{\"ok\":false,\"op\":\"LEASE_ACQUIRE\",\"key\":\"%s\","
                 "\"error\":\"forward_to_leader\",\"leader\":\"%s\"}",
                 key, leader);
        return send_frame_json(fd, out);
    }

    pthread_mutex_lock(&g_lease_mu);
    hg_lease_entry_t *e = lease_upsert_locked(key);
    if (!e) {
        pthread_mutex_unlock(&g_lease_mu);
        return send_frame_json(fd, "{\"ok\":false,\"error\":\"lease_table_full\"}");
    }

    uint64_t tnow = now_ms();
    bool expired = (e->expires_at_ms > 0 && e->expires_at_ms <= tnow);
    bool free_now = (e->token == 0 || expired);

    if (free_now) {
        e->mode = mode;
        e->owner_node_id = (prefer_owner != 0) ? prefer_owner : 0;
        e->token = (e->token == 0) ? 1 : (e->token + 1);
        e->expires_at_ms = tnow + (ttl_ms ? ttl_ms : 30000);
    }
    uint32_t owner = e->owner_node_id;
    uint64_t token = e->token;
    hg_lease_mode_t cur_mode = e->mode;
    uint64_t exp = e->expires_at_ms;
    pthread_mutex_unlock(&g_lease_mu);

    char out[640];
    snprintf(out, sizeof(out),
             "{\"ok\":true,\"op\":\"LEASE_ACQUIRE\",\"key\":\"%s\","
             "\"owner\":%u,\"token\":%llu,\"mode\":\"%s\",\"expires_at_ms\":%llu}",
             key,
             (unsigned)owner,
             (unsigned long long)token,
             (cur_mode == HG_LEASE_EXCLUSIVE ? "EXCLUSIVE" : "SHARED"),
             (unsigned long long)exp);
    return send_frame_json(fd, out);
}

static int handle_lease_renew(int fd,
                              const char *key,
                              uint64_t token,
                              uint64_t ttl_ms,
                              uint32_t self_node_id,
                              bool forwarded)
{
    pthread_mutex_lock(&g_lease_mu);
    hg_lease_entry_t *e = lease_find_locked(key);
    if (!e) {
        pthread_mutex_unlock(&g_lease_mu);
        return send_frame_json(fd, "{\"ok\":false,\"error\":\"not_found\"}");
    }

    uint32_t owner = e->owner_node_id;
    uint64_t cur_token = e->token;

    if (token != cur_token) {
        pthread_mutex_unlock(&g_lease_mu);
        char out[512];
        snprintf(out, sizeof(out),
                 "{\"ok\":false,\"op\":\"LEASE_RENEW\",\"key\":\"%s\","
                 "\"error\":\"stale_token\",\"owner\":%u,\"token\":%llu}",
                 key, (unsigned)owner, (unsigned long long)cur_token);
        return send_frame_json(fd, out);
    }

    if (owner != 0 && self_node_id != 0 && owner != self_node_id) {
        pthread_mutex_unlock(&g_lease_mu);
        if (forwarded) {
            char out[512];
            snprintf(out, sizeof(out),
                     "{\"ok\":false,\"op\":\"LEASE_RENEW\",\"key\":\"%s\","
                     "\"error\":\"owner_redirect_failed\",\"owner\":%u}",
                     key, (unsigned)owner);
            return send_frame_json(fd, out);
        }
        return forward_lease_renew_remote(fd, key, token, ttl_ms, owner, self_node_id);
    }

    if (owner != 0 && self_node_id == 0) {
        pthread_mutex_unlock(&g_lease_mu);
        char out[512];
        snprintf(out, sizeof(out),
                 "{\"ok\":false,\"op\":\"LEASE_RENEW\",\"key\":\"%s\","
                 "\"error\":\"not_owner\",\"owner\":%u}",
                 key, (unsigned)owner);
        return send_frame_json(fd, out);
    }

    uint64_t tnow = now_ms();
    e->expires_at_ms = tnow + (ttl_ms ? ttl_ms : 30000);
    uint64_t exp = e->expires_at_ms;
    pthread_mutex_unlock(&g_lease_mu);

    char out[512];
    snprintf(out, sizeof(out),
             "{\"ok\":true,\"op\":\"LEASE_RENEW\",\"key\":\"%s\",\"token\":%llu,\"expires_at_ms\":%llu}",
             key, (unsigned long long)token, (unsigned long long)exp);
    return send_frame_json(fd, out);
}

static int handle_lease_release(int fd, const char *key, uint64_t token, uint32_t self_node_id)
{
    pthread_mutex_lock(&g_lease_mu);
    hg_lease_entry_t *e = lease_find_locked(key);
    if (!e) {
        pthread_mutex_unlock(&g_lease_mu);
        return send_frame_json(fd, "{\"ok\":false,\"error\":\"not_found\"}");
    }
    if (token != e->token) {
        uint64_t cur = e->token;
        uint32_t owner = e->owner_node_id;
        pthread_mutex_unlock(&g_lease_mu);

        char out[512];
        snprintf(out, sizeof(out),
                 "{\"ok\":false,\"op\":\"LEASE_RELEASE\",\"key\":\"%s\","
                 "\"error\":\"stale_token\",\"owner\":%u,\"token\":%llu}",
                 key, (unsigned)owner, (unsigned long long)cur);
        return send_frame_json(fd, out);
    }
    if (self_node_id != 0 && e->owner_node_id != 0 && e->owner_node_id != self_node_id) {
        uint32_t owner = e->owner_node_id;
        pthread_mutex_unlock(&g_lease_mu);

        char out[512];
        snprintf(out, sizeof(out),
                 "{\"ok\":false,\"op\":\"LEASE_RELEASE\",\"key\":\"%s\","
                 "\"error\":\"not_owner\",\"owner\":%u}",
                 key, (unsigned)owner);
        return send_frame_json(fd, out);
    }

    /* Remove by swapping with last (simple). */
    size_t idx = (size_t)(e - g_leases);
    g_leases[idx] = g_leases[g_lease_count - 1];
    memset(&g_leases[g_lease_count - 1], 0, sizeof(g_leases[g_lease_count - 1]));
    g_lease_count--;
    pthread_mutex_unlock(&g_lease_mu);

    char out[256];
    snprintf(out, sizeof(out),
             "{\"ok\":true,\"op\":\"LEASE_RELEASE\",\"key\":\"%s\"}", key);
    return send_frame_json(fd, out);
}

int hg_leasing_apply_lease_make(const struct RaftLeaseCommand *cmd)
{
    if (!cmd || cmd->key[0] == '\0')
        return -EINVAL;

    pthread_mutex_lock(&g_lease_mu);
    hg_lease_entry_t *e = lease_upsert_locked(cmd->key);
    if (!e) {
        pthread_mutex_unlock(&g_lease_mu);
        return -ENOSPC;
    }

    hg_lease_mode_t mode = (cmd->mode == HG_LEASE_EXCLUSIVE) ?
                           HG_LEASE_EXCLUSIVE : HG_LEASE_SHARED;
    e->mode = mode;

    uint32_t owner = cmd->owner_node_id ?
                     cmd->owner_node_id : cmd->requester_node_id;
    e->owner_node_id = owner;

    uint64_t token = cmd->token;
    if (token == 0)
        token = (e->token == 0) ? 1 : (e->token + 1);
    e->token = token;

    uint64_t base = cmd->timestamp_ms;
    if (base == 0)
        base = now_ms();
    e->expires_at_ms = cmd->ttl_ms ? base + cmd->ttl_ms : 0;

    pthread_mutex_unlock(&g_lease_mu);
    return 0;
}

int hg_leasing_apply_lease_renew(const struct RaftLeaseCommand *cmd)
{
    if (!cmd || cmd->key[0] == '\0')
        return -EINVAL;

    pthread_mutex_lock(&g_lease_mu);
    hg_lease_entry_t *e = lease_find_locked(cmd->key);
    if (!e) {
        pthread_mutex_unlock(&g_lease_mu);
        return -ENOENT;
    }

    if (cmd->token == 0 || e->token != cmd->token) {
        pthread_mutex_unlock(&g_lease_mu);
        return -ESTALE;
    }

    uint64_t base = cmd->timestamp_ms ? cmd->timestamp_ms : now_ms();
    uint64_t ttl_ms = cmd->ttl_ms ? cmd->ttl_ms : 30000;
    e->expires_at_ms = base + ttl_ms;
    pthread_mutex_unlock(&g_lease_mu);
    return 0;
}

int hg_leasing_apply_lease_release(const struct RaftLeaseCommand *cmd)
{
    if (!cmd || cmd->key[0] == '\0')
        return -EINVAL;

    pthread_mutex_lock(&g_lease_mu);
    hg_lease_entry_t *e = lease_find_locked(cmd->key);
    if (!e) {
        pthread_mutex_unlock(&g_lease_mu);
        return -ENOENT;
    }

    if (cmd->token != 0 && e->token != cmd->token) {
        pthread_mutex_unlock(&g_lease_mu);
        return -ESTALE;
    }

    size_t idx = (size_t)(e - g_leases);
    g_leases[idx] = g_leases[g_lease_count - 1];
    memset(&g_leases[g_lease_count - 1], 0, sizeof(g_leases[0]));
    g_lease_count--;
    pthread_mutex_unlock(&g_lease_mu);
    return 0;
}

/* -------------------------------------------------------------------------- */
/* Connection loop                                                             */
/* -------------------------------------------------------------------------- */

static int set_sock_reuse(int fd)
{
    int one = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) != 0) return -errno;
#ifdef SO_REUSEPORT
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));
#endif
    return 0;
}

static int make_listen_socket(const char *addr, int port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -errno;

    int rc = set_sock_reuse(fd);
    if (rc != 0) { close(fd); return rc; }

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, addr, &sa.sin_addr) != 1) {
        close(fd);
        return -EINVAL;
    }

    if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
        rc = -errno;
        close(fd);
        return rc;
    }
    if (listen(fd, 128) != 0) {
        rc = -errno;
        close(fd);
        return rc;
    }
    return fd;
}

typedef struct conn_ctx {
    int fd;
    int idle_ms;
    uint32_t self_node_id; /* optional: set via env for debugging */
} conn_ctx_t;

static void *conn_thread(void *arg)
{
    conn_ctx_t *cx = (conn_ctx_t *)arg;
    int fd = cx->fd;
    int idle_ms = cx->idle_ms;
    uint32_t self_node_id = cx->self_node_id;
    free(cx);

    lease_log("INFO", "client fd=%d connected", fd);

    uint64_t last_active = now_ms();

    for (;;) {
        /* crude idle timeout: if no data arrives, read will block; so we use SO_RCVTIMEO */
        struct timeval tv;
        tv.tv_sec = idle_ms / 1000;
        tv.tv_usec = (idle_ms % 1000) * 1000;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        uint32_t be_len = 0;
        int rr = read_full(fd, &be_len, sizeof(be_len));
        if (rr == 0) break;
        if (rr < 0) {
            if (rr == -EAGAIN || rr == -EWOULDBLOCK) {
                uint64_t tnow = now_ms();
                if ((int)(tnow - last_active) > idle_ms) break;
                continue;
            }
            lease_log("WARN", "fd=%d read error %d", fd, rr);
            break;
        }

        uint32_t len = ntohl(be_len);
        if (len == 0 || len > (1024 * 1024)) { /* 1MB max for debug */
            send_frame_json(fd, "{\"ok\":false,\"error\":\"bad_frame_len\"}");
            break;
        }

        char *json = (char *)calloc(1, (size_t)len + 1);
        if (!json) {
            send_frame_json(fd, "{\"ok\":false,\"error\":\"oom\"}");
            break;
        }
        rr = read_full(fd, json, len);
        if (rr <= 0) {
            free(json);
            break;
        }

        last_active = now_ms();

        lease_log("INFO", "fd=%d req=%s", fd, json);

        JVal root;
        if (!jparse(json, &root) || root.t != JT_OBJ) {
            jfree(&root);
            free(json);
            send_frame_json(fd, "{\"ok\":false,\"error\":\"bad_json\"}");
            continue;
        }

        const char *op_s  = jstr(jobj_get(&root, "op"));
        const char *key   = jstr(jobj_get(&root, "key"));
        const char *mode_s= jstr(jobj_get(&root, "mode"));
        uint64_t token    = (uint64_t)jnum_i(jobj_get(&root, "token"), 0);
        uint64_t ttl_ms   = (uint64_t)jnum_i(jobj_get(&root, "ttl_ms"), 30000);
        uint32_t prefer_owner = (uint32_t)jnum_i(jobj_get(&root, "prefer_owner"), 0);
        bool forwarded = jbool(jobj_get(&root, "forwarded"), false);

        if (!op_s) {
            send_frame_json(fd, "{\"ok\":false,\"error\":\"missing_op\"}");
            jfree(&root);
            free(json);
            continue;
        }

        hg_lease_op_t op = parse_op(op_s);
        if (op == 0) {
            send_frame_json(fd, "{\"ok\":false,\"error\":\"unknown_op\"}");
            jfree(&root);
            free(json);
            continue;
        }

        if (!key || !*key) {
            send_frame_json(fd, "{\"ok\":false,\"error\":\"missing_key\"}");
            jfree(&root);
            free(json);
            continue;
        }

        if (op == HG_LEASE_RENEW && self_node_id != 0 && token != 0) {
            uint32_t owner_node = 0;
            pthread_mutex_lock(&g_lease_mu);
            const hg_lease_entry_t *lease = lease_find_locked(key);
            if (lease)
                owner_node = lease->owner_node_id;
            pthread_mutex_unlock(&g_lease_mu);

            if (owner_node != 0 && owner_node != self_node_id) {
                int frc;
                if (forwarded) {
                    char out[512];
                    snprintf(out, sizeof(out),
                             "{\"ok\":false,\"op\":\"LEASE_RENEW\",\"key\":\"%s\","
                             "\"error\":\"owner_redirect_failed\",\"owner\":%u}",
                             key, (unsigned)owner_node);
                    frc = send_frame_json(fd, out);
                } else {
                    frc = forward_lease_renew_remote(fd, key, token, ttl_ms, owner_node, self_node_id);
                }

                jfree(&root);
                free(json);
                if (frc != 0) {
                    lease_log("WARN", "fd=%d renew_forward rc=%d", fd, frc);
                    break;
                }
                continue;
            }
        }

        if (op == HG_LEASE_ACQUIRE) {
            bool is_leader = true;
            if (g_hooks.is_leader)
                is_leader = g_hooks.is_leader(g_hooks.ctx);
            if (!is_leader) {
                int frc;
                if (forwarded) {
                    frc = send_forwarded_error(fd, op_s, key, "leader_redirect_failed");
                } else {
                    const char *leader = g_hooks.leader_addr ?
                                         g_hooks.leader_addr(g_hooks.ctx) : NULL;
                    frc = forward_to_leader_json(fd, leader, json, len, op_s, key);
                }
                jfree(&root);
                free(json);
                if (frc != 0) {
                    lease_log("WARN", "fd=%d forward rc=%d", fd, frc);
                    break;
                }
                continue;
            }
        }

        int rc = 0;
        switch (op) {
            case HG_LEASE_LOOKUP:
                rc = handle_lease_lookup(fd, key);
                break;

            case HG_LEASE_ACQUIRE: {
                hg_lease_mode_t m = parse_mode(mode_s);
                if (m == 0) {
                    rc = send_frame_json(fd, "{\"ok\":false,\"error\":\"missing_or_bad_mode\"}");
                    break;
                }
                rc = handle_lease_acquire(fd, key, m, ttl_ms, prefer_owner);
                break;
            }

            case HG_LEASE_RENEW:
                if (token == 0) {
                    rc = send_frame_json(fd, "{\"ok\":false,\"error\":\"missing_token\"}");
                    break;
                }
                rc = handle_lease_renew(fd, key, token, ttl_ms, self_node_id, forwarded);
                break;

            case HG_LEASE_RELEASE:
                if (token == 0) {
                    rc = send_frame_json(fd, "{\"ok\":false,\"error\":\"missing_token\"}");
                    break;
                }
                rc = handle_lease_release(fd, key, token, self_node_id);
                break;

            default:
                rc = send_frame_json(fd, "{\"ok\":false,\"error\":\"unhandled\"}");
                break;
        }

        if (rc != 0) {
            lease_log("WARN", "fd=%d send rc=%d", fd, rc);
            jfree(&root);
            free(json);
            break;
        }

        jfree(&root);
        free(json);
    }

    lease_log("INFO", "client fd=%d disconnected", fd);
    close(fd);
    return NULL;
}

/* -------------------------------------------------------------------------- */
/* Listener thread  
 * The leasing agent gets its own thread and a seperate listener on a new port
 * for now. We may mold this into the same port when we move to UV protocol.
 * But it will stay in its own thread and connections will be immediately
 * forwarded here to take load off the storage threads.
*/
/* -------------------------------------------------------------------------- */

static void *listener_thread(void *arg)
{
    (void)arg;

    const char *addr = get_env_str("HIVE_LEASE_LISTEN_ADDR", "0.0.0.0");
    int port = get_env_int("HIVE_LEASE_LISTEN_PORT", 7400);
    int idle_ms = get_env_int("HIVE_LEASE_IDLE_MS", 30000);
    uint32_t self_node_id = (uint32_t)get_env_int("HIVE_STORAGE_NODE_ID", 0);

    int fd = make_listen_socket(addr, port);
    if (fd < 0) {
        lease_log("ERR", "listen failed addr=%s port=%d rc=%d", addr, port, fd);
        return NULL;
    }
    g_listen_fd = fd;

    lease_log("INFO", "listening on %s:%d idle_ms=%d", addr, port, idle_ms);

    while (!g_stop) {
        struct sockaddr_in peer;
        socklen_t peer_len = sizeof(peer);
        int cfd = accept(fd, (struct sockaddr *)&peer, &peer_len);
        if (cfd < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
            lease_log("WARN", "accept error: %d", -errno);
            continue;
        }

        conn_ctx_t *cx = (conn_ctx_t *)calloc(1, sizeof(*cx));
        if (!cx) { close(cfd); continue; }
        cx->fd = cfd;
        cx->idle_ms = idle_ms;
        cx->self_node_id = self_node_id;

        pthread_t t;
        pthread_create(&t, NULL, conn_thread, cx);
        pthread_detach(t);
    }

    close(fd);
    g_listen_fd = -1;
    return NULL;
}

/* -------------------------------------------------------------------------- */
/* Public API                                                                  */
/* -------------------------------------------------------------------------- */

int hg_leasing_start(const hg_leasing_config_t *cfg, const hg_leasing_hooks_t *hooks)
{
    if (hooks) {
        memset(&g_hooks, 0, sizeof(g_hooks));
        g_hooks = *hooks;
    } else {
        memset(&g_hooks, 0, sizeof(g_hooks));
    }

    g_stop = 0;

    /* If cfg is provided, prefer it by setting env-like globals later.
     * For now we keep it env-driven for easy debugging.
     */
    (void)cfg;

    int rc = pthread_create(&g_thr, NULL, listener_thread, NULL);
    if (rc != 0) {
        lease_log("ERR", "pthread_create failed: %d", rc);
        return -EIO;
    }
    return 0;
}

void hg_leasing_stop(void)
{
    g_stop = 1;
    if (g_listen_fd >= 0) {
        shutdown(g_listen_fd, SHUT_RDWR);
        close(g_listen_fd);
        g_listen_fd = -1;
    }
    pthread_join(g_thr, NULL);
}
