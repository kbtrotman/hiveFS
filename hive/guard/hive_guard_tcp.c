/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

/**
 * HiveFS hive_guard TCP server
 *
 * Listens for JSON-over-length-prefixed frames and proxies requests to the
 * existing MariaDB-backed helpers in hive_guard_sql.c.  The protocol mirrors
 * the new hi_command_tcp_coms.c client: callers send a JSON object with
 * "type", optional scalar arguments, and (for binary payloads) a
 * "payload_b64" field.  Successful replies include {"ok":true,...}
 * and may return "payload_b64" blobs.
 */

#include "hive_guard.h"
#include "hive_guard_sql.h"
#include "../../hifs_shared_defs.h"
#include <errno.h>
#include <pthread.h>
#include <openssl/sha.h>

static int write_framed_json(int fd, const char *json);


/* -------------------------------------------------------------------------- */
/* Logging bridge for hi_command macros                                       */
/* -------------------------------------------------------------------------- */

/* Sanity: ensure protocol structs match the shared definitions we send/recv. */
/* This ensures the hifs_shared_defs.h file is the only defs used for all stages. */
_Static_assert(sizeof(struct hifs_sb_msg) == sizeof(struct hifs_sb_msg), "hifs_sb_msg size mismatch");
_Static_assert(sizeof(struct hifs_root_msg) == sizeof(struct hifs_root_msg), "hifs_root_msg size mismatch");
_Static_assert(sizeof(struct hifs_dentry_msg) == sizeof(struct hifs_dentry_msg), "hifs_dentry_msg size mismatch");
_Static_assert(sizeof(struct hifs_inode_msg) == sizeof(struct hifs_inode_msg), "hifs_inode_msg size mismatch");

void hicomm_log(int level, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	FILE *stream = (level <= LOG_ERR) ? stderr : stdout;
	vfprintf(stream, fmt, ap);
	fputc('\n', stream);
	fflush(stream);

	va_end(ap);

	va_start(ap, fmt);
	vsyslog(level, fmt, ap);
	va_end(ap);
}

/* -------------------------------------------------------------------------- */
/* Environment helpers                                                        */
/* -------------------------------------------------------------------------- */
static const char *get_env(const char *k, const char *def)
{
	const char *v = getenv(k);
	return (v && *v) ? v : def;
}

static int get_env_int(const char *k, int def)
{
	const char *v = getenv(k);
	return (v && *v) ? atoi(v) : def;
}

/* -------------------------------------------------------------------------- */
/* Minimal JSON parsing (shared with earlier prototype)                       */
/* -------------------------------------------------------------------------- */
typedef struct JVal JVal;
typedef struct JField JField;

struct JVal {
	enum { JT_NULL, JT_BOOL, JT_NUM, JT_STR, JT_OBJ, JT_ARR } t;
	double num;
	int boolean;
	char *str;
	JField *obj;
	size_t obj_len;
	JVal **arr;
	size_t arr_len;
};

struct JField {
	char *key;
	JVal *val;
};

static void *jxmalloc(size_t n)
{
	void *p = malloc(n);
	if (!p) {
		perror("malloc");
		exit(EXIT_FAILURE);
	}
	return p;
}

static void jfree(JVal *v)
{
	if (!v)
		return;
	if (v->t == JT_STR && v->str)
		free(v->str);
	if (v->t == JT_OBJ) {
		for (size_t i = 0; i < v->obj_len; ++i) {
			free(v->obj[i].key);
			jfree(v->obj[i].val);
		}
		free(v->obj);
	}
	if (v->t == JT_ARR) {
		for (size_t i = 0; i < v->arr_len; ++i)
			jfree(v->arr[i]);
		free(v->arr);
	}
	free(v);
}

typedef struct {
	const char *s;
	size_t i;
	size_t n;
} JTok;

static void sp(JTok *t)
{
	while (t->i < t->n && isspace((unsigned char)t->s[t->i]))
		++t->i;
}

static int pk(JTok *t)
{
	return t->i < t->n ? t->s[t->i] : 0;
}

static int gt(JTok *t)
{
	return t->i < t->n ? t->s[t->i++] : 0;
}

static char *parse_string(JTok *t)
{
	if (gt(t) != '"')
		return NULL;
	char *out = jxmalloc(1);
	size_t cap = 1, len = 0;
	out[0] = '\0';
	while (1) {
		if (t->i >= t->n)
			return NULL;
		int c = gt(t);
		if (c == '"')
			break;
		if (c == '\\') {
			if (t->i >= t->n)
				return NULL;
			c = gt(t);
			if (c == 'n')
				c = '\n';
			else if (c == 't')
				c = '\t';
		}
		if (len + 1 >= cap) {
			cap = cap ? cap * 2 : 4;
			out = realloc(out, cap);
		}
		out[len++] = (char)c;
		out[len] = '\0';
	}
	return out;
}

static JVal *parse_value(JTok *t);

static JVal *parse_object(JTok *t)
{
	if (gt(t) != '{')
		return NULL;
	JField *fields = NULL;
	size_t flen = 0, fcap = 0;
	sp(t);
	if (pk(t) == '}') {
		gt(t);
		JVal *v = jxmalloc(sizeof(*v));
		v->t = JT_OBJ;
		v->obj = NULL;
		v->obj_len = 0;
		return v;
	}
	while (1) {
		sp(t);
		char *key = parse_string(t);
		if (!key)
			return NULL;
		sp(t);
		if (gt(t) != ':')
			return NULL;
		sp(t);
		JVal *val = parse_value(t);
		if (!val)
			return NULL;
		if (flen == fcap) {
			fcap = fcap ? fcap * 2 : 4;
			fields = realloc(fields, fcap * sizeof(JField));
		}
		fields[flen].key = key;
		fields[flen].val = val;
		++flen;
		sp(t);
		int c = gt(t);
		if (c == '}')
			break;
		if (c != ',')
			return NULL;
	}
	JVal *v = jxmalloc(sizeof(*v));
	v->t = JT_OBJ;
	v->obj = fields;
	v->obj_len = flen;
	return v;
}

static JVal *parse_bool(JTok *t)
{
	if (t->i + 4 <= t->n && strncmp(t->s + t->i, "true", 4) == 0) {
		t->i += 4;
		JVal *v = jxmalloc(sizeof(*v));
		v->t = JT_BOOL;
		v->boolean = 1;
		return v;
	}
	if (t->i + 5 <= t->n && strncmp(t->s + t->i, "false", 5) == 0) {
		t->i += 5;
		JVal *v = jxmalloc(sizeof(*v));
		v->t = JT_BOOL;
		v->boolean = 0;
		return v;
	}
	return NULL;
}

static JVal *parse_null(JTok *t)
{
	if (t->i + 4 <= t->n && strncmp(t->s + t->i, "null", 4) == 0) {
		t->i += 4;
		JVal *v = jxmalloc(sizeof(*v));
		v->t = JT_NULL;
		return v;
	}
	return NULL;
}

static JVal *parse_number(JTok *t)
{
	size_t start = t->i;
	int c = pk(t);
	if (c == '-' || c == '+')
		gt(t);
	while (isdigit(pk(t)))
		gt(t);
	if (pk(t) == '.') {
		gt(t);
		while (isdigit(pk(t)))
			gt(t);
	}
	if (t->i == start)
		return NULL;
	char buf[64];
	size_t len = t->i - start;
	if (len >= sizeof(buf))
		len = sizeof(buf) - 1;
	memcpy(buf, t->s + start, len);
	buf[len] = '\0';
	JVal *v = jxmalloc(sizeof(*v));
	v->t = JT_NUM;
	v->num = strtod(buf, NULL);
	return v;
}

static JVal *parse_value(JTok *t)
{
	sp(t);
	int c = pk(t);
	if (c == '{')
		return parse_object(t);
	if (c == '"') {
		JVal *v = jxmalloc(sizeof(*v));
		v->t = JT_STR;
		v->str = parse_string(t);
		return v;
	}
	JVal *b = parse_bool(t);
	if (b)
		return b;
	JVal *n = parse_null(t);
	if (n)
		return n;
	return parse_number(t);
}

static JVal *jparse(const char *s, size_t n)
{
	JTok t = { s, 0, n };
	JVal *v = parse_value(&t);
	if (!v)
		return NULL;
	sp(&t);
	return (t.i == t.n) ? v : NULL;
}

static const JVal *jobj_get(const JVal *o, const char *key)
{
	if (!o || o->t != JT_OBJ)
		return NULL;
	for (size_t i = 0; i < o->obj_len; ++i) {
		if (strcmp(o->obj[i].key, key) == 0)
			return o->obj[i].val;
	}
	return NULL;
}

static const char *jstr(const JVal *v)
{
	return (v && v->t == JT_STR) ? v->str : NULL;
}

static long jnum_i(const JVal *v, long def)
{
	return (v && v->t == JT_NUM) ? (long)v->num : def;
}

/* -------------------------------------------------------------------------- */
/* Base64 helpers                                                             */
/* -------------------------------------------------------------------------- */
static const char b64_table[] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static char *base64_encode(const uint8_t *in, size_t len)
{
	size_t out_len = 4 * ((len + 2) / 3);
	char *out = malloc(out_len + 1);
	if (!out)
		return NULL;
	size_t i = 0, j = 0;
	while (i < len) {
		uint32_t octet_a = i < len ? in[i++] : 0;
		uint32_t octet_b = i < len ? in[i++] : 0;
		uint32_t octet_c = i < len ? in[i++] : 0;
		uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;
		out[j++] = b64_table[(triple >> 18) & 0x3F];
		out[j++] = b64_table[(triple >> 12) & 0x3F];
		out[j++] = (i - 1 > len) ? '=' : b64_table[(triple >> 6) & 0x3F];
		out[j++] = (i > len) ? '=' : b64_table[triple & 0x3F];
		if (i - 1 > len)
			out[j - 2] = '=';
		if (i > len)
			out[j - 1] = '=';
	}
	out[j] = '\0';
	return out;
}

static int base64_decode(const char *in, uint8_t **out, size_t *out_len)
{
	size_t len = strlen(in);
	if (len % 4 != 0)
		return -1;
	size_t olen = len / 4 * 3;
	if (len >= 4 && in[len - 1] == '=')
		--olen;
	if (len >= 4 && in[len - 2] == '=')
		--olen;
	uint8_t *buf = malloc(olen);
	if (!buf)
		return -1;
	int T[256];
	memset(T, -1, sizeof(T));
	for (int i = 0; i < 64; ++i)
		T[(int)b64_table[i]] = i;
	T['='] = 0;
	size_t j = 0;
	for (size_t i = 0; i < len; i += 4) {
		int a = T[(int)in[i]];
		int b = T[(int)in[i + 1]];
		int c = T[(int)in[i + 2]];
		int d = T[(int)in[i + 3]];
		if (a < 0 || b < 0 || c < 0 || d < 0) {
			free(buf);
			return -1;
		}
		buf[j++] = (uint8_t)((a << 2) | (b >> 4));
		if (in[i + 2] != '=')
			buf[j++] = (uint8_t)((b << 4) | (c >> 2));
		if (in[i + 3] != '=')
			buf[j++] = (uint8_t)((c << 6) | d);
	}
	*out = buf;
	if (out_len)
		*out_len = j;
	return 0;
}

/* -------------------------------------------------------------------------- */
/* Networking structures                                                       */
/* -------------------------------------------------------------------------- */
typedef struct Client {
	int fd;
	uint64_t session_id;
	uint64_t last_active_ms;
	char *rbuf;
	size_t rcap;
	size_t rlen;
} Client;

typedef struct {
	int idle_timeout_ms;
	uint64_t next_session;
} ServerState;

static Client *g_clients[MAXC];
static int g_epoll_fd = -1;

struct pending_write {
	uint64_t volume_id;
	uint64_t block_no;
	uint8_t hash[HIFS_BLOCK_HASH_SIZE];
	int fd;
	struct pending_write *next;
};

struct write_ack_event {
	int fd;
	uint64_t volume_id;
	uint64_t block_no;
	struct write_ack_event *next;
};

static pthread_mutex_t g_pending_mu = PTHREAD_MUTEX_INITIALIZER;
static struct pending_write *g_pending_writes;

static pthread_mutex_t g_ack_mu = PTHREAD_MUTEX_INITIALIZER;
static struct write_ack_event *g_ack_head;
static struct write_ack_event *g_ack_tail;

static bool pending_write_register(int fd, uint64_t volume_id,
				       uint64_t block_no,
				       const uint8_t hash[HIFS_BLOCK_HASH_SIZE]);
static void pending_write_cancel(int fd, uint64_t volume_id,
			      uint64_t block_no,
			      const uint8_t hash[HIFS_BLOCK_HASH_SIZE]);
static void pending_write_drop_fd(int fd);
static void write_ack_event_remove_fd(int fd);
static void write_ack_event_enqueue(int fd, uint64_t volume_id, uint64_t block_no);
static struct write_ack_event *write_ack_event_drain(void);
static Client *guard_find_client_by_fd(int fd, int *slot_out);
static bool send_write_ack_frame(Client *cl, uint64_t volume_id, uint64_t block_no);
static void flush_pending_write_acks(void);
static void guard_close_client(int slot);

static uint64_t now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	return (uint64_t)ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL;
}

static bool pending_write_register(int fd, uint64_t volume_id,
				       uint64_t block_no,
				       const uint8_t hash[HIFS_BLOCK_HASH_SIZE])
{
	struct pending_write *w = malloc(sizeof(*w));
	if (!w)
		return false;
	w->fd = fd;
	w->volume_id = volume_id;
	w->block_no = block_no;
	memcpy(w->hash, hash, HIFS_BLOCK_HASH_SIZE);
	pthread_mutex_lock(&g_pending_mu);
	w->next = g_pending_writes;
	g_pending_writes = w;
	pthread_mutex_unlock(&g_pending_mu);
	return true;
}

static void pending_write_cancel(int fd, uint64_t volume_id,
			      uint64_t block_no,
			      const uint8_t hash[HIFS_BLOCK_HASH_SIZE])
{
	pthread_mutex_lock(&g_pending_mu);
	struct pending_write **cur = &g_pending_writes;
	while (*cur) {
		if ((*cur)->fd == fd &&
		    (*cur)->volume_id == volume_id &&
		    (*cur)->block_no == block_no &&
		    memcmp((*cur)->hash, hash, HIFS_BLOCK_HASH_SIZE) == 0) {
			struct pending_write *dead = *cur;
			*cur = dead->next;
			free(dead);
			break;
		}
		cur = &(*cur)->next;
	}
	pthread_mutex_unlock(&g_pending_mu);
}

static void write_ack_event_remove_fd(int fd)
{
	pthread_mutex_lock(&g_ack_mu);
	struct write_ack_event **cur = &g_ack_head;
	while (*cur) {
		if ((*cur)->fd == fd) {
			struct write_ack_event *dead = *cur;
			*cur = dead->next;
			free(dead);
		} else {
			cur = &(*cur)->next;
		}
	}
	g_ack_tail = NULL;
	struct write_ack_event *scan = g_ack_head;
	while (scan) {
		if (!scan->next)
			g_ack_tail = scan;
		scan = scan->next;
	}
	pthread_mutex_unlock(&g_ack_mu);
}

static void pending_write_drop_fd(int fd)
{
	pthread_mutex_lock(&g_pending_mu);
	struct pending_write **cur = &g_pending_writes;
	while (*cur) {
		if ((*cur)->fd == fd) {
			struct pending_write *dead = *cur;
			*cur = dead->next;
			free(dead);
		} else {
			cur = &(*cur)->next;
		}
	}
	pthread_mutex_unlock(&g_pending_mu);
	write_ack_event_remove_fd(fd);
}

static void write_ack_event_enqueue(int fd, uint64_t volume_id, uint64_t block_no)
{
	struct write_ack_event *ev = malloc(sizeof(*ev));
	if (!ev)
		return;
	ev->fd = fd;
	ev->volume_id = volume_id;
	ev->block_no = block_no;
	ev->next = NULL;
	pthread_mutex_lock(&g_ack_mu);
	if (g_ack_tail)
		g_ack_tail->next = ev;
	else
		g_ack_head = ev;
	g_ack_tail = ev;
	pthread_mutex_unlock(&g_ack_mu);
}

static struct write_ack_event *write_ack_event_drain(void)
{
	pthread_mutex_lock(&g_ack_mu);
	struct write_ack_event *head = g_ack_head;
	g_ack_head = NULL;
	g_ack_tail = NULL;
	pthread_mutex_unlock(&g_ack_mu);
	return head;
}

void hifs_guard_notify_write_ack(uint64_t volume_id, uint64_t block_no,
			 const uint8_t *hash, size_t hash_len)
{
	if (!hash)
		return;
	struct pending_write *match = NULL;
	pthread_mutex_lock(&g_pending_mu);
	struct pending_write **cur = &g_pending_writes;
	size_t cmp_len = hash_len < HIFS_BLOCK_HASH_SIZE ? hash_len : HIFS_BLOCK_HASH_SIZE;
	while (*cur) {
		if ((*cur)->volume_id == volume_id &&
		    (*cur)->block_no == block_no &&
		    memcmp((*cur)->hash, hash, cmp_len) == 0) {
			match = *cur;
			*cur = match->next;
			break;
		}
		cur = &(*cur)->next;
	}
	pthread_mutex_unlock(&g_pending_mu);
	if (!match)
		return;
	write_ack_event_enqueue(match->fd, match->volume_id, match->block_no);
	free(match);
}

static Client *guard_find_client_by_fd(int fd, int *slot_out)
{
	for (int i = 0; i < MAXC; ++i) {
		Client *cl = g_clients[i];
		if (cl && cl->fd == fd) {
			if (slot_out)
				*slot_out = i;
			return cl;
		}
	}
	return NULL;
}

static bool send_write_ack_frame(Client *cl, uint64_t volume_id, uint64_t block_no)
{
	char *json = NULL;
	if (asprintf(&json,
		     "{\"ok\":true,\"type\":\"volume_block_put\",\"volume_id\":%llu,\"block_no\":%llu}",
		     (unsigned long long)volume_id,
		     (unsigned long long)block_no) < 0)
		return false;
	int rc = write_framed_json(cl->fd, json);
	free(json);
	return rc == 0;
}

static void flush_pending_write_acks(void)
{
	struct write_ack_event *ev = write_ack_event_drain();
	uint64_t ts = now_ms();
	while (ev) {
		struct write_ack_event *next = ev->next;
		int slot = -1;
		Client *cl = guard_find_client_by_fd(ev->fd, &slot);
		if (cl) {
			if (!send_write_ack_frame(cl, ev->volume_id, ev->block_no)) {
				hifs_warning("write ack notify failed for fd=%d", cl->fd);
				guard_close_client(slot);
			} else {
				cl->last_active_ms = ts;
			}
		}
		free(ev);
		ev = next;
	}
}

static void guard_close_client(int slot)
{
	if (slot < 0 || slot >= MAXC)
		return;
	Client *cl = g_clients[slot];
	if (!cl)
		return;
	if (g_epoll_fd >= 0)
		epoll_ctl(g_epoll_fd, EPOLL_CTL_DEL, cl->fd, NULL);
	pending_write_drop_fd(cl->fd);
	close(cl->fd);
	free(cl->rbuf);
	free(cl);
	g_clients[slot] = NULL;
}

static int set_nonblock(int fd)
{
	int f = fcntl(fd, F_GETFL, 0);
	if (f < 0)
		return -1;
	return fcntl(fd, F_SETFL, f | O_NONBLOCK);
}

static int write_framed_json(int fd, const char *json)
{
	uint32_t n = (uint32_t)strlen(json);
	uint32_t be = htonl(n);
	struct iovec vec[2] = {
		{ .iov_base = &be, .iov_len = 4 },
		{ .iov_base = (void *)json, .iov_len = n },
	};
	ssize_t w1 = send(fd, vec[0].iov_base, vec[0].iov_len, 0);
	if (w1 != 4)
		return -1;
	ssize_t w2 = send(fd, vec[1].iov_base, vec[1].iov_len, 0);
	return (w2 == (ssize_t)n) ? 0 : -1;
}

static int read_frame(Client *c, char **out_json, size_t *out_len)
{
	for (;;) {
		if (c->rlen < 4) {
			if (c->rcap - c->rlen < 1024) {
				size_t new_cap = c->rcap ? c->rcap * 2 : 4096;
				c->rbuf = realloc(c->rbuf, new_cap);
				c->rcap = new_cap;
			}
			ssize_t r = recv(c->fd, c->rbuf + c->rlen, c->rcap - c->rlen, 0);
			if (r == 0)
				return 1;
			if (r < 0) {
				if (errno == EAGAIN || errno == EWOULDBLOCK)
					return 2;
				return -1;
			}
			c->rlen += (size_t)r;
			continue;
		}
		uint32_t be_len = 0;
		memcpy(&be_len, c->rbuf, 4);
		uint32_t len = ntohl(be_len);
		if (len > 32U * 1024U * 1024U)
			return -1;
		if (c->rlen < 4 + len) {
			if (c->rcap - c->rlen < (size_t)(4 + len - c->rlen)) {
				size_t needed = c->rlen + (size_t)len + 4;
				c->rbuf = realloc(c->rbuf, needed);
				c->rcap = needed;
			}
			ssize_t r = recv(c->fd, c->rbuf + c->rlen, c->rcap - c->rlen, 0);
			if (r == 0)
				return 1;
			if (r < 0) {
				if (errno == EAGAIN || errno == EWOULDBLOCK)
					return 2;
				return -1;
			}
			c->rlen += (size_t)r;
			continue;
		}
		char *payload = jxmalloc(len + 1);
		memcpy(payload, c->rbuf + 4, len);
		payload[len] = '\0';
		memmove(c->rbuf, c->rbuf + 4 + len, c->rlen - (4 + len));
		c->rlen -= 4 + len;
		*out_json = payload;
		if (out_len)
			*out_len = len;
		return 0;
	}
}

/* -------------------------------------------------------------------------- */
/* Response helpers                                                            */
/* -------------------------------------------------------------------------- */
static bool send_error_json(int fd, const char *code, const char *fmt, ...)
{
	char msg[512];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);

	char *json = NULL;
	if (asprintf(&json,
		     "{\"ok\":false,\"type\":\"error\",\"code\":\"%s\",\"message\":\"%s\"}",
		     code, msg) < 0)
		return false;
	int rc = write_framed_json(fd, json);
	free(json);
	return rc == 0;
}

static bool send_simple_ok(int fd, const char *type)
{
	char *json = NULL;
	if (asprintf(&json, "{\"ok\":true,\"type\":\"%s\"}", type) < 0)
		return false;
	int rc = write_framed_json(fd, json);
	free(json);
	return rc == 0;
}

static bool send_payload_ok(int fd, const char *type,
			    const void *data, size_t len)
{
	char *b64 = base64_encode(data, len);
	if (!b64)
		return false;
	char *json = NULL;
	if (asprintf(&json,
		     "{\"ok\":true,\"type\":\"%s\",\"payload_b64\":\"%s\"}",
		     type, b64) < 0) {
		free(b64);
		return false;
	}
	free(b64);
	int rc = write_framed_json(fd, json);
	free(json);
	return rc == 0;
}

static bool decode_payload(const JVal *root, uint8_t **buf, size_t *len)
{
	const char *b64 = jstr(jobj_get(root, "payload_b64"));
	if (!b64)
		return false;
	return base64_decode(b64, buf, len) == 0;
}

/* Accept slightly over-sized payloads that only add trailing NUL padding. */
static bool trim_optional_padding(uint8_t *blob, size_t *len, size_t want)
{
	if (!blob || !len)
		return false;
	if (*len == want)
		return true;
	if (*len < want)
		return false;
	for (size_t i = want; i < *len; ++i) {
		if (blob[i] != 0)
			return false;
	}
	*len = want;
	return true;
}

static bool get_u64(const JVal *root, const char *key, uint64_t *out)
{
	const JVal *v = jobj_get(root, key);
	if (!v)
		return false;
	if (v->t == JT_NUM) {
		*out = (uint64_t)v->num;
		return true;
	}
	if (v->t == JT_STR && v->str) {
		*out = strtoull(v->str, NULL, 10);
		return true;
	}
	return false;
}

static const char *safe_str(const char *s)
{
	return s ? s : "";
}

/* -------------------------------------------------------------------------- */
/* RPC handlers                                                                */
/* -------------------------------------------------------------------------- */
static bool handle_register_host(const JVal *root)
{
	const char *machine_id = jstr(jobj_get(root, "machine_id"));
	const char *hostname = jstr(jobj_get(root, "hostname"));
	long host_id = jnum_i(jobj_get(root, "host_id"), -1);
	const char *os_name = jstr(jobj_get(root, "os_name"));
	const char *os_version = jstr(jobj_get(root, "os_version"));

	if (!machine_id || !hostname || host_id < 0)
		return false;

	char *serial_q = hifs_get_quoted_value(machine_id);
	char *name_q = hifs_get_quoted_value(hostname);
	char *os_name_q = hifs_get_quoted_value(os_name ? os_name : "");
	char *os_version_q = hifs_get_quoted_value(os_version ? os_version : "");
	if (!serial_q || !name_q || !os_name_q || !os_version_q) {
		free(serial_q);
		free(name_q);
		free(os_name_q);
		free(os_version_q);
		return false;
	}

	char sql[MAX_QUERY_SIZE];
	snprintf(sql, sizeof(sql), SQL_HOST_UPSERT,
		 safe_str(serial_q), safe_str(name_q), host_id,
		 safe_str(os_name_q), safe_str(os_version_q));

	free(serial_q);
	free(name_q);
	free(os_name_q);
	free(os_version_q);

	return hifs_insert_data(sql);
}

static bool handle_host_super_list(Client *c, const JVal *root)
{
	const char *machine_id = jstr(jobj_get(root, "machine_id"));
	if (!machine_id)
		return send_error_json(c->fd, "bad_request", "missing machine_id");

	sqldb.host.serial = (char *)machine_id;
	if (hifs_get_hive_host_sbs() <= 0)
		return send_error_json(c->fd, "not_found", "no superblocks");

	size_t count = (size_t)sqldb.rows;
	hifs_notice("host_super_list request from %s -> %zu records", machine_id, count);
	size_t payload_len = count * sizeof(struct superblock);
	return send_payload_ok(c->fd, "host_super_list", sqldb.sb, payload_len);
}

static bool handle_struct_get(Client *c, const char *type,
			      uint64_t volume_id,
			      bool (*fn)(uint64_t, void *),
			      size_t struct_size)
{
	uint8_t buf[struct_size];
	memset(buf, 0, sizeof(buf));
	if (!fn(volume_id, buf))
		return send_error_json(c->fd, "not_found", "record missing");
	return send_payload_ok(c->fd, type, buf, sizeof(buf));
}

static bool handle_struct_set(Client *c, uint64_t volume_id,
			      const JVal *root,
			      bool (*fn)(uint64_t, const void *),
			      size_t struct_size,
			      const char *ok_type)
{
	uint8_t *blob = NULL;
	size_t blob_len = 0;
	if (!decode_payload(root, &blob, &blob_len))
		return send_error_json(c->fd, "bad_request", "missing payload");
	if (!trim_optional_padding(blob, &blob_len, struct_size)) {
		free(blob);
		return send_error_json(c->fd, "bad_request", "payload size mismatch");
	}
	bool ok = fn(volume_id, blob);
	free(blob);
	if (!ok)
		return send_error_json(c->fd, "db_error", "unable to store");
	return send_simple_ok(c->fd, ok_type);
}

static bool handle_volume_dentry_get_inode(Client *c, const JVal *root)
{
	uint64_t volume_id = 0, inode = 0;
	if (!get_u64(root, "volume_id", &volume_id) ||
	    !get_u64(root, "inode", &inode))
		return send_error_json(c->fd, "bad_request", "missing volume_id/inode");

	struct hifs_volume_dentry dent = {0};
	if (!hifs_volume_dentry_load_by_inode(volume_id, inode, &dent))
		return send_error_json(c->fd, "not_found", "dentry missing");
	return send_payload_ok(c->fd, "volume_dentry_get_inode", &dent, sizeof(dent));
}

static bool handle_volume_dentry_get_name(Client *c, const JVal *root)
{
	uint64_t volume_id = 0, parent = 0;
	const char *name_hex = jstr(jobj_get(root, "name_hex"));
	if (!get_u64(root, "volume_id", &volume_id) ||
	    !get_u64(root, "parent", &parent) ||
	    !name_hex)
		return send_error_json(c->fd, "bad_request", "missing args");

	struct hifs_volume_dentry dent = {0};
	if (!hifs_volume_dentry_load_by_name(volume_id, parent,
					     name_hex, (uint32_t)strlen(name_hex),
					     &dent))
		return send_error_json(c->fd, "not_found", "dentry missing");
	return send_payload_ok(c->fd, "volume_dentry_get_name", &dent, sizeof(dent));
}

static bool handle_volume_dentry_store(Client *c, const JVal *root)
{
	uint64_t volume_id = 0;
	if (!get_u64(root, "volume_id", &volume_id))
		return send_error_json(c->fd, "bad_request", "missing volume_id");
	uint8_t *blob = NULL;
	size_t blob_len = 0;
	if (!decode_payload(root, &blob, &blob_len))
		return send_error_json(c->fd, "bad_request", "missing payload");
	if (!trim_optional_padding(blob, &blob_len, sizeof(struct hifs_volume_dentry))) {
		free(blob);
		return send_error_json(c->fd, "bad_request", "payload size mismatch");
	}
	bool ok = hifs_volume_dentry_store(volume_id,
					   (const struct hifs_volume_dentry *)blob);
	free(blob);
	if (!ok)
		return send_error_json(c->fd, "db_error", "unable to store dentry");
	return send_simple_ok(c->fd, "volume_dentry_put");
}

static bool handle_volume_inode_load(Client *c, const JVal *root)
{
	uint64_t volume_id = 0, inode = 0;
	if (!get_u64(root, "volume_id", &volume_id) ||
	    !get_u64(root, "inode", &inode))
		return send_error_json(c->fd, "bad_request", "missing args");
	struct hifs_inode_wire wire = {0};
	if (!hifs_volume_inode_load(volume_id, inode, &wire))
		return send_error_json(c->fd, "not_found", "inode missing");
	return send_payload_ok(c->fd, "volume_inode_get", &wire, sizeof(wire));
}

static bool handle_volume_inode_store(Client *c, const JVal *root)
{
	uint64_t volume_id = 0;
	if (!get_u64(root, "volume_id", &volume_id))
		return send_error_json(c->fd, "bad_request", "missing volume_id");
	uint8_t *blob = NULL;
	size_t blob_len = 0;
	if (!decode_payload(root, &blob, &blob_len))
		return send_error_json(c->fd, "bad_request", "missing payload");
	if (!trim_optional_padding(blob, &blob_len, sizeof(struct hifs_inode_wire))) {
		free(blob);
		return send_error_json(c->fd, "bad_request", "payload size mismatch");
	}
	bool ok = hifs_volume_inode_store(volume_id,
					  (const struct hifs_inode_wire *)blob);
	free(blob);
	if (!ok)
		return send_error_json(c->fd, "db_error", "unable to store inode");
	return send_simple_ok(c->fd, "volume_inode_put");
}

static bool handle_volume_block_get(Client *c, const JVal *root)
{
	uint64_t volume_id = 0, block_no = 0;
	if (!get_u64(root, "volume_id", &volume_id) ||
	    !get_u64(root, "block_no", &block_no))
		return send_error_json(c->fd, "bad_request", "missing args");

	uint8_t buf[HIFS_DEFAULT_BLOCK_SIZE];
	uint32_t len = sizeof(buf);
	if (!hifs_volume_block_load(volume_id, block_no, buf, &len))
		return send_error_json(c->fd, "not_found", "block missing");
	return send_payload_ok(c->fd, "volume_block_get", buf, len);
}

static bool handle_volume_block_put(Client *c, const JVal *root)
{
	uint64_t volume_id = 0, block_no = 0;
	if (!get_u64(root, "volume_id", &volume_id) ||
	    !get_u64(root, "block_no", &block_no))
		return send_error_json(c->fd, "bad_request", "missing args");

	uint8_t *blob = NULL;
	size_t blob_len = 0;
	if (!decode_payload(root, &blob, &blob_len))
		return send_error_json(c->fd, "bad_request", "missing payload");
	if (blob_len > HIFS_DEFAULT_BLOCK_SIZE) {
		free(blob);
		return send_error_json(c->fd, "bad_request", "block too large");
	}

	uint8_t digest[SHA256_DIGEST_LENGTH];
	SHA256(blob, blob_len, digest);
	uint8_t hash_bytes[HIFS_BLOCK_HASH_SIZE];
	memcpy(hash_bytes, digest, HIFS_BLOCK_HASH_SIZE);

	if (!pending_write_register(c->fd, volume_id, block_no, hash_bytes)) {
		free(blob);
		return send_error_json(c->fd, "server_busy", "unable to queue write ack");
	}

	int rc = hifs_put_block(volume_id, block_no, blob, blob_len, HIFS_HASH_ALGO_SHA256);
	free(blob);
	if (rc == -EAGAIN) {
		pending_write_cancel(c->fd, volume_id, block_no, hash_bytes);
		return send_error_json(c->fd, "not_leader", "forward to leader");
	}
	if (rc != 0) {
		pending_write_cancel(c->fd, volume_id, block_no, hash_bytes);
		return send_error_json(c->fd, "db_error", "unable to store block");
	}
	return true;
}

/* -------------------------------------------------------------------------- */
/* Command dispatcher                                                         */
/* -------------------------------------------------------------------------- */
static bool dispatch_request(Client *c, ServerState *S, const JVal *root)
{
	const char *type = jstr(jobj_get(root, "type"));
	if (!type)
		return send_error_json(c->fd, "bad_request", "missing type");

	if (strcmp(type, "hello") == 0) {
		if (c->session_id == 0)
			c->session_id = ++S->next_session;
		char *json = NULL;
		if (asprintf(&json,
			     "{\"ok\":true,\"type\":\"welcome\",\"session_id\":%llu,"
			     "\"idle_timeout_ms\":%d}",
			     (unsigned long long)c->session_id,
			     S->idle_timeout_ms) < 0)
			return false;
		int rc = write_framed_json(c->fd, json);
		free(json);
		return rc == 0;
	}
	if (strcmp(type, "goodbye") == 0)
		return send_simple_ok(c->fd, "goodbye");
	if (strcmp(type, "ping") == 0) {
		uint64_t ts = (uint64_t)jnum_i(jobj_get(root, "ts"),
					       (long)now_ms());
		char *json = NULL;
		if (asprintf(&json, "{\"ok\":true,\"type\":\"pong\",\"ts\":%llu}",
			     (unsigned long long)ts) < 0)
			return false;
		int rc = write_framed_json(c->fd, json);
		free(json);
		return rc == 0;
	}
	if (strcmp(type, "db_status") == 0) {
		char *json = NULL;
		int version = get_hive_vers();
		if (asprintf(&json,
			     "{\"ok\":true,\"type\":\"db_status\",\"version\":%d}",
			     version) < 0)
			return false;
		int rc = write_framed_json(c->fd, json);
		free(json);
		return rc == 0;
	}
	if (strcmp(type, "register_host") == 0) {
		if (!handle_register_host(root))
			return send_error_json(c->fd, "db_error", "host upsert failed");
		return send_simple_ok(c->fd, "register_host");
	}
	if (strcmp(type, "host_super_list") == 0)
		return handle_host_super_list(c, root);
	if (strcmp(type, "volume_super_get") == 0) {
		uint64_t volume_id = 0;
		if (!get_u64(root, "volume_id", &volume_id))
			return send_error_json(c->fd, "bad_request", "missing volume_id");
		return handle_struct_get(c, "volume_super_get", volume_id,
					 (bool (*)(uint64_t, void *))hifs_volume_super_get,
					 sizeof(struct hifs_volume_superblock));
	}
	if (strcmp(type, "volume_super_set") == 0) {
		uint64_t volume_id = 0;
		if (!get_u64(root, "volume_id", &volume_id))
			return send_error_json(c->fd, "bad_request", "missing volume_id");
		return handle_struct_set(c, volume_id, root,
					 (bool (*)(uint64_t, const void *))hifs_volume_super_set,
					 sizeof(struct hifs_volume_superblock),
					 "volume_super_set");
	}
	if (strcmp(type, "root_dentry_get") == 0) {
		uint64_t volume_id = 0;
		if (!get_u64(root, "volume_id", &volume_id))
			return send_error_json(c->fd, "bad_request", "missing volume_id");
		return handle_struct_get(c, "root_dentry_get", volume_id,
					 (bool (*)(uint64_t, void *))hifs_root_dentry_load,
					 sizeof(struct hifs_volume_root_dentry));
	}
	if (strcmp(type, "root_dentry_put") == 0) {
		uint64_t volume_id = 0;
		if (!get_u64(root, "volume_id", &volume_id))
			return send_error_json(c->fd, "bad_request", "missing volume_id");
		return handle_struct_set(c, volume_id, root,
					 (bool (*)(uint64_t, const void *))hifs_root_dentry_store,
					 sizeof(struct hifs_volume_root_dentry),
					 "root_dentry_put");
	}
	if (strcmp(type, "volume_dentry_get_inode") == 0)
		return handle_volume_dentry_get_inode(c, root);
	if (strcmp(type, "volume_dentry_get_name") == 0)
		return handle_volume_dentry_get_name(c, root);
	if (strcmp(type, "volume_dentry_put") == 0)
		return handle_volume_dentry_store(c, root);
	if (strcmp(type, "volume_inode_get") == 0)
		return handle_volume_inode_load(c, root);
	if (strcmp(type, "volume_inode_put") == 0)
		return handle_volume_inode_store(c, root);
	if (strcmp(type, "volume_block_get") == 0)
		return handle_volume_block_get(c, root);
	if (strcmp(type, "volume_block_put") == 0)
		return handle_volume_block_put(c, root);

	return send_error_json(c->fd, "unknown_type", "unsupported request");
}

/* -------------------------------------------------------------------------- */
/* Client driver                                                               */
/* -------------------------------------------------------------------------- */
static int handle_one(Client *c, ServerState *S)
{
	char *js = NULL;
	size_t len = 0;
	int rf = read_frame(c, &js, &len);
	if (rf == 2)
		return 0;
	if (rf == 1)
		return 1;
	if (rf < 0)
		return -1;

	JVal *root = jparse(js, len);
	if (!root) {
		send_error_json(c->fd, "bad_json", "invalid JSON payload");
		free(js);
		return 0;
	}

	bool ok = dispatch_request(c, S, root);
	jfree(root);
	free(js);
	c->last_active_ms = now_ms();
	return ok ? 0 : -1;
}

static volatile sig_atomic_t g_stop = 0;
static void on_signal(int s)
{
	(void)s;
	g_stop = 1;
}

/* -------------------------------------------------------------------------- */
/* tcp Server main loop (FROM CLIENT CONNECTIONS)                                                            */
/* -------------------------------------------------------------------------- */
int hive_guard_server_main(void)
{
	printf("here!\n");
	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);

	const char *listen_addr = get_env(ENV_LISTEN_ADDR, "0.0.0.0");
	int listen_port = get_env_int(ENV_LISTEN_PORT, atoi(HIFS_GUARD_PORT_STR));
	int idle_ms = get_env_int(ENV_IDLE_MS, 30000);

	openlog("hive_guard", LOG_PID | LOG_NDELAY, LOG_USER);

	init_hive_link();
	if (!sqldb.sql_init || !sqldb.conn) {
		hifs_err("Unable to connect to MariaDB");
		return EXIT_FAILURE;
	}

	hifs_notice("Preparing hive_guard TCP listener on %s:%d", listen_addr, listen_port);

	int sfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sfd < 0) {
		perror("socket");
		return EXIT_FAILURE;
	}

	int one = 1;
	setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	struct sockaddr_in addr = {
		.sin_family = AF_INET,
		.sin_port = htons(listen_port),
		.sin_addr.s_addr = inet_addr(listen_addr),
	};
	if (bind(sfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("bind");
		close(sfd);
		return EXIT_FAILURE;
	}
	if (listen(sfd, 128) < 0) {
		perror("listen");
		close(sfd);
		return EXIT_FAILURE;
	}
	set_nonblock(sfd);
	hifs_notice("hive_guard listening on %s:%d (idle_timeout=%d ms)",
		    listen_addr, listen_port, idle_ms);

	int ep = epoll_create1(0);
	if (ep < 0) {
		perror("epoll_create1");
		close(sfd);
		return EXIT_FAILURE;
	}
	struct epoll_event ev = {
		.events = EPOLLIN,
		.data.fd = sfd,
	};
	epoll_ctl(ep, EPOLL_CTL_ADD, sfd, &ev);
	g_epoll_fd = ep;
	memset(g_clients, 0, sizeof(g_clients));
	ServerState S = {.idle_timeout_ms = idle_ms, .next_session = 0 };

	struct epoll_event events[128];
	while (!g_stop) {
		int n = epoll_wait(ep, events, 128, 250);
		uint64_t now = now_ms();
		for (int i = 0; i < n; ++i) {
			if (events[i].data.fd == sfd) {
				while (1) {
					struct sockaddr_in ca;
					socklen_t calen = sizeof(ca);
					int cfd = accept(sfd, (struct sockaddr *)&ca, &calen);
					if (cfd < 0) {
						if (errno == EAGAIN || errno == EWOULDBLOCK)
							break;
						perror("accept");
						break;
					}
					set_nonblock(cfd);
					int slot = -1;
					for (int idx = 0; idx < MAXC; ++idx) {
						if (!g_clients[idx]) {
							slot = idx;
							break;
						}
					}
					if (slot < 0) {
						hifs_warning("Too many clients, rejecting fd=%d", cfd);
						close(cfd);
						continue;
					}
					Client *cl = calloc(1, sizeof(Client));
					cl->fd = cfd;
					cl->last_active_ms = now;
					g_clients[slot] = cl;
					struct epoll_event cev = {
						.events = EPOLLIN | EPOLLET,
						.data.u32 = (uint32_t)(slot + 1),
					};
					epoll_ctl(ep, EPOLL_CTL_ADD, cfd, &cev);
					hifs_info("Client connected fd=%d slot=%d", cfd, slot);
				}
			} else {
				int slot = (int)events[i].data.u32 - 1;
				if (slot < 0 || slot >= MAXC || !g_clients[slot])
					continue;
				Client *cl = g_clients[slot];
				int rc = handle_one(cl, &S);
				if (rc != 0) {
					hifs_info("Closing client slot=%d rc=%d", slot, rc);
					guard_close_client(slot);
				}
			}
		}
		flush_pending_write_acks();

		for (int i = 0; i < MAXC; ++i) {
			Client *cl = g_clients[i];
			if (!cl)
				continue;
			if (now - cl->last_active_ms > (uint64_t)S.idle_timeout_ms) {
				hifs_info("Idle timeout slot=%d", i);
				guard_close_client(i);
			}
		}
	}

	flush_pending_write_acks();
	for (int i = 0; i < MAXC; ++i)
		if (g_clients[i])
			guard_close_client(i);
	close(ep);
	close(sfd);
	close_hive_link();
	closelog();
	return EXIT_SUCCESS;
}
