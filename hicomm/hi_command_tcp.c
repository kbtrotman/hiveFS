/**
 * HiveFS
 *
 * TCP/JSON client for the hive_guard service.  This replaces the older
 * direct-to-MariaDB implementation so that all metadata/locking logic can
 * be centralised inside hive_guard.
 */

#include "hi_command.h"
#include "guard_client_state.h"
#include "hi_command_tcp.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <limits.h>
#include <endian.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <openssl/sha.h>
#include <stdarg.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/utsname.h>
#include <unistd.h>

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

struct guard_link_state g_guard_link;

/* -------------------------------------------------------------------------- */
/* Helper types                                                                */
/* -------------------------------------------------------------------------- */

struct guard_client {
	int      fd;
	uint64_t session_id;
	char    *rx_buf;
	size_t   rx_cap;
};

static struct guard_client g_guard = {
	.fd = -1,
	.session_id = 0,
	.rx_buf = NULL,
	.rx_cap = 0,
};

struct tcp_client {
	int fd;
};

static int tcp_listen_fd = -1;
static struct tcp_client tcp_clients[HICMD_MAX_TCP_CLIENTS];
static size_t tcp_client_count = 0;
static int tcp_kernel_fd = -1;

static int tcp_set_nonblock(int fd)
{
	int flags = fcntl(fd, F_GETFL, 0);
	if (flags < 0)
		return -errno;
	if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
		return -errno;
	return 0;
}

static int tcp_bind_and_listen(const char *host, const char *port)
{
	struct addrinfo hints = {
		.ai_family   = AF_UNSPEC,
		.ai_socktype = SOCK_STREAM,
		.ai_flags    = AI_PASSIVE,
	};
	struct addrinfo *res = NULL, *rp = NULL;
	int rc = getaddrinfo(host, port, &hints, &res);
	if (rc != 0) {
		hifs_err("getaddrinfo(%s:%s) failed: %s", host, port, gai_strerror(rc));
		return -1;
	}

	int fd = -1;
	for (rp = res; rp; rp = rp->ai_next) {
		fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (fd < 0)
			continue;
		int one = 1;
		setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
		if (bind(fd, rp->ai_addr, rp->ai_addrlen) == 0 && listen(fd, 16) == 0) {
			break;
		}
		close(fd);
		fd = -1;
	}
	freeaddrinfo(res);

	if (fd >= 0)
		(void)tcp_set_nonblock(fd);
	return fd;
}

static void tcp_remove_client(size_t idx)
{
	if (idx >= tcp_client_count)
		return;
	close(tcp_clients[idx].fd);
	if (idx != tcp_client_count - 1)
		tcp_clients[idx] = tcp_clients[tcp_client_count - 1];
	tcp_client_count--;
}

static void tcp_accept_new(void)
{
	if (tcp_listen_fd < 0 || tcp_client_count >= HICMD_MAX_TCP_CLIENTS)
		return;

	struct sockaddr_storage ss;
	socklen_t slen = sizeof(ss);
	int fd = accept(tcp_listen_fd, (struct sockaddr *)&ss, &slen);
	if (fd < 0)
		return;
	if (tcp_set_nonblock(fd) != 0) {
		close(fd);
		return;
	}
	tcp_clients[tcp_client_count++].fd = fd;
}

static void tcp_drain_client(size_t idx)
{
	char buf[HIFS_MAX_CMD_SIZE];
	ssize_t n = recv(tcp_clients[idx].fd, buf, sizeof(buf) - 1, 0);
	if (n <= 0) {
		tcp_remove_client(idx);
		return;
	}

	struct hifs_cmds cmd;
	memset(&cmd, 0, sizeof(cmd));
	if (n > HIFS_MAX_CMD_SIZE - 1)
		n = HIFS_MAX_CMD_SIZE - 1;
	cmd.count = (int)n;
	memcpy(cmd.cmd, buf, (size_t)n);

	if (tcp_kernel_fd >= 0 && hicomm_comm_send_cmd(tcp_kernel_fd, &cmd) != 0)
		hifs_warning("failed to enqueue TCP command to kernel");
}

int hicmd_tcp_init(const char *host, const char *port, int comm_fd)
{
	const char *bind_host = (host && *host) ? host : HICMD_DEFAULT_BIND_HOST;
	const char *bind_port = (port && *port) ? port : HICMD_DEFAULT_BIND_PORT;

	tcp_kernel_fd = comm_fd;
	tcp_listen_fd = tcp_bind_and_listen(bind_host, bind_port);
	if (tcp_listen_fd >= 0) {
		hifs_notice("TCP control listening on %s:%s", bind_host, bind_port);
		return 0;
	}
	return -1;
}

void hicmd_tcp_broadcast_cmd(const struct hifs_cmds *cmd)
{
	if (!cmd || cmd->count <= 0)
		return;

	for (size_t i = 0; i < tcp_client_count; ) {
		ssize_t n = send(tcp_clients[i].fd, cmd->cmd, cmd->count, MSG_NOSIGNAL);
		if (n < 0 && (errno == EPIPE || errno == ECONNRESET)) {
			tcp_remove_client(i);
			continue;
		}
		++i;
	}
}

void hicmd_tcp_poll(int timeout_ms)
{
	struct pollfd pfds[1 + HICMD_MAX_TCP_CLIENTS];
	nfds_t nfds = 0;

	if (tcp_listen_fd >= 0) {
		pfds[nfds].fd = tcp_listen_fd;
		pfds[nfds].events = POLLIN;
		++nfds;
	}

	for (size_t i = 0; i < tcp_client_count; ++i) {
		pfds[nfds].fd = tcp_clients[i].fd;
		pfds[nfds].events = POLLIN;
		++nfds;
	}

	if (nfds == 0) {
		if (timeout_ms > 0)
			usleep(timeout_ms * 1000);
		return;
	}

	int rc = poll(pfds, nfds, timeout_ms);
	if (rc <= 0) {
		if (rc < 0 && errno != EINTR)
			hifs_warning("tcp poll failed: %s", strerror(errno));
		return;
	}

	nfds_t idx = 0;
	if (tcp_listen_fd >= 0) {
		if (pfds[idx].revents & POLLIN)
			tcp_accept_new();
		idx++;
	}

	for (size_t c = 0; c < tcp_client_count && idx < nfds; ++c, ++idx) {
		if (pfds[idx].revents & POLLIN) {
			tcp_drain_client(c);
			break;
		}
	}
}

void hicmd_tcp_shutdown(void)
{
	for (size_t i = 0; i < tcp_client_count; ++i)
		close(tcp_clients[i].fd);
	tcp_client_count = 0;
	if (tcp_listen_fd >= 0) {
		close(tcp_listen_fd);
		tcp_listen_fd = -1;
	}
	tcp_kernel_fd = -1;
}

/* -------------------------------------------------------------------------- */
/* Minimal JSON helper (borrowed from hive_guard server implementation)       */
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

static void *jmalloc(size_t n)
{
	void *p = malloc(n);
	if (!p) {
		perror("malloc");
		exit(EXIT_FAILURE);
	}
	return p;
}

static char *jstrdup(const char *s)
{
	size_t n = strlen(s);
	char *d = jmalloc(n + 1);
	memcpy(d, s, n + 1);
	return d;
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

static void jskip_ws(JTok *t)
{
	while (t->i < t->n && isspace((unsigned char)t->s[t->i]))
		++t->i;
}

static int jpeek(JTok *t)
{
	return t->i < t->n ? t->s[t->i] : 0;
}

static int jget(JTok *t)
{
	return t->i < t->n ? t->s[t->i++] : 0;
}

static char *jparse_string(JTok *t)
{
	if (jget(t) != '"')
		return NULL;
	char *out = jmalloc(1);
	size_t cap = 1, len = 0;
	out[0] = '\0';
	while (1) {
		if (t->i >= t->n)
			return NULL;
		int c = jget(t);
		if (c == '"')
			break;
		if (c == '\\') {
			if (t->i >= t->n)
				return NULL;
			c = jget(t);
			switch (c) {
			case 'n': c = '\n'; break;
			case 't': c = '\t'; break;
			case 'r': c = '\r'; break;
			case '\\': case '"': break;
			default: break;
			}
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

static JVal *jparse_value(JTok *t);

static JVal *jparse_object(JTok *t)
{
	if (jget(t) != '{')
		return NULL;
	JField *fields = NULL;
	size_t flen = 0, fcap = 0;
	jskip_ws(t);
	if (jpeek(t) == '}') {
		jget(t);
		JVal *v = jmalloc(sizeof(*v));
		v->t = JT_OBJ;
		v->obj = NULL;
		v->obj_len = 0;
		return v;
	}
	while (1) {
		jskip_ws(t);
		char *key = jparse_string(t);
		if (!key)
			return NULL;
		jskip_ws(t);
		if (jget(t) != ':')
			return NULL;
		jskip_ws(t);
		JVal *val = jparse_value(t);
		if (!val)
			return NULL;
		if (flen == fcap) {
			fcap = fcap ? fcap * 2 : 4;
			fields = realloc(fields, fcap * sizeof(JField));
		}
		fields[flen].key = key;
		fields[flen].val = val;
		++flen;
		jskip_ws(t);
		int c = jget(t);
		if (c == '}')
			break;
		if (c != ',')
			return NULL;
	}
	JVal *v = jmalloc(sizeof(*v));
	v->t = JT_OBJ;
	v->obj = fields;
	v->obj_len = flen;
	return v;
}

static JVal *jparse_bool(JTok *t)
{
	if (t->i + 4 <= t->n && strncmp(t->s + t->i, "true", 4) == 0) {
		t->i += 4;
		JVal *v = jmalloc(sizeof(*v));
		v->t = JT_BOOL;
		v->boolean = 1;
		return v;
	}
	if (t->i + 5 <= t->n && strncmp(t->s + t->i, "false", 5) == 0) {
		t->i += 5;
		JVal *v = jmalloc(sizeof(*v));
		v->t = JT_BOOL;
		v->boolean = 0;
		return v;
	}
	return NULL;
}

static JVal *jparse_null(JTok *t)
{
	if (t->i + 4 <= t->n && strncmp(t->s + t->i, "null", 4) == 0) {
		t->i += 4;
		JVal *v = jmalloc(sizeof(*v));
		v->t = JT_NULL;
		return v;
	}
	return NULL;
}

static JVal *jparse_number(JTok *t)
{
	size_t start = t->i;
	int c = jpeek(t);
	if (c == '-' || c == '+')
		jget(t);
	while (isdigit(jpeek(t)))
		jget(t);
	if (jpeek(t) == '.') {
		jget(t);
		while (isdigit(jpeek(t)))
			jget(t);
	}
	if (t->i == start)
		return NULL;
	char buf[64];
	size_t len = t->i - start;
	if (len >= sizeof(buf))
		len = sizeof(buf) - 1;
	memcpy(buf, t->s + start, len);
	buf[len] = '\0';
	JVal *v = jmalloc(sizeof(*v));
	v->t = JT_NUM;
	v->num = strtod(buf, NULL);
	return v;
}

static JVal *jparse_value(JTok *t)
{
	jskip_ws(t);
	int c = jpeek(t);
	if (c == '{')
		return jparse_object(t);
	if (c == '"') {
		JVal *v = jmalloc(sizeof(*v));
		v->t = JT_STR;
		v->str = jparse_string(t);
		return v;
	}
	JVal *b = jparse_bool(t);
	if (b)
		return b;
	JVal *n = jparse_null(t);
	if (n)
		return n;
	return jparse_number(t);
}

static JVal *jparse(const char *s, size_t len)
{
	JTok t = { .s = s, .i = 0, .n = len };
	JVal *v = jparse_value(&t);
	if (!v)
		return NULL;
	jskip_ws(&t);
	if (t.i != t.n) {
		jfree(v);
		return NULL;
	}
	return v;
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

static const char *jval_str(const JVal *v)
{
	return (v && v->t == JT_STR) ? v->str : NULL;
}

static long jval_long(const JVal *v, long def)
{
	return (v && v->t == JT_NUM) ? (long)v->num : def;
}

/* -------------------------------------------------------------------------- */
/* Base64 helper                                                              */
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
		uint32_t octet_a = in[i++];
		uint32_t octet_b = 0;
		uint32_t octet_c = 0;
		size_t bytes = 1;
		if (i < len) {
			octet_b = in[i++];
			++bytes;
		}
		if (i < len) {
			octet_c = in[i++];
			++bytes;
		}
		uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;
		out[j++] = b64_table[(triple >> 18) & 0x3F];
		out[j++] = b64_table[(triple >> 12) & 0x3F];
		out[j++] = (bytes > 1) ? b64_table[(triple >> 6) & 0x3F] : '=';
		out[j++] = (bytes > 2) ? b64_table[triple & 0x3F] : '=';
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
/* Generic network helpers                                                    */
/* -------------------------------------------------------------------------- */

static int guard_dial(const char *host, const char *port)
{
	struct addrinfo hints = {
		.ai_socktype = SOCK_STREAM,
	};
	struct addrinfo *res = NULL;
	int rc = getaddrinfo(host, port, &hints, &res);
	if (rc != 0) {
		hifs_err("guard dial: getaddrinfo failed: %s", gai_strerror(rc));
		return -1;
	}
	int fd = -1;
	for (struct addrinfo *rp = res; rp; rp = rp->ai_next) {
		fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (fd < 0)
			continue;
		int one = 1;
		setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
		if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0)
			break;
		close(fd);
		fd = -1;
	}
	freeaddrinfo(res);
	if (fd < 0)
		hifs_err("guard dial: unable to connect to %s:%s", host, port);
	return fd;
}

static int send_all(int fd, const void *buf, size_t len)
{
	const uint8_t *p = buf;
	size_t sent = 0;
	while (sent < len) {
		ssize_t n = send(fd, p + sent, len - sent, MSG_NOSIGNAL);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (n == 0)
			return -1;
		sent += (size_t)n;
	}
	return 0;
}

static int recv_all(int fd, void *buf, size_t len)
{
	uint8_t *p = buf;
	size_t recvd = 0;
	while (recvd < len) {
		ssize_t n = recv(fd, p + recvd, len - recvd, 0);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (n == 0)
			return -1;
		recvd += (size_t)n;
	}
	return 0;
}

static bool guard_send_frame(int fd, const char *json, size_t len)
{
	uint32_t be_len = htonl((uint32_t)len);
	if (send_all(fd, &be_len, sizeof(be_len)) != 0)
		return false;
	return send_all(fd, json, len) == 0;
}

static bool guard_recv_frame(int fd, char **json_out, size_t *len_out)
{
	uint32_t be_len = 0;
	if (recv_all(fd, &be_len, sizeof(be_len)) != 0)
		return false;
	uint32_t len = ntohl(be_len);
	if (len == 0 || len > (32U * 1024U * 1024U)) {
		hifs_err("guard recv: invalid frame size %u", len);
		return false;
	}
	char *buf = malloc(len + 1);
	if (!buf)
		return false;
	if (recv_all(fd, buf, len) != 0) {
		free(buf);
		return false;
	}
	buf[len] = '\0';
	*json_out = buf;
	if (len_out)
		*len_out = len;
	return true;
}

static bool guard_ensure_connected(void)
{
	if (g_guard.fd >= 0)
		return true;
	const char *host = getenv("HIFS_GUARD_HOST");
	const char *port = getenv("HIFS_GUARD_PORT");
	if (!host || !*host)
		host = HIFS_GUARD_HOST;
	if (!port || !*port)
		port = HIFS_GUARD_PORT_STR;
	int fd = guard_dial(host, port);
	if (fd < 0)
		return false;
	g_guard.fd = fd;
	return true;
}

static void guard_disconnect(void)
{
	if (g_guard.fd >= 0) {
		close(g_guard.fd);
		g_guard.fd = -1;
	}
	free(g_guard.rx_buf);
	g_guard.rx_buf = NULL;
	g_guard.rx_cap = 0;
	g_guard.session_id = 0;
	g_guard_link.guard_ready = false;
}

/* -------------------------------------------------------------------------- */
/* JSON builder for outgoing requests                                         */
/* -------------------------------------------------------------------------- */

struct json_builder {
	char *buf;
	size_t len;
	size_t cap;
};

static void jb_init(struct json_builder *b)
{
	b->cap = 256;
	b->len = 0;
	b->buf = malloc(b->cap);
	if (!b->buf) {
		perror("malloc");
		exit(EXIT_FAILURE);
	}
	b->buf[0] = '\0';
}

static bool jb_reserve(struct json_builder *b, size_t need)
{
	if (b->len + need + 1 <= b->cap)
		return true;
	size_t new_cap = b->cap;
	while (b->len + need + 1 > new_cap)
		new_cap *= 2;
	char *tmp = realloc(b->buf, new_cap);
	if (!tmp)
		return false;
	b->buf = tmp;
	b->cap = new_cap;
	return true;
}

static bool jb_append(struct json_builder *b, const char *fmt, ...)
{
	va_list ap;
	while (1) {
		va_start(ap, fmt);
		int n = vsnprintf(b->buf + b->len, b->cap - b->len, fmt, ap);
		va_end(ap);
		if (n < 0)
			return false;
		if ((size_t)n < b->cap - b->len) {
			b->len += (size_t)n;
			return true;
		}
		if (!jb_reserve(b, (size_t)n + 1))
			return false;
	}
}

static char *jb_finish(struct json_builder *b)
{
	char *out = b->buf;
	b->buf = NULL;
	b->len = b->cap = 0;
	return out;
}

static void jb_destroy(struct json_builder *b)
{
	free(b->buf);
	b->buf = NULL;
	b->len = b->cap = 0;
}

static char *json_escape(const char *in)
{
	if (!in)
		return jstrdup("");
	size_t extra = 0;
	for (const unsigned char *p = (const unsigned char *)in; *p; ++p) {
		if (*p == '"' || *p == '\\')
			++extra;
		else if (*p == '\n' || *p == '\r' || *p == '\t')
			++extra;
	}
	size_t len = strlen(in);
	char *out = malloc(len + extra + 1);
	if (!out)
		return NULL;
	size_t j = 0;
	for (const unsigned char *p = (const unsigned char *)in; *p; ++p) {
		switch (*p) {
		case '"': out[j++]='\\'; out[j++]='"'; break;
		case '\\': out[j++]='\\'; out[j++]='\\'; break;
		case '\n': out[j++]='\\'; out[j++]='n'; break;
		case '\t': out[j++]='\\'; out[j++]='t'; break;
		case '\r': out[j++]='\\'; out[j++]='r'; break;
		default: out[j++]=(char)*p; break;
		}
	}
	out[j] = '\0';
	return out;
}

enum guard_field_type {
	GF_STR,
	GF_U64,
	GF_I64,
	GF_BOOL,
};

struct guard_field {
	const char *key;
	enum guard_field_type type;
	union {
		const char *str;
		uint64_t u64;
		int64_t i64;
		bool boolean;
	} v;
};

#define GUARD_FIELD_STR(k, s)  { .key = (k), .type = GF_STR,  .v.str = (s) }
#define GUARD_FIELD_U64(k, u)  { .key = (k), .type = GF_U64,  .v.u64 = (u) }
#define GUARD_FIELD_I64(k, i)  { .key = (k), .type = GF_I64,  .v.i64 = (i) }
#define GUARD_FIELD_BOOL(k, b) { .key = (k), .type = GF_BOOL, .v.boolean = (b) }

static char *guard_build_request(const char *type,
				 const struct guard_field *fields, size_t field_count,
				 const void *payload, size_t payload_len)
{
	struct json_builder jb;
	jb_init(&jb);
	if (!jb_append(&jb, "{\"type\":\"%s\"", type))
		goto fail;
	for (size_t i = 0; i < field_count; ++i) {
		const struct guard_field *f = &fields[i];
		switch (f->type) {
		case GF_STR: {
			char *esc = json_escape(f->v.str ? f->v.str : "");
			if (!esc)
				goto fail;
			bool ok = jb_append(&jb, ",\"%s\":\"%s\"", f->key, esc);
			free(esc);
			if (!ok)
				goto fail;
			break;
		}
		case GF_U64:
			if (!jb_append(&jb, ",\"%s\":%llu",
				       f->key,
				       (unsigned long long)f->v.u64))
				goto fail;
			break;
		case GF_I64:
			if (!jb_append(&jb, ",\"%s\":%lld",
				       f->key,
				       (long long)f->v.i64))
				goto fail;
			break;
		case GF_BOOL:
			if (!jb_append(&jb, ",\"%s\":%s",
				       f->key,
				       f->v.boolean ? "true" : "false"))
				goto fail;
			break;
		}
	}
	if (payload && payload_len) {
		char *b64 = base64_encode(payload, payload_len);
		if (!b64)
			goto fail;
		bool ok = jb_append(&jb, ",\"payload_b64\":\"%s\"", b64);
		free(b64);
		if (!ok)
			goto fail;
	}
	if (!jb_append(&jb, "}"))
		goto fail;
	return jb_finish(&jb);
fail:
	jb_destroy(&jb);
	return NULL;
}

/* -------------------------------------------------------------------------- */
/* RPC plumbing                                                                */
/* -------------------------------------------------------------------------- */

struct guard_response {
	char *raw;
	size_t raw_len;
	JVal *root;
};

static void guard_response_destroy(struct guard_response *resp)
{
	if (!resp)
		return;
	jfree(resp->root);
	free(resp->raw);
	memset(resp, 0, sizeof(*resp));
}

static bool guard_rpc(const char *type,
		      const struct guard_field *fields, size_t field_count,
		      const void *payload, size_t payload_len,
		      struct guard_response *resp)
{
	char *json = guard_build_request(type, fields, field_count,
					 payload, payload_len);
	if (!json)
		return false;

	size_t json_len = strlen(json);
	for (int attempt = 0; attempt < 2; ++attempt) {
		if (!guard_ensure_connected()) {
			hifs_warning("guard rpc %s: unable to connect", type);
			break;
		}

		hifs_info("guard rpc %s: sending %zu-byte frame (payload=%zu)",
			  type, json_len, payload_len);
		if (!guard_send_frame(g_guard.fd, json, json_len)) {
			hifs_warning("guard rpc %s: send failed (attempt %d), reconnecting",
				     type, attempt + 1);
			guard_disconnect();
			continue;
		}

		char *reply = NULL;
		size_t reply_len = 0;
		if (!guard_recv_frame(g_guard.fd, &reply, &reply_len)) {
			hifs_warning("guard rpc %s: recv failed (attempt %d), reconnecting",
				     type, attempt + 1);
			free(reply);
			guard_disconnect();
			continue;
		}
		hifs_info("guard rpc %s: received %zu-byte reply", type, reply_len);

		JVal *root = jparse(reply, reply_len);
		if (!root) {
			hifs_err("guard rpc: failed to parse reply: %s", reply);
			free(reply);
			guard_disconnect();
			continue;
		}

		resp->raw = reply;
		resp->raw_len = reply_len;
		resp->root = root;
		free(json);
		return true;
	}

	free(json);
	return false;
}

static bool guard_response_ok(const struct guard_response *resp)
{
	const JVal *ok = jobj_get(resp->root, "ok");
	if (ok && ok->t == JT_BOOL)
		return ok->boolean != 0;
	const JVal *type = jobj_get(resp->root, "type");
	if (type && type->t == JT_STR && strcmp(type->str, "error") == 0)
		return false;
	return true;
}

static void guard_log_error(const struct guard_response *resp,
			    const char *context)
{
	const char *code = jval_str(jobj_get(resp->root, "code"));
	const char *msg = jval_str(jobj_get(resp->root, "message"));
	if (code || msg)
		hifs_err("guard %s failed: %s (%s)",
			 context,
			 msg ? msg : "unknown error",
			 code ? code : "no-code");
	else
		hifs_err("guard %s failed: malformed error response", context);
}

static bool guard_expect_ok(struct guard_response *resp,
			    const char *context)
{
	if (guard_response_ok(resp))
		return true;
	guard_log_error(resp, context);
	return false;
}

static bool guard_copy_payload_exact(struct guard_response *resp,
				     void *out, size_t out_len)
{
	const char *b64 = jval_str(jobj_get(resp->root, "payload_b64"));
	if (!b64) {
		hifs_err("guard response missing payload");
		return false;
	}
	uint8_t *blob = NULL;
	size_t blob_len = 0;
	if (base64_decode(b64, &blob, &blob_len) != 0) {
		hifs_err("guard payload base64 decode failed");
		return false;
	}
	if (blob_len != out_len) {
		hifs_err("guard payload size mismatch (have %zu need %zu)",
			 blob_len, out_len);
		free(blob);
		return false;
	}
	memcpy(out, blob, out_len);
	free(blob);
	return true;
}

static bool guard_take_payload(struct guard_response *resp,
			       uint8_t **out_buf, size_t *out_len)
{
	const char *b64 = jval_str(jobj_get(resp->root, "payload_b64"));
	if (!b64)
		return false;
	uint8_t *blob = NULL;
	size_t blob_len = 0;
	if (base64_decode(b64, &blob, &blob_len) != 0)
		return false;
	*out_buf = blob;
	*out_len = blob_len;
	return true;
}

/* -------------------------------------------------------------------------- */
/* Common local helpers (host info, hashing, etc.)                             */
/* -------------------------------------------------------------------------- */

static char *hifs_get_machine_id(void)
{
	static char id_buf[64];
	FILE *fp = fopen("/etc/machine-id", "r");
	if (fp) {
		if (fgets(id_buf, sizeof(id_buf), fp)) {
			id_buf[strcspn(id_buf, "\n")] = '\0';
			fclose(fp);
			if (id_buf[0] != '\0')
				return id_buf;
		}
		fclose(fp);
	}
	if (gethostname(id_buf, sizeof(id_buf)) == 0)
		return id_buf;
	strcpy(id_buf, "unknown");
	return id_buf;
}

static long hifs_get_host_id(void)
{
	long id = gethostid();
	if (id == -1) {
		char hostname[256];
		if (gethostname(hostname, sizeof(hostname)) == 0) {
			unsigned long hash = 5381;
			for (const unsigned char *p = (const unsigned char *)hostname; *p; ++p)
				hash = ((hash << 5) + hash) + *p;
			id = (long)hash;
		} else {
			id = 0;
		}
	}
	return id;
}

static void bytes_to_hex(const uint8_t *src, size_t len, char *dst)
{
	static const char hexdigits[] = "0123456789abcdef";
	for (size_t i = 0; i < len; ++i) {
		dst[i * 2] = hexdigits[(src[i] >> 4) & 0xF];
		dst[i * 2 + 1] = hexdigits[src[i] & 0xF];
	}
	dst[len * 2] = '\0';
}

static void sha256_hex(const uint8_t *src, size_t len, char *dst)
{
	unsigned char digest[SHA256_DIGEST_LENGTH];
	SHA256(src, len, digest);
	bytes_to_hex(digest, SHA256_DIGEST_LENGTH, dst);
}

/* -------------------------------------------------------------------------- */
/* Guard RPC wrappers for common patterns                                     */
/* -------------------------------------------------------------------------- */

static bool guard_fetch_struct(const char *type,
			       const struct guard_field *fields, size_t field_count,
			       void *out, size_t out_len)
{
	struct guard_response resp = {0};
	bool ok = guard_rpc(type, fields, field_count, NULL, 0, &resp) &&
		  guard_expect_ok(&resp, type) &&
		  guard_copy_payload_exact(&resp, out, out_len);
	guard_response_destroy(&resp);
	return ok;
}

static bool guard_store_struct(const char *type,
			       const struct guard_field *fields, size_t field_count,
			       const void *payload, size_t payload_len)
{
	struct guard_response resp = {0};
	bool ok = guard_rpc(type, fields, field_count, payload, payload_len, &resp) &&
		  guard_expect_ok(&resp, type);
	guard_response_destroy(&resp);
	return ok;
}

/* -------------------------------------------------------------------------- */
/* Public API implementations                                                 */
/* -------------------------------------------------------------------------- */

void init_hive_link(void)
{
	if (g_guard_link.guard_ready)
		return;

	char *machine_id = hifs_get_machine_id();
	long host_id = hifs_get_host_id();
	char pid_buf[32];
	snprintf(pid_buf, sizeof(pid_buf), "%ld", (long)getpid());

	struct guard_field fields[] = {
		GUARD_FIELD_STR("client_id", pid_buf),
		GUARD_FIELD_STR("machine_id", machine_id),
		GUARD_FIELD_I64("host_id", host_id),
		GUARD_FIELD_U64("version", 1),
	};

	struct guard_response resp = {0};
	if (!guard_rpc("hello", fields, ARRAY_SIZE(fields), NULL, 0, &resp)) {
		hifs_err("guard hello failed");
		return;
	}

	if (!guard_expect_ok(&resp, "hello")) {
		guard_response_destroy(&resp);
		return;
	}

	const char *session = jval_str(jobj_get(resp.root, "session_id"));
	if (session)
		g_guard.session_id = strtoull(session, NULL, 10);


	guard_response_destroy(&resp);

	g_guard_link.guard_ready = true;
	hifs_notice("hive_guard session established (session=%llu)",
		    (unsigned long long)g_guard.session_id);
}

void close_hive_link(void)
{
	if (!g_guard_link.guard_ready) {
		guard_disconnect();
		return;
	}

	struct guard_response resp = {0};
	struct guard_field fields[] = {
		GUARD_FIELD_U64("session_id", g_guard.session_id),
	};
	if (guard_rpc("goodbye", fields, ARRAY_SIZE(fields), NULL, 0, &resp))
		guard_response_destroy(&resp);
	guard_disconnect();
	memset(&g_guard_link, 0, sizeof(g_guard_link));
}

int get_hive_vers(void)
{
	struct guard_response resp = {0};
	if (!guard_rpc("db_status", NULL, 0, NULL, 0, &resp))
		return 0;
	if (!guard_expect_ok(&resp, "db_status")) {
		guard_response_destroy(&resp);
		return 0;
	}
	int ver = (int)jval_long(jobj_get(resp.root, "version"), 0);
	guard_response_destroy(&resp);
	return ver;
}

static bool guard_register_host(const char *machine_id,
				const char *hostname,
				const char *ip_address,
				long host_id,
				const struct utsname *uts)
{
	struct guard_field fields[] = {
		GUARD_FIELD_STR("machine_id", machine_id),
		GUARD_FIELD_STR("hostname", hostname),
		GUARD_FIELD_STR("ip_address", ip_address),
		GUARD_FIELD_I64("host_id", host_id),
		GUARD_FIELD_STR("os_name", uts ? uts->sysname : "unknown"),
		GUARD_FIELD_STR("os_version", uts ? uts->release : "unknown"),
	};
	struct guard_response resp = {0};
	bool ok = guard_rpc("register_host", fields, ARRAY_SIZE(fields),
			    NULL, 0, &resp) &&
		  guard_expect_ok(&resp, "register_host");
	guard_response_destroy(&resp);
	return ok;
}

int register_hive_host(void)
{
	char hostname[256] = {0};
	if (gethostname(hostname, sizeof(hostname)) != 0) {
		hifs_err("gethostname failed: %s", strerror(errno));
		return 0;
	}

	struct hostent *host_entry = gethostbyname(hostname);
	if (!host_entry) {
		hifs_err("gethostbyname failed: %s", hstrerror(h_errno));
		return 0;
	}

	struct in_addr addr;
	memcpy(&addr.s_addr, host_entry->h_addr_list[0], host_entry->h_length);
	char ip_address[64];
	snprintf(ip_address, sizeof(ip_address), "%s", inet_ntoa(addr));

	struct utsname uts;
	if (uname(&uts) != 0) {
		hifs_err("uname failed: %s", strerror(errno));
		return 0;
	}

	char *machine_id = hifs_get_machine_id();
	long host_id = hifs_get_host_id();

	if (!guard_register_host(machine_id, hostname, ip_address, host_id, &uts))
		return 0;

	g_guard_link.host.serial = machine_id;
	free(g_guard_link.host.name);
	g_guard_link.host.name = strdup(hostname);
	g_guard_link.host.host_id = host_id;
	free(g_guard_link.host.os_name);
	free(g_guard_link.host.os_version);
	g_guard_link.host.os_name = strdup(uts.sysname);
	g_guard_link.host.os_version = strdup(uts.release);
	free(g_guard_link.host.create_time);
	g_guard_link.host.create_time = NULL;

	hifs_info("Registered host %s (%s) with hive_guard", hostname, machine_id);
	return 1;
}

int hifs_get_hive_host_sbs(void)
{
	struct guard_field fields[] = {
		GUARD_FIELD_STR("machine_id", g_guard_link.host.serial ? g_guard_link.host.serial : hifs_get_machine_id()),
	};
	struct guard_response resp = {0};
	const char *machine_id = fields[0].v.str;

	hifs_info("Requesting host superblock list for %s", machine_id);
	if (!guard_rpc("host_super_list", fields, ARRAY_SIZE(fields), NULL, 0, &resp)) {
		hifs_warning("host_super_list RPC failed (will retry once)");
		if (!guard_rpc("host_super_list", fields, ARRAY_SIZE(fields), NULL, 0, &resp)) {
			hifs_err("host_super_list RPC failed after retry");
			return 0;
		}
	}
	if (!guard_expect_ok(&resp, "host_super_list")) {
		guard_response_destroy(&resp);
		return 0;
	}

	uint8_t *buf = NULL;
	size_t len = 0;
	if (!guard_take_payload(&resp, &buf, &len)) {
		hifs_warning("host_super_list response missing payload");
		guard_response_destroy(&resp);
		return 0;
	}

	size_t count = len / sizeof(struct superblock);
	size_t copy = MIN(count, ARRAY_SIZE(g_guard_link.sb));
	memcpy(g_guard_link.sb, buf, copy * sizeof(struct superblock));
	g_guard_link.rows = (int)copy;
	hifs_info("Retrieved %zu superblock entries for %s", copy, machine_id);
	free(buf);
	guard_response_destroy(&resp);
	return g_guard_link.rows;
}

bool hifs_volume_super_get(uint64_t volume_id, struct hifs_volume_superblock *out)
{
	struct guard_field fields[] = {
		GUARD_FIELD_U64("volume_id", volume_id),
	};
	return guard_fetch_struct("volume_super_get", fields, ARRAY_SIZE(fields),
				  out, sizeof(*out));
}

bool hifs_volume_super_set(uint64_t volume_id, const struct hifs_volume_superblock *vsb)
{
	struct guard_field fields[] = {
		GUARD_FIELD_U64("volume_id", volume_id),
	};
	return guard_store_struct("volume_super_set", fields, ARRAY_SIZE(fields),
				  vsb, sizeof(*vsb));
}

bool hifs_root_dentry_load(uint64_t volume_id, struct hifs_volume_root_dentry *out)
{
	struct guard_field fields[] = {
		GUARD_FIELD_U64("volume_id", volume_id),
	};
	return guard_fetch_struct("root_dentry_get", fields, ARRAY_SIZE(fields),
				  out, sizeof(*out));
}

bool hifs_root_dentry_store(uint64_t volume_id,
			    const struct hifs_volume_root_dentry *root)
{
	struct guard_field fields[] = {
		GUARD_FIELD_U64("volume_id", volume_id),
	};
	return guard_store_struct("root_dentry_put", fields, ARRAY_SIZE(fields),
				  root, sizeof(*root));
}

bool hifs_volume_dentry_load_by_inode(uint64_t volume_id, uint64_t inode,
				      struct hifs_volume_dentry *out)
{
	struct guard_field fields[] = {
		GUARD_FIELD_U64("volume_id", volume_id),
		GUARD_FIELD_U64("inode", inode),
	};
	return guard_fetch_struct("volume_dentry_get_inode",
				  fields, ARRAY_SIZE(fields),
				  out, sizeof(*out));
}

bool hifs_volume_dentry_load_by_name(uint64_t volume_id, uint64_t parent,
				     const char *name_hex, uint32_t name_hex_len,
				     struct hifs_volume_dentry *out)
{
	(void)name_hex_len;
	struct guard_field fields[] = {
		GUARD_FIELD_U64("volume_id", volume_id),
		GUARD_FIELD_U64("parent", parent),
		GUARD_FIELD_STR("name_hex", name_hex),
	};
	return guard_fetch_struct("volume_dentry_get_name",
				  fields, ARRAY_SIZE(fields),
				  out, sizeof(*out));
}

bool hifs_volume_dentry_store(uint64_t volume_id,
			      const struct hifs_volume_dentry *dent)
{
	struct guard_field fields[] = {
		GUARD_FIELD_U64("volume_id", volume_id),
	};
	return guard_store_struct("volume_dentry_put",
				  fields, ARRAY_SIZE(fields),
				  dent, sizeof(*dent));
}

bool hifs_volume_inode_load(uint64_t volume_id, uint64_t inode,
			    struct hifs_inode_wire *out)
{
	struct guard_field fields[] = {
		GUARD_FIELD_U64("volume_id", volume_id),
		GUARD_FIELD_U64("inode", inode),
	};
	return guard_fetch_struct("volume_inode_get",
				  fields, ARRAY_SIZE(fields),
				  out, sizeof(*out));
}

bool hifs_volume_inode_store(uint64_t volume_id,
			     const struct hifs_inode_wire *inode)
{
	struct guard_field fields[] = {
		GUARD_FIELD_U64("volume_id", volume_id),
	};
	return guard_store_struct("volume_inode_put", fields, ARRAY_SIZE(fields),
				  inode, sizeof(*inode));
}

bool hifs_volume_block_load(uint64_t volume_id, uint64_t block_no,
			    uint8_t *buf, uint32_t *len)
{
	if (!buf || !len)
		return false;
	struct guard_field fields[] = {
		GUARD_FIELD_U64("volume_id", volume_id),
		GUARD_FIELD_U64("block_no", block_no),
	};
	struct guard_response resp = {0};
	if (!guard_rpc("volume_block_get", fields, ARRAY_SIZE(fields),
		       NULL, 0, &resp))
		return false;
	if (!guard_expect_ok(&resp, "volume_block_get")) {
		guard_response_destroy(&resp);
		return false;
	}
	uint8_t *payload = NULL;
	size_t payload_len = 0;
	if (!guard_take_payload(&resp, &payload, &payload_len)) {
		guard_response_destroy(&resp);
		return false;
	}
	if (payload_len > *len) {
		*len = (uint32_t)payload_len;
		free(payload);
		guard_response_destroy(&resp);
		return false;
	}
	memcpy(buf, payload, payload_len);
	*len = (uint32_t)payload_len;
	free(payload);
	guard_response_destroy(&resp);
	return true;
}

bool hifs_volume_block_store(uint64_t volume_id, uint64_t block_no,
			     const uint8_t *buf, uint32_t len,
			     const uint8_t *hash, uint8_t hash_algo)
{
	if (!buf || len == 0)
		return false;

	char hash_hex[HIFS_BLOCK_HASH_SIZE * 2 + 1];
	uint8_t algo = hash_algo;

	if (!hash) {
		sha256_hex(buf, len, hash_hex);
		algo = HIFS_HASH_ALGO_SHA256;
	} else {
		bytes_to_hex(hash, HIFS_BLOCK_HASH_SIZE, hash_hex);
	}

	struct guard_field fields[] = {
		GUARD_FIELD_U64("volume_id", volume_id),
		GUARD_FIELD_U64("block_no", block_no),
		GUARD_FIELD_U32("hash_algo", algo),
		GUARD_FIELD_STR("hash", hash_hex),
	};
	return guard_store_struct("volume_block_put",
				  fields, ARRAY_SIZE(fields),
				  buf, len);
}

bool hifs_contig_block_send(uint64_t volume_id, uint64_t block_start,
			    const uint32_t *lengths, size_t block_count,
			    const uint8_t *data, size_t data_len,
			    const uint8_t *hash_algos,
			    const uint8_t *hashes)
{
	struct guard_field fields[] = {
		GUARD_FIELD_U64("volume_id", volume_id),
		GUARD_FIELD_U64("block_start", block_start),
		GUARD_FIELD_U64("block_count", block_count),
	};
	struct hifs_contig_block_stream header = {
		.block_start = htole64(block_start),
		.block_count = htole32(block_count),
		.reserved = 0,
	};
	size_t lengths_bytes;
	size_t meta_bytes;
	size_t expected = 0;
	uint8_t *payload;
	uint8_t *cursor;
	bool ok;

	if (!lengths || !data || !hash_algos || !hashes || !block_count)
		return false;

	for (size_t i = 0; i < block_count; ++i) {
		expected += lengths[i];
		if (lengths[i] == 0)
			return false;
	}
	if (expected != data_len)
		return false;

	if (block_count > (SIZE_MAX - sizeof(header)) / sizeof(uint32_t))
		return false;
	lengths_bytes = block_count * sizeof(uint32_t);
	meta_bytes = block_count * sizeof(struct hifs_block_hash_wire);
	if (data_len > SIZE_MAX - (sizeof(header) + lengths_bytes + meta_bytes))
		return false;
	payload = malloc(sizeof(header) + lengths_bytes + meta_bytes + data_len);
	if (!payload)
		return false;

	memcpy(payload, &header, sizeof(header));
	cursor = payload + sizeof(header);
	for (size_t i = 0; i < block_count; ++i) {
		uint32_t le = htole32(lengths[i]);
		memcpy(cursor + i * sizeof(uint32_t), &le, sizeof(le));
	}
	struct hifs_block_hash_wire *meta =
		(struct hifs_block_hash_wire *)(cursor + lengths_bytes);
	for (size_t i = 0; i < block_count; ++i) {
		struct hifs_block_hash_wire *entry = &meta[i];
		entry->hash_algo = hash_algos[i];
		memset(entry->reserved, 0, sizeof(entry->reserved));
		memcpy(entry->hash, hashes + i * HIFS_BLOCK_HASH_SIZE,
		       HIFS_BLOCK_HASH_SIZE);
	}
	memcpy((uint8_t *)meta + meta_bytes, data, data_len);

	ok = guard_store_struct("volume_block_put_contig",
				fields, ARRAY_SIZE(fields),
				payload, sizeof(header) + lengths_bytes + meta_bytes + data_len);
	free(payload);
	return ok;
}

bool hifs_volume_block_recv(uint64_t volume_id, uint64_t block_no,
			    uint8_t *buf, uint32_t *len)
{
	return hifs_volume_block_load(volume_id, block_no, buf, len);
}

bool hifs_insert_data(const char *q_string)
{
	(void)q_string;
	hifs_warning("hifs_insert_data: legacy SQL path is no longer supported");
	return false;
}

char *hifs_get_quoted_value(const char *in_str)
{
	return in_str ? strdup(in_str) : NULL;
}

char *hifs_get_unquoted_value(const char *in_str)
{
	return in_str ? strdup(in_str) : NULL;
}

void hifs_release_query(void)
{
	/* nothing to do in guard mode */
}

int save_binary_data(char *data_block, char *hash)
{
	(void)data_block;
	(void)hash;
	return 0;
}
