/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

#include "hive_bootstrap.h"
#include "hive_bootstrap_sock.h"

#include <sys/un.h>

#define HIVE_BOOTSTRAP_SOCK_PATH "/run/hive_bootstrap.sock"
#define HIVE_BOOTSTRAP_MSG_MAX   4096
#define HIVE_BOOTSTRAP_BACKLOG   4

struct hive_bootstrap_request {
	char command[32];
	uint64_t cluster_id;
	uint64_t node_id;
	char cluster_name[31];
	char cluster_desc[201];
	char node_name[31];
	char join_token[201];
};

static inline const char *skip_ws(const char *p)
{
	while (p && *p && isspace((unsigned char)*p))
		++p;
	return p;
}

static const char *find_json_key(const char *json, const char *key,
				 size_t *needle_len_out)
{
	char needle[64];
	size_t klen;
	int n;

	if (!json || !key)
		return NULL;
	klen = strlen(key);
	if (klen + 3 > sizeof(needle))
		return NULL;
	n = snprintf(needle, sizeof(needle), "\"%s\"", key);
	if (n < 0 || (size_t)n >= sizeof(needle))
		return NULL;
	if (needle_len_out)
		*needle_len_out = (size_t)n;
	return strstr(json, needle);
}

static bool parse_json_string_value(const char *json, const char *key,
				    char *out, size_t out_len)
{
	size_t needle_len = 0;
	const char *p = find_json_key(json, key, &needle_len);

	if (!p || !out || out_len == 0)
		return false;
	p += needle_len;
	p = skip_ws(p);
	if (*p != ':')
		return false;
	p = skip_ws(p + 1);
	if (*p != '"')
		return false;
	++p;
	const char *start = p;
	while (*p && *p != '"')
		++p;
	size_t len = (size_t)(p - start);
	if (len >= out_len)
		len = out_len - 1;
	memcpy(out, start, len);
	out[len] = '\0';
	return true;
}

static bool parse_json_u64_value(const char *json, const char *key,
				 uint64_t *out)
{
	size_t needle_len = 0;
	const char *p = find_json_key(json, key, &needle_len);

	if (!p || !out)
		return false;
	p += needle_len;
	p = skip_ws(p);
	if (*p != ':')
		return false;
	p = skip_ws(p + 1);

	errno = 0;
	char *end = NULL;
	unsigned long long v = strtoull(p, &end, 10);
	if (p == end || errno == ERANGE)
		return false;
	*out = (uint64_t)v;
	return true;
}

static void safe_copy(char *dst, size_t dst_len, const char *src)
{
	size_t to_copy;

	if (!dst || dst_len == 0)
		return;
	if (!src) {
		dst[0] = '\0';
		return;
	}
	to_copy = strnlen(src, dst_len - 1);
	memcpy(dst, src, to_copy);
	dst[to_copy] = '\0';
}

static bool parse_bootstrap_request(const char *json,
				    struct hive_bootstrap_request *req)
{
	if (!json || !req)
		return false;
	memset(req, 0, sizeof(*req));

	parse_json_string_value(json, "command",
				req->command, sizeof(req->command));
	parse_json_u64_value(json, "cluster_id", &req->cluster_id);
	parse_json_u64_value(json, "clister_id", &req->cluster_id);
	parse_json_string_value(json, "cluster_name",
				req->cluster_name, sizeof(req->cluster_name));
	parse_json_string_value(json, "cluster_desc",
				req->cluster_desc, sizeof(req->cluster_desc));
	parse_json_u64_value(json, "node_id", &req->node_id);
	parse_json_string_value(json, "node_name",
				req->node_name, sizeof(req->node_name));
	parse_json_string_value(json, "join_token",
				req->join_token, sizeof(req->join_token));

	return req->command[0] != '\0';
}

static void configure_cluster(const struct hive_bootstrap_request *req)
{
	hbc.cluster_id = req->cluster_id;
	safe_copy(hbc.cluster_state, sizeof(hbc.cluster_state), "configuring");
	safe_copy(hbc.database_state, sizeof(hbc.database_state), "pending");
	safe_copy(hbc.bootstrap_token, sizeof(hbc.bootstrap_token),
		  req->join_token);
	fprintf(stdout,
		"bootstrap: configure_cluster cluster_id=%llu name=%s desc=%s\n",
		(unsigned long long)req->cluster_id,
		req->cluster_name,
		req->cluster_desc);
}

static void configure_node(const struct hive_bootstrap_request *req)
{
	hbc.cluster_id = req->cluster_id;
	hbc.storage_node_id = (uint32_t)req->node_id;
	safe_copy(hbc.storage_node_name, sizeof(hbc.storage_node_name),
		  req->node_name);
	safe_copy(hbc.bootstrap_token, sizeof(hbc.bootstrap_token),
		  req->join_token);
	hbc.storage_node_last_heartbeat = (uint64_t)time(NULL);
	fprintf(stdout,
		"bootstrap: configure_node cluster_id=%llu node_id=%llu name=%s\n",
		(unsigned long long)req->cluster_id,
		(unsigned long long)req->node_id,
		req->node_name);
}

static bool dispatch_bootstrap_request(const struct hive_bootstrap_request *req)
{
	if (!req)
		return false;
	if (strcmp(req->command, "cluster_config") == 0 ||
	    strcmp(req->command, "cluster") == 0) {
		configure_cluster(req);
		return true;
	}
	if (strcmp(req->command, "node_config") == 0 ||
	    strcmp(req->command, "node_join") == 0 ||
	    strcmp(req->command, "node") == 0) {
		configure_node(req);
		return true;
	}
	return false;
}

static void respond(int fd, const char *msg)
{
	size_t len;

	if (fd < 0 || !msg)
		return;
	len = strlen(msg);
	while (len > 0) {
		ssize_t wr = write(fd, msg, len);
		if (wr < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		if (wr == 0)
			break;
		msg += wr;
		len -= (size_t)wr;
	}
}

static bool read_json_request(int fd, char *buf, size_t buf_sz)
{
	size_t total = 0;

	if (!buf || buf_sz == 0)
		return false;

	while (total < buf_sz - 1) {
		ssize_t rd = recv(fd, buf + total, buf_sz - 1 - total, 0);
		if (rd < 0) {
			if (errno == EINTR)
				continue;
			return false;
		}
		if (rd == 0)
			break;
		total += (size_t)rd;
		if (memchr(buf + total - rd, '\n', (size_t)rd) ||
		    memchr(buf + total - rd, '\r', (size_t)rd))
			break;
	}
	if (total == 0)
		return false;
	buf[total] = '\0';
	char *newline = strpbrk(buf, "\r\n");
	if (newline)
		*newline = '\0';
	return true;
}

static bool process_client(int fd)
{
	char buf[HIVE_BOOTSTRAP_MSG_MAX];
	struct hive_bootstrap_request req;

	if (!read_json_request(fd, buf, sizeof(buf))) {
		respond(fd, "ERR invalid payload\n");
		return false;
	}
	if (!parse_bootstrap_request(buf, &req)) {
		respond(fd, "ERR invalid JSON\n");
		return false;
	}
	if (!dispatch_bootstrap_request(&req)) {
		respond(fd, "ERR unknown command\n");
		return false;
	}

	respond(fd, "OK\n");
	return true;
}

static int create_listener(const char *path)
{
	struct sockaddr_un addr;
	int fd;

	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0)
		return -1;
	if (!path || !*path)
		path = HIVE_BOOTSTRAP_SOCK_PATH;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	safe_copy(addr.sun_path, sizeof(addr.sun_path), path);
	unlink(path);
	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		close(fd);
		return -1;
	}
	if (listen(fd, HIVE_BOOTSTRAP_BACKLOG) != 0) {
		close(fd);
		unlink(path);
		return -1;
	}
	return fd;
}

int hive_bootstrap_sock_run(const char *socket_path)
{
	const char *path = socket_path && *socket_path
		? socket_path : HIVE_BOOTSTRAP_SOCK_PATH;
	int listener = create_listener(path);
	bool handled = false;

	if (listener < 0)
		return -1;

	while (!handled) {
		int client = accept(listener, NULL, NULL);

		if (client < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		handled = process_client(client);
		close(client);
	}

	close(listener);
	unlink(path);
	return handled ? 0 : -1;
}
