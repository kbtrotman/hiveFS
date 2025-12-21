/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */


#include <sys/un.h>

#include "hive_bootstrap.h"
#include "hive_bootstrap_sock.h"

#define HIVE_BOOTSTRAP_SOCK_PATH "/run/hive_bootstrap.sock"
#define HIVE_BOOTSTRAP_MSG_MAX   4096
#define HIVE_BOOTSTRAP_BACKLOG   4

struct hive_bootstrap_request {
	char command[32];
	uint64_t cluster_id;
	uint64_t node_id;
	char cluster_name[30];
	char cluster_desc[201];
	char node_name[50];
	char join_token[201];
};

static char g_status_message[64] = "IDLE";
static unsigned int g_status_percent = 0;
static char g_config_state[16] = "IDLE";
static char g_socket_path[sizeof(((struct sockaddr_un *)0)->sun_path)] =
	HIVE_BOOTSTRAP_SOCK_PATH;
static const char *configure_status(void);

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

static const char *json_escape_string(const char *src, char *dst, size_t dst_sz)
{
	size_t j = 0;

	if (!dst || dst_sz == 0)
		return "";
	if (!src)
		src = "";
	for (size_t i = 0; src[i] && j + 2 < dst_sz; ++i) {
		unsigned char c = (unsigned char)src[i];

		switch (c) {
		case '"':
		case '\\':
			dst[j++] = '\\';
			dst[j++] = (char)c;
			break;
		case '\n':
			dst[j++] = '\\';
			dst[j++] = 'n';
			break;
		case '\r':
			dst[j++] = '\\';
			dst[j++] = 'r';
			break;
		case '\t':
			dst[j++] = '\\';
			dst[j++] = 't';
			break;
		default:
			if (c < 0x20) {
				if (j + 6 >= dst_sz)
					goto done;
				int n = snprintf(dst + j, dst_sz - j, "\\u%04x", c);
				if (n <= 0)
					goto done;
				j += (size_t)n;
			} else {
				dst[j++] = (char)c;
			}
			break;
		}
		if (j >= dst_sz - 1)
			break;
	}
done:
	dst[j] = '\0';
	return dst;
}

static void bootstrap_status_update(const char *message, unsigned int percent,
				    const char *state)
{
	if (percent > 100)
		percent = 100;
	if (!message || !*message)
		message = "IDLE";
	safe_copy(g_status_message, sizeof(g_status_message), message);
	g_status_percent = percent;
	if (state && *state)
		safe_copy(g_config_state, sizeof(g_config_state), state);
	else if (!g_config_state[0])
		safe_copy(g_config_state, sizeof(g_config_state), "IDLE");
}

static bool parse_bootstrap_request(const char *json,
				    struct hive_bootstrap_request *req)
{
	if (!json || !req)
		return false;
	memset(req, 0, sizeof(*req));

    sprintf("Json: %s", json);
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
	bootstrap_status_update("cluster_config: processing", 25, "OP_PENDING");
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
	bootstrap_status_update("cluster_config: complete", 100, "IDLE");
}

static void configure_node(const struct hive_bootstrap_request *req)
{
	bootstrap_status_update("node_join: processing", 25, "OP_PENDING");
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
	bootstrap_status_update("node_join: complete", 100, "IDLE");
}

static void configure_foreigner(const struct hive_bootstrap_request *req)
{
	(void)req;
	bootstrap_status_update("add_foreigner: processing", 50, "OP_PENDING");
	fprintf(stdout, "bootstrap: add_foreigner request received\n");
	bootstrap_status_update("add_foreigner: complete", 100, "IDLE");
}

static bool dispatch_bootstrap_request(const struct hive_bootstrap_request *req,
				       const char **ok_message_out)
{
	if (!req)
		return false;
	if (ok_message_out)
		*ok_message_out = NULL;
	if (strcmp(req->command, "cluster_init") == 0 ||
	    strcmp(req->command, "cluster") == 0 ||
	    strcmp(req->command, "cluster_config") == 0) {
		configure_cluster(req);
		if (ok_message_out)
			*ok_message_out = configure_status();
		return true;
	}
	if (strcmp(req->command, "node_config") == 0 ||
	    strcmp(req->command, "node_join") == 0 ||
	    strcmp(req->command, "node") == 0) {
		configure_node(req);
		if (ok_message_out)
			*ok_message_out = configure_status();
		return true;
	}
	if (strcmp(req->command, "foreigner_config") == 0 ||
	    strcmp(req->command, "add_foreigner") == 0 ||
	    strcmp(req->command, "foreigner") == 0) {
		configure_foreigner(req);
		if (ok_message_out)
			*ok_message_out = configure_status();
		return true;
	}
    	if (strcmp(req->command, "status") == 0 ||
	    strcmp(req->command, "stat") == 0 ||
	    strcmp(req->command, "alive") == 0) {
		if (ok_message_out)
			*ok_message_out = configure_status();
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
	const char *ok_message = NULL;

	if (!read_json_request(fd, buf, sizeof(buf))) {
		respond(fd, "ERR invalid payload\n");
		return false;
	}
	if (!parse_bootstrap_request(buf, &req)) {
		respond(fd, "ERR invalid JSON\n");
		return false;
	}
	if (!dispatch_bootstrap_request(&req, &ok_message)) {
		respond(fd, "ERR unknown command\n");
		return false;
	}

	if (ok_message && *ok_message) {
		respond(fd, "OK ");
		respond(fd, ok_message);
		respond(fd, "\n");
	} else {
		respond(fd, "OK\n");
	}
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
	safe_copy(g_socket_path, sizeof(g_socket_path), addr.sun_path);
	unlink(path);
	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		close(fd);
		return -1;
	}
	if (chmod(path, 0666) != 0) {
		close(fd);
		unlink(path);
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

static inline const char *state_or_unknown(const char *state)
{
	return (state && *state) ? state : "unknown";
}

static const char *configure_status(void)
{
	static char status_buf[HIVE_BOOTSTRAP_MSG_MAX];
	char msg_buf[128];
	char state_buf[32];
	char token_buf[256];
	char ts_buf[128];
	struct stat st;
	const char *config_state = json_escape_string(g_config_state,
						      state_buf,
						      sizeof(state_buf));
	const char *config_msg = json_escape_string(g_status_message,
						    msg_buf,
						    sizeof(msg_buf));
	const char *token = json_escape_string(hbc.bootstrap_token,
					       token_buf,
					       sizeof(token_buf));
	const char *boot_ts = json_escape_string(hbc.first_boot_ts,
						 ts_buf,
						 sizeof(ts_buf));
	unsigned int percent = g_status_percent;
	char progress_text[8];

	if (percent > 100)
		percent = 100;
	snprintf(progress_text, sizeof(progress_text), "%u%%", percent);

	if (stat(g_socket_path, &st) != 0) {
		snprintf(status_buf, sizeof(status_buf),
			 "{\"ok\":false,"
			 "\"command\":\"status\","
			 "\"config_status\":\"IN_ERROR\","
			 "\"status\":\"IN_ERROR\","
			 "\"config_progress\":\"0%%\","
			 "\"percent\":0,"
			 "\"config_msg\":\"NO BOOTSTRAPS\","
			 "\"cluster_state\":\"unknown\","
			 "\"database_state\":\"unknown\","
			 "\"kv_state\":\"unknown\","
			 "\"cont1_state\":\"unknown\","
			 "\"cont2_state\":\"unknown\","
			 "\"cluster_id\":0,"
			 "\"node_id\":0,"
			 "\"min_nodes_req\":0,"
			 "\"bootstrap_token\":\"\","
			 "\"first_boot_ts\":\"\","
			 "\"config_progress_value\":0}");
		return status_buf;
	}

	snprintf(status_buf, sizeof(status_buf),
		 "{\"ok\":true,"
		 "\"command\":\"status\","
		 "\"config_status\":\"%s\","
		 "\"status\":\"%s\","
		 "\"config_progress\":\"%s\","
		 "\"percent\":%u,"
		 "\"config_msg\":\"%s\","
		 "\"cluster_state\":\"%s\","
		 "\"database_state\":\"%s\","
		 "\"kv_state\":\"%s\","
		 "\"cont1_state\":\"%s\","
		 "\"cont2_state\":\"%s\","
		 "\"cluster_id\":%llu,"
		 "\"node_id\":%u,"
		 "\"min_nodes_req\":%llu,"
		 "\"bootstrap_token\":\"%s\","
		 "\"first_boot_ts\":\"%s\","
		 "\"config_progress_value\":%u}",
		 config_state,
		 config_state,
		 progress_text,
		 percent,
		 config_msg,
		 state_or_unknown(hbc.cluster_state),
		 state_or_unknown(hbc.database_state),
		 state_or_unknown(hbc.kv_state),
		 state_or_unknown(hbc.cont1_state),
		 state_or_unknown(hbc.cont2_state),
		 (unsigned long long)hbc.cluster_id,
		 hbc.storage_node_id,
		 (unsigned long long)hbc.min_nodes_req,
		 token,
		 boot_ts,
		 percent);
	return status_buf;
}
