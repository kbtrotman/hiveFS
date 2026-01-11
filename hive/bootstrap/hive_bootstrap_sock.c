/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */


#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <libgen.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "hive_bootstrap.h"
#include "hive_bootstrap_sock.h"
#include "../common/hive_common_sql.h"
#include "../common/hive_common_sock.h"
#include "../../hifs_shared_defs.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

#define UUID_TEXT_LEN 36
#define UUID_BUF_LEN (UUID_TEXT_LEN + 1)
#define BYTES_PER_GIB (1024ULL * 1024ULL * 1024ULL)
#define HIVE_METADATA_MIN_BYTES (200ULL * BYTES_PER_GIB)
#define HIVE_GUARD_KV_MIN_BYTES (200ULL * BYTES_PER_GIB)
#define HIVE_LOG_MIN_BYTES (50ULL * BYTES_PER_GIB)

struct hive_bootstrap_request {
	char command[32];
	uint64_t cluster_id;
	uint64_t node_id;
	char cluster_state[16];
	char cluster_name[30];
	char cluster_desc[201];
	char node_name[100];
	char join_token[201];
	uint16_t min_nodes_req;
	char raw_payload[HIVE_BOOTSTRAP_MSG_MAX];
};

char g_status_message[64] = "IDLE";
unsigned int g_status_percent = 0;
char g_config_state[16] = "IDLE";

static char g_socket_path[sizeof(((struct sockaddr_un *)0)->sun_path)] =
	HIVE_BOOTSTRAP_SOCK_PATH;
static const char *configure_status(void);
static void set_invalid_node_join_state(void);
static bool parse_bootstrap_request(const char *json,
				    struct hive_bootstrap_request *req);
static bool parse_json_string_value(const char *json, const char *key,
				    char *out, size_t out_len);
static bool parse_json_u64_value(const char *json, const char *key,
				 uint64_t *out);

enum bootstrap_request_type g_pending_request_type =
	BOOTSTRAP_REQ_UNKNOWN;

static bool bootstrap_run_command(const char *const argv[])
{
	pid_t pid;

	if (!argv || !argv[0])
		return false;
	pid = fork();
	if (pid < 0) {
		fprintf(stderr, "bootstrap: fork failed for %s: %s\n",
			argv[0], strerror(errno));
		return false;
	}
	if (pid == 0) {
		execvp(argv[0], (char *const *)argv);
		_exit(127);
	}

	int status;
	while (waitpid(pid, &status, 0) < 0) {
		if (errno == EINTR)
			continue;
		fprintf(stderr, "bootstrap: waitpid failed for %s: %s\n",
			argv[0], strerror(errno));
		return false;
	}
	return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static bool ensure_hive_guard_service(void)
{
	static const char *const check_enabled[] = {
		"systemctl", "is-enabled", "hive_guard.service", NULL,
	};
	static const char *const enable_cmd[] = {
		"systemctl", "enable", "--now", "hive_guard.service", NULL,
	};
	static const char *const restart_policy[] = {
		"systemctl", "set-property", "hive_guard.service",
		"Restart=always", "RestartSec=5", NULL,
	};
	static const char *const start_cmd[] = {
		"systemctl", "start", "hive_guard.service", NULL,
	};
	static const char *const status_cmd[] = {
		"systemctl", "is-active", "hive_guard.service", NULL,
	};

	bool active = bootstrap_run_command(status_cmd);
	bool enabled = bootstrap_run_command(check_enabled);

	if (active && enabled)
		return true;

	if (!enabled) {
		if (!bootstrap_run_command(enable_cmd))
			return false;
	}

	(void)bootstrap_run_command(restart_policy);
	active = bootstrap_run_command(status_cmd);
	if (!active) {
		if (!bootstrap_run_command(start_cmd))
			return false;
	}
	return bootstrap_run_command(status_cmd);
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

static bool gather_local_identity(struct hive_storage_node *node)
{
	if (!node)
		return false;
	memset(node, 0, sizeof(*node));

	if (!hifs_get_local_node_identity(node))
		return false;

	if (hbc.storage_node_name[0] &&
	    strcmp(hbc.storage_node_name, node->name) != 0) {
		size_t len = strnlen(hbc.storage_node_name,
				     sizeof(hbc.storage_node_name));

		if (len > 0 && sethostname(hbc.storage_node_name, len) != 0)
			fprintf(stderr,
				"bootstrap: sethostname(%s) failed: %s\n",
				hbc.storage_node_name, strerror(errno));
		hifs_safe_strcpy(node->name, sizeof(node->name),
				 hbc.storage_node_name);
	} else if (!hbc.storage_node_name[0]) {
		hifs_safe_strcpy(hbc.storage_node_name,
				 sizeof(hbc.storage_node_name),
				 node->name);
	}

	if (!node->guard_port)
		node->guard_port = hifs_local_guard_port();
	if (!node->stripe_port)
		node->stripe_port = node->guard_port;
	node->last_heartbeat = (uint64_t)time(NULL);
	return true;
}

static const char *json_string_or_null(const char *src, char *buf, size_t buf_sz)
{
	if (!buf || buf_sz < 3)
		return "null";
	if (!src || !*src)
		return "null";
	buf[0] = '"';
	json_escape_string(src, buf + 1, buf_sz - 2);
	size_t len = strlen(buf + 1);
	if (len > buf_sz - 3)
		len = buf_sz - 3;
	buf[1 + len] = '"';
	buf[2 + len] = '\0';
	return buf;
}

static const char *json_string_or_default(const char *src, const char *fallback,
					  char *buf, size_t buf_sz)
{
	const char *value = (src && *src) ? src : fallback;

	if (!value)
		value = "";
	return json_string_or_null(value, buf, buf_sz);
}

static bool prepare_local_node(struct hive_storage_node *node)
{
	if (!ensure_hive_guard_service()) {
		fprintf(stderr,
			"bootstrap: unable to ensure hive_guard systemd service\n");
		return false;
	}

	if (!gather_local_identity(node)) {
		fprintf(stderr,
			"bootstrap: unable to gather local node identity\n");
		return false;
	}

	return true;
}

static bool send_all(int fd, const char *buf, size_t len)
{
	const char *cursor = buf;
	size_t remaining = len;

	if (fd < 0 || !buf || len == 0)
		return false;
	while (remaining > 0) {
		ssize_t wr = send(fd, cursor, remaining, 0);

		if (wr < 0) {
			if (errno == EINTR)
				continue;
			return false;
		}
		if (wr == 0)
			return false;
		cursor += wr;
		remaining -= (size_t)wr;
	}
	return true;
}

static bool forward_request_to_guard(const char *json_payload)
{
	struct sockaddr_un addr;
	bool ok = false;
	int fd;

	if (!json_payload || !*json_payload)
		return false;
	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) {
		fprintf(stderr,
			"bootstrap: failed to create hive_guard socket: %s\n",
			strerror(errno));
		return false;
	}
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, HIVE_GUARD_SOCK_PATH,
		sizeof(addr.sun_path) - 1);
	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		fprintf(stderr,
			"bootstrap: failed to connect to hive_guard socket %s: %s\n",
			addr.sun_path, strerror(errno));
		close(fd);
		return false;
	}
	size_t payload_len = strlen(json_payload);
	if (send_all(fd, json_payload, payload_len)) {
		char newline = '\n';

		ok = send_all(fd, &newline, 1);
	}
	if (!ok) {
		fprintf(stderr,
			"bootstrap: failed to forward join payload to hive_guard\n");
	}
	close(fd);
	return ok;
}

static bool send_guard_request_with_reply(const char *json_payload,
					  char *response, size_t response_sz)
{
	struct sockaddr_un addr;
	bool ok = false;
	int fd;

	if (!json_payload || !*json_payload)
		return false;
	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) {
		fprintf(stderr,
			"bootstrap: failed to create hive_guard socket: %s\n",
			strerror(errno));
		return false;
	}
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, HIVE_GUARD_SOCK_PATH,
		sizeof(addr.sun_path) - 1);
	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		fprintf(stderr,
			"bootstrap: failed to connect to hive_guard socket %s: %s\n",
			addr.sun_path, strerror(errno));
		close(fd);
		return false;
	}
	size_t payload_len = strlen(json_payload);
	if (!send_all(fd, json_payload, payload_len))
		goto out;
	char newline = '\n';
	if (!send_all(fd, &newline, 1))
		goto out;
	if (!response || response_sz == 0) {
		ok = true;
		goto out;
	}
	size_t total = 0;
	while (total + 1 < response_sz) {
		ssize_t rd = recv(fd, response + total,
				  response_sz - total - 1, 0);
		if (rd < 0) {
			if (errno == EINTR)
				continue;
			goto out;
		}
		if (rd == 0)
			break;
		total += (size_t)rd;
		if (response[total - 1] == '\n')
			break;
	}
	response[total] = '\0';
	ok = total > 0;
out:
	close(fd);
	return ok;
}

static bool build_cluster_join_payload(const struct hive_bootstrap_request *req,
				       char *buf, size_t buf_sz)
{
	if (!req || !buf || buf_sz == 0)
		return false;
	char cluster_name_buf[128];
	char cluster_desc_buf[512];
	char node_name_buf[160];
	char join_token_buf[256];
	char raw_payload_buf[HIVE_BOOTSTRAP_MSG_MAX * 2];
	const char *cluster_name_json =
		json_string_or_null(req->cluster_name, cluster_name_buf,
				    sizeof(cluster_name_buf));
	const char *cluster_desc_json =
		json_string_or_null(req->cluster_desc, cluster_desc_buf,
				    sizeof(cluster_desc_buf));
	const char *node_name_json =
		json_string_or_null(req->node_name, node_name_buf,
				    sizeof(node_name_buf));
	const char *join_token_json =
		json_string_or_null(req->join_token, join_token_buf,
				    sizeof(join_token_buf));
	const char *raw_payload_json =
		json_string_or_null(req->raw_payload, raw_payload_buf,
				    sizeof(raw_payload_buf));
	const char *cluster_state_src =
		(req->cluster_state[0] ? req->cluster_state : "configuring");
	char cluster_state_buf[32];

	json_escape_string(cluster_state_src, cluster_state_buf,
			   sizeof(cluster_state_buf));
	int n = snprintf(
		buf, buf_sz,
		"{\"command\":\"cluster_join\","
		"\"cluster_id\":%llu,"
		"\"node_id\":%llu,"
		"\"min_nodes_req\":%u,"
		"\"cluster_state\":\"%s\","
		"\"cluster_name\":%s,"
		"\"cluster_desc\":%s,"
		"\"node_name\":%s,"
		"\"join_token\":%s,"
		"\"raw_payload\":%s}",
		(unsigned long long)req->cluster_id,
		(unsigned long long)req->node_id,
		(unsigned int)(req->min_nodes_req ? req->min_nodes_req : 1),
		cluster_state_buf,
		cluster_name_json,
		cluster_desc_json,
		node_name_json,
		join_token_json,
		raw_payload_json);
	return n >= 0 && (size_t)n < buf_sz;
}

static bool forward_cluster_join_request(const struct hive_bootstrap_request *req)
{
	char payload[HIVE_BOOTSTRAP_MSG_MAX * 2];
	char response[256];

	if (!build_cluster_join_payload(req, payload, sizeof(payload)))
		return false;
	if (!send_guard_request_with_reply(payload, response, sizeof(response)))
		return false;
	return strncmp(response, "OK", 2) == 0;
}

static unsigned int parse_progress_percent(const char *progress)
{
	if (!progress || !*progress)
		return 0;
	while (*progress && !isdigit((unsigned char)*progress))
		++progress;
	errno = 0;
	char *end = NULL;
	unsigned long val = strtoul(progress, &end, 10);

	if (end == progress || errno == ERANGE)
		return 0;
	if (val > 100)
		val = 100;
	return (unsigned int)val;
}

static void update_local_status_cache(const char *message,
					      unsigned int percent,
					      const char *state)
{
	const char *msg = (message && *message) ? message : "IDLE";
	const char *st = (state && *state) ? state : "IDLE";

	if (percent > 100)
		percent = 100;
	hifs_safe_strcpy(g_status_message, sizeof(g_status_message), msg);
	g_status_percent = percent;
	hifs_safe_strcpy(g_config_state, sizeof(g_config_state), st);
}

static bool apply_guard_status_response(const char *json)
{
	char state[32];
	char message[128];
	char progress[32];

	state[0] = '\0';
	message[0] = '\0';
	progress[0] = '\0';
	parse_json_string_value(json, "config_status",
				state, sizeof(state));
	parse_json_string_value(json, "config_msg",
				message, sizeof(message));
	parse_json_string_value(json, "config_progress",
				progress, sizeof(progress));
	unsigned int percent = parse_progress_percent(progress);
	if (!state[0] && !message[0] && percent == 0)
		return false;

	update_local_status_cache(message[0] ? message : "IDLE",
				   percent,
				   state[0] ? state : "IDLE");
	return true;
}

static bool send_guard_status_packet(const char *message,
					 unsigned int percent,
					 const char *state)
{
	char payload[HIVE_BOOTSTRAP_MSG_MAX];
	char response[HIVE_BOOTSTRAP_MSG_MAX];
	char msg_buf[128];
	char state_buf[32];
	char progress_buf[16];
	const char *msg = (message && *message) ? message : "IDLE";
	const char *st = (state && *state) ? state : "IDLE";

	if (percent > 100)
		percent = 100;
	json_escape_string(msg, msg_buf, sizeof(msg_buf));
	json_escape_string(st, state_buf, sizeof(state_buf));
	snprintf(progress_buf, sizeof(progress_buf), "%u%%", percent);
	int n = snprintf(payload, sizeof(payload),
			 "{\"command\":\"status\"," \
			 "\"config_status\":\"%s\"," \
			 "\"config_progress\":\"%s\"," \
			 "\"config_msg\":\"%s\"}",
			 state_buf[0] ? state_buf : "IDLE",
			 progress_buf,
			 msg_buf[0] ? msg_buf : "IDLE");
	if (n < 0 || (size_t)n >= sizeof(payload))
		return false;
	if (!send_guard_request_with_reply(payload, response,
						 sizeof(response)))
		return false;
	if (response[0] != '{')
		return false;
	return apply_guard_status_response(response);
}

static void push_guard_status_update(const char *message,
					 unsigned int percent,
					 const char *state)
{
	if (!send_guard_status_packet(message, percent, state))
		update_local_status_cache(message, percent, state);
}

static bool build_status_payload(const struct hive_bootstrap_request *req,
				 char *buf, size_t buf_sz)
{
	if (!req || !buf || buf_sz == 0)
		return false;
	char raw_payload_buf[HIVE_BOOTSTRAP_MSG_MAX * 2];
	const char *raw_payload_json =
		json_string_or_null(req->raw_payload, raw_payload_buf,
				    sizeof(raw_payload_buf));
	int n = snprintf(
		buf, buf_sz,
		"{\"command\":\"status\","
		"\"cluster_id\":%llu,"
		"\"node_id\":%llu,"
		"\"raw_payload\":%s}",
		(unsigned long long)req->cluster_id,
		(unsigned long long)req->node_id,
		raw_payload_json);
	return n >= 0 && (size_t)n < buf_sz;
}

static bool request_guard_status(const struct hive_bootstrap_request *req)
{
	char payload[HIVE_BOOTSTRAP_MSG_MAX * 2];
	char response[HIVE_BOOTSTRAP_MSG_MAX];

	if (!build_status_payload(req, payload, sizeof(payload)))
		return false;
	if (!send_guard_request_with_reply(payload, response,
					   sizeof(response)))
		return false;
	if (response[0] != '{')
		return false;
	return apply_guard_status_response(response);
}

static bool build_local_node_join_payload(char *buf, size_t buf_sz)
{
	if (!buf || buf_sz == 0)
		return false;
	if (hbc.cluster_id == 0 || hbc.storage_node_id == 0)
		return false;
	char cluster_state_buf[64];
	char database_state_buf[64];
	char kv_state_buf[64];
	char cont1_state_buf[64];
	char cont2_state_buf[64];
	char token_buf[256];
	char first_boot_buf[64];
	char status_buf[64];
	char message_buf[256];
	char progress_raw[16];
	char progress_buf[32];
	char hive_version_buf[32];
	char patch_value_buf[8];
	char patch_json_buf[16];

	const char *cluster_state_json =
		json_string_or_default(hbc.cluster_state,
				       "unconfigured",
				       cluster_state_buf,
				       sizeof(cluster_state_buf));
	const char *database_state_json =
		json_string_or_default(hbc.database_state,
				       "configured",
				       database_state_buf,
				       sizeof(database_state_buf));
	const char *kv_state_json =
		json_string_or_default(hbc.kv_state,
				       "configured",
				       kv_state_buf,
				       sizeof(kv_state_buf));
	const char *cont1_state_json =
		json_string_or_default(hbc.cont1_state,
				       "configured",
				       cont1_state_buf,
				       sizeof(cont1_state_buf));
	const char *cont2_state_json =
		json_string_or_default(hbc.cont2_state,
				       "configured",
				       cont2_state_buf,
				       sizeof(cont2_state_buf));
	const char *token_json =
		json_string_or_null(hbc.bootstrap_token,
				    token_buf, sizeof(token_buf));
	const char *first_boot_json =
		json_string_or_null(hbc.first_boot_ts,
				    first_boot_buf, sizeof(first_boot_buf));
	const char *status_json =
		json_string_or_default(g_config_state,
				       "IDLE",
				       status_buf, sizeof(status_buf));
	const char *message_json =
		json_string_or_default(g_status_message,
				       "IDLE",
				       message_buf, sizeof(message_buf));
	unsigned int status_pct = g_status_percent;

	if (status_pct > 100)
		status_pct = 100;
	snprintf(progress_raw, sizeof(progress_raw), "%u%%", status_pct);
	const char *progress_json =
		json_string_or_null(progress_raw,
				    progress_buf, sizeof(progress_buf));
	const char *hive_version_json =
		json_string_or_default(HIFS_VERSION,
				       "unknown",
				       hive_version_buf, sizeof(hive_version_buf));
	snprintf(patch_value_buf, sizeof(patch_value_buf), "%04u",
		 (unsigned)(hbc.storage_node_hive_patch_level));
	const char *patch_level_json =
		json_string_or_null(patch_value_buf,
				    patch_json_buf, sizeof(patch_json_buf));
	uint64_t min_nodes =
		hbc.min_nodes_req ? hbc.min_nodes_req : 1ULL;

	int n = snprintf(
		buf, buf_sz,
		"{\"command\":\"node_join\","
		"\"cluster_id\":%llu,"
		"\"node_id\":%u,"
		"\"min_nodes_req\":%llu,"
		"\"cluster_state\":%s,"
		"\"database_state\":%s,"
		"\"kv_state\":%s,"
		"\"cont1_state\":%s,"
		"\"cont2_state\":%s,"
		"\"bootstrap_token\":%s,"
		"\"first_boot_ts\":%s,"
		"\"config_status\":%s,"
		"\"config_progress\":%s,"
		"\"config_msg\":%s,"
		"\"hive_version\":%s,"
		"\"hive_patch_level\":%s}",
		(unsigned long long)hbc.cluster_id,
		hbc.storage_node_id,
		(unsigned long long)min_nodes,
		cluster_state_json,
		database_state_json,
		kv_state_json,
		cont1_state_json,
		cont2_state_json,
		token_json,
		first_boot_json,
		status_json,
		progress_json,
		message_json,
		hive_version_json,
		patch_level_json);
	return n >= 0 && (size_t)n < buf_sz;
}

static bool forward_node_join_request(void)
{
	char payload[HIVE_BOOTSTRAP_MSG_MAX * 2];
	char response[HIVE_BOOTSTRAP_MSG_MAX];

	if (!build_local_node_join_payload(payload, sizeof(payload)))
		return false;
	if (!send_guard_request_with_reply(payload, response,
					   sizeof(response)))
		return false;
	return strncmp(response, "OK", 2) == 0;
}

static void poll_guard_status_updates(const struct hive_bootstrap_request *req)
{
	if (!req)
		return;
	const unsigned int max_attempts = 10;

	for (unsigned int attempt = 0; attempt < max_attempts; ++attempt) {
		if (!request_guard_status(req))
			break;
		if (strcmp(g_config_state, "IDLE") == 0 ||
		    strcmp(g_config_state, "IN_ERROR") == 0)
			break;
		sleep(30);
	}
}

static bool is_cluster_command(const char *cmd)
{
	if (!cmd || !*cmd)
		return false;
	return strcmp(cmd, "cluster_init") == 0 ||
	       strcmp(cmd, "cluster") == 0 ||
	       strcmp(cmd, "cluster_config") == 0;
}

static bool is_node_command(const char *cmd)
{
	if (!cmd || !*cmd)
		return false;
	return strcmp(cmd, "node_config") == 0 ||
	       strcmp(cmd, "node_join") == 0 ||
	       strcmp(cmd, "node") == 0;
}

static bool is_invalid_node_join_request(const struct hive_bootstrap_request *req)
{
	return req && is_node_command(req->command) &&
	       req->cluster_state[0] &&
	       strcmp(req->cluster_state, "unconfigured") == 0;
}

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

static const char *json_number_or_null(char *buf, size_t buf_sz,
				       unsigned long long value)
{
	if (!buf || buf_sz == 0)
		return "null";
	if (value == 0) {
		buf[0] = '\0';
		return "null";
	}
	snprintf(buf, buf_sz, "%llu", value);
	return buf;
}

static bool ensure_directory_path(const char *path)
{
	if (!path || !*path)
		return false;
	if (mkdir(path, 0755) == 0 || errno == EEXIST)
		return true;
	fprintf(stderr, "bootstrap: mkdir(%s) failed: %s\n",
		path, strerror(errno));
	return false;
}

static void trim_newline(char *buf)
{
	if (!buf)
		return;
	size_t len = strcspn(buf, "\r\n");

	buf[len] = '\0';
}

static bool read_uuid_from_file(const char *path, char *uuid, size_t uuid_sz)
{
	if (!path || !uuid || uuid_sz < UUID_BUF_LEN)
		return false;
	FILE *fp = fopen(path, "re");

	if (!fp)
		return false;
	bool ok = fgets(uuid, (int)uuid_sz, fp) != NULL;

	fclose(fp);
	if (!ok)
		return false;
	trim_newline(uuid);
	return strlen(uuid) == UUID_TEXT_LEN;
}

static bool read_kernel_uuid(char *uuid, size_t uuid_sz)
{
	if (!uuid || uuid_sz < UUID_BUF_LEN)
		return false;
	FILE *fp = fopen("/proc/sys/kernel/random/uuid", "re");

	if (!fp)
		return false;
	bool ok = fgets(uuid, (int)uuid_sz, fp) != NULL;

	fclose(fp);
	if (!ok)
		return false;
	trim_newline(uuid);
	return strlen(uuid) == UUID_TEXT_LEN;
}

static bool write_text_file(const char *path, const char *text)
{
	if (!path || !text)
		return false;
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);

	if (fd < 0) {
		fprintf(stderr, "bootstrap: open(%s) failed: %s\n",
			path, strerror(errno));
		return false;
	}
	size_t len = strlen(text);
	size_t off = 0;
	bool ok = true;

	while (off < len) {
		ssize_t n = write(fd, text + off, len - off);

		if (n < 0) {
			if (errno == EINTR)
				continue;
			ok = false;
			fprintf(stderr, "bootstrap: write(%s) failed: %s\n",
				path, strerror(errno));
			break;
		}
		off += (size_t)n;
	}
	close(fd);
	return ok && off == len;
}

static bool ensure_chrony_ready(void)
{
	struct stat st;
	static const char *const disable_ntp_cmd[] = {
		"timedatectl", "set-ntp", "no", NULL,
	};
	static const char *const status_cmd[] = {
		"systemctl", "is-active", "chronyd.service", NULL,
	};
	static const char *const is_enabled_cmd[] = {
		"systemctl", "is-enabled", "chronyd.service", NULL,
	};
	static const char *const enable_cmd[] = {
		"systemctl", "enable", "--now", "chronyd.service", NULL,
	};
	static const char *const start_cmd[] = {
		"systemctl", "start", "chronyd.service", NULL,
	};

	if (stat("/etc/chrony/chrony.conf", &st) != 0 ||
	    !S_ISREG(st.st_mode)) {
		fprintf(stderr,
			"bootstrap: chrony config missing at /etc/chrony/chrony.conf\n");
		return false;
	}

	(void)bootstrap_run_command(disable_ntp_cmd);
	bool active = bootstrap_run_command(status_cmd);
	bool enabled = bootstrap_run_command(is_enabled_cmd);

	if (!enabled) {
		if (!bootstrap_run_command(enable_cmd))
			return false;
		active = bootstrap_run_command(status_cmd);
	}
	if (!active) {
		if (!bootstrap_run_command(start_cmd))
			return false;
	}
	return bootstrap_run_command(status_cmd);
}

static bool get_parent_path(const char *path, char *parent, size_t parent_sz)
{
	if (!path || !parent || parent_sz == 0)
		return false;

	char tmp[PATH_MAX];
	size_t len = strlen(path);

	if (len >= sizeof(tmp)) {
		fprintf(stderr, "bootstrap: path too long: %s\n", path);
		return false;
	}
	memcpy(tmp, path, len + 1);

	if (len > 1 && tmp[len - 1] == '/')
		tmp[len - 1] = '\0';

	char *dir = dirname(tmp);

	if (!dir)
		return false;
	size_t dir_len = strnlen(dir, parent_sz);

	if (dir_len >= parent_sz)
		return false;
	memcpy(parent, dir, dir_len);
	parent[dir_len] = '\0';
	if (parent[0] == '\0')
		strcpy(parent, "/");
	return true;
}

static bool path_is_mountpoint(const char *path, const struct stat *st_path)
{
	struct stat parent;
	char parent_path[PATH_MAX];

	if (!path || !st_path)
		return false;
	if (!get_parent_path(path, parent_path, sizeof(parent_path)))
		return false;
	if (stat(parent_path, &parent) != 0)
		return false;
	return st_path->st_dev != parent.st_dev;
}

static bool check_directory_fs(const char *path, uint64_t min_bytes, dev_t *dev_out)
{
	struct stat st;

	if (stat(path, &st) != 0) {
		fprintf(stderr, "bootstrap: stat(%s) failed: %s\n",
			path, strerror(errno));
		return false;
	}
	if (!S_ISDIR(st.st_mode)) {
		fprintf(stderr, "bootstrap: %s is not a directory\n", path);
		return false;
	}
	if (dev_out)
		*dev_out = st.st_dev;

	bool ok = true;

	if (!path_is_mountpoint(path, &st)) {
		fprintf(stderr, "bootstrap: %s is not a mountpoint\n", path);
		ok = false;
	}

	struct statvfs vfs;

	if (statvfs(path, &vfs) != 0) {
		fprintf(stderr, "bootstrap: statvfs(%s) failed: %s\n",
			path, strerror(errno));
		ok = false;
	} else {
		unsigned long long total = vfs.f_frsize;

		total *= vfs.f_blocks;
		if (min_bytes > 0 && total < min_bytes) {
			fprintf(stderr,
				"bootstrap: %s capacity %llu bytes, need %llu\n",
				path, total, (unsigned long long)min_bytes);
			ok = false;
		}
	}
	return ok;
}

static bool verify_required_filesystems(bool enforce)
{
	const struct {
		const char *path;
		uint64_t min_bytes;
	} reqs[] = {
		{ HIVE_METADATA_DIR, HIVE_METADATA_MIN_BYTES },
		{ HIVE_GUARD_KV_DIR, HIVE_GUARD_KV_MIN_BYTES },
		{ HIVE_LOG_DIR, HIVE_LOG_MIN_BYTES },
	};
	dev_t devs[ARRAY_SIZE(reqs)] = { 0 };
	bool ok = true;

	for (size_t i = 0; i < ARRAY_SIZE(reqs); ++i) {
		if (!check_directory_fs(reqs[i].path, reqs[i].min_bytes,
					&devs[i]))
			ok = false;
	}
	for (size_t i = 0; i < ARRAY_SIZE(reqs); ++i) {
		for (size_t j = i + 1; j < ARRAY_SIZE(reqs); ++j) {
			if (devs[i] != 0 && devs[i] == devs[j]) {
				fprintf(stderr,
					"bootstrap: %s and %s share the same device\n",
					reqs[i].path, reqs[j].path);
				ok = false;
			}
		}
	}
	if (ok)
		return true;
	if (!enforce) {
		fprintf(stderr,
			"bootstrap: filesystem checks failed but enforcement is disabled\n");
		return true;
	}
	return false;
}

enum {
	HIVE_NODE_NAME_LEN = sizeof(((struct hive_storage_node *)0)->name),
	HIVE_NODE_ADDR_LEN = sizeof(((struct hive_storage_node *)0)->address),
	HIVE_NODE_UID_LEN = sizeof(((struct hive_storage_node *)0)->uid),
	HIVE_NODE_SERIAL_LEN = sizeof(((struct hive_storage_node *)0)->serial),
};

static void configure_cluster(const struct hive_bootstrap_request *req)
{
	push_guard_status_update("cluster_config: processing", 25,
				 "OP_PENDING");
	hbc.cluster_id = req->cluster_id;
	hbc.storage_node_id = req->node_id ? (uint32_t)req->node_id : 1U;
	safe_copy(hbc.cluster_state, sizeof(hbc.cluster_state), "configuring");
	safe_copy(hbc.database_state, sizeof(hbc.database_state), "pending");
	safe_copy(hbc.bootstrap_token, sizeof(hbc.bootstrap_token),
		  req->join_token);
	hbc.min_nodes_req = req->min_nodes_req ? req->min_nodes_req : 1;
	fprintf(stdout,
		"bootstrap: configure_cluster cluster_id=%llu name=%s desc=%s\n",
		(unsigned long long)req->cluster_id,
		req->cluster_name,
		req->cluster_desc);

	char cluster_uuid[UUID_BUF_LEN];
	char node_uuid[UUID_BUF_LEN];

	if (!ensure_directory_path(HIVE_BOOTSTRAP_SYS_DIR) ||
	    !ensure_directory_path(HIVE_CLUSTER_DIR)) {
		push_guard_status_update("cluster_config: cannot prep directories",
					 100, "IN_ERROR");
		return;
	}
	if (!read_uuid_from_file(HIVE_CLUSTER_ID, cluster_uuid,
				 sizeof(cluster_uuid))) {
		if (!read_kernel_uuid(cluster_uuid, sizeof(cluster_uuid)) ||
		    !write_text_file(HIVE_CLUSTER_ID, cluster_uuid)) {
			push_guard_status_update("cluster_config: cluster uuid failed",
						 100, "IN_ERROR");
			return;
		}
	}
	if (!read_uuid_from_file(HIVE_NODE_ID, node_uuid,
				 sizeof(node_uuid))) {
		if (!read_kernel_uuid(node_uuid, sizeof(node_uuid)) ||
		    !write_text_file(HIVE_NODE_ID, node_uuid)) {
			push_guard_status_update("cluster_config: node uuid failed",
						 100, "IN_ERROR");
			return;
		}
	}
	if (!ensure_chrony_ready()) {
		push_guard_status_update("cluster_config: chrony not ready",
					 100, "IN_ERROR");
		return;
	}
	const bool enforce_fs_checks = false; /* flip to true after testing. */

	if (!verify_required_filesystems(enforce_fs_checks)) {
		push_guard_status_update("cluster_config: filesystem check failed",
					 100, "IN_ERROR");
		return;
	}


	if (!forward_cluster_join_request(req)) {
		push_guard_status_update("cluster_config: cluster persist failed",
					 100, "IN_ERROR");
		return;
	}

	poll_guard_status_updates(req);
	safe_copy(hbc.cluster_state, sizeof(hbc.cluster_state), "pending");

	fflush(stdout);
	struct hive_storage_node local_node_cluster;

	if (!prepare_local_node(&local_node_cluster))
		return;

	if (!forward_node_join_request())
		return;

    
	push_guard_status_update("cluster_config: complete", 100, "IDLE");
	fprintf(stdout, "bootstrap: cluster metadata persisted in hive_api.cluster\n");
}

static void configure_node(const struct hive_bootstrap_request *req)
{
	push_guard_status_update("node_join: processing", 25, "OP_PENDING");
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

	struct hive_storage_node local_node_join;

	if (!prepare_local_node(&local_node_join))
		return;

	if (!forward_request_to_guard(req->raw_payload))
		return;



	push_guard_status_update("node_join: complete", 100, "IDLE");
}

static void configure_foreigner(const struct hive_bootstrap_request *req)
{
	(void)req;
	push_guard_status_update("add_foreigner: processing", 50,
				 "OP_PENDING");
	fprintf(stdout, "bootstrap: add_foreigner request received\n");

	struct hive_storage_node local_node_join;

	if (!prepare_local_node(&local_node_join))
		return;

	push_guard_status_update("add_foreigner: complete", 100, "IDLE");
}

static bool dispatch_bootstrap_request(const struct hive_bootstrap_request *req,
				       const char **ok_message_out)
{
	if (!req)
		return false;
	if (ok_message_out)
		*ok_message_out = NULL;

	if (is_invalid_node_join_request(req)) {
		set_invalid_node_join_state();
		if (ok_message_out)
			*ok_message_out = configure_status();
		return true;
	}

	if (is_cluster_command(req->command)) {
		enum bootstrap_request_type prev = g_pending_request_type;

		g_pending_request_type = BOOTSTRAP_REQ_CLUSTER;
		configure_cluster(req);
		g_pending_request_type = prev;
		if (ok_message_out)
			*ok_message_out = configure_status();
		return true;
	}
	if (is_node_command(req->command)) {
		enum bootstrap_request_type prev = g_pending_request_type;

		g_pending_request_type = BOOTSTRAP_REQ_NODE_JOIN;
		configure_node(req);
		g_pending_request_type = prev;
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

static void set_invalid_node_join_state(void)
{
	push_guard_status_update(
		"Cannot add a node to a cluster that does not exist yet",
		100, "invalid_op");
	safe_copy(hbc.cluster_state, sizeof(hbc.cluster_state), "invalid_op");
}

static bool process_client(int fd)
{
	char buf[HIVE_BOOTSTRAP_MSG_MAX];
	struct hive_bootstrap_request req;
	const char *ok_message = NULL;

	if (!hive_common_sock_read_json_request(fd, buf, sizeof(buf))) {
		hive_common_sock_respond(fd, "ERR invalid payload\n");
		return false;
	}
	fprintf(stdout, "bootstrap_request: %s\n", buf);
	fflush(stdout);
	if (!parse_bootstrap_request(buf, &req)) {
		hive_common_sock_respond(fd, "ERR invalid JSON\n");
		return false;
	}
	if (!dispatch_bootstrap_request(&req, &ok_message)) {
		hive_common_sock_respond(fd, "ERR unknown command\n");
		return false;
	}

	if (ok_message && *ok_message) {
		hive_common_sock_respond(fd, "OK ");
		hive_common_sock_respond(fd, ok_message);
		hive_common_sock_respond(fd, "\n");
	} else {
		hive_common_sock_respond(fd, "OK\n");
	}
	return true;
}

int hive_bootstrap_sock_run(const char *socket_path)
{
	struct hive_common_sock_params params = {
		.path = socket_path,
		.default_path = HIVE_BOOTSTRAP_SOCK_PATH,
		.backlog = HIVE_BOOTSTRAP_BACKLOG,
		.mode = 0666,
		.resolved_path = g_socket_path,
		.resolved_path_len = sizeof(g_socket_path),
	};
	int listener = hive_common_sock_create_listener(&params);
	if (listener < 0)
		return -1;

	for (;;) {
		int client = accept(listener, NULL, NULL);

		if (client < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		process_client(client);
		close(client);
	}

	hive_common_sock_destroy_listener(listener,
					  g_socket_path[0]
						  ? g_socket_path
						  : HIVE_BOOTSTRAP_SOCK_PATH);
	return -1;
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
	char token_buf[260];
	char ts_buf[128];
	char cluster_id_buf[32];
	char node_id_buf[32];
	char hive_version_buf[32];
	char hive_patch_buf[32];
	char patch_str[8];
	struct stat st;
	const char *config_state = json_escape_string(g_config_state,
						      state_buf,
						      sizeof(state_buf));
	const char *config_msg = json_escape_string(g_status_message,
						    msg_buf,
						    sizeof(msg_buf));
	unsigned int percent = g_status_percent;
	char progress_text[8];
	const char *cluster_id_field;
	const char *node_id_field;
	const char *token_field;
	const char *ts_field;
	const char *hive_version_field;
	const char *hive_patch_field;
	const char *pub_key_field = "null";
	const char *status_text = (config_state && *config_state)
		? config_state : "IDLE";
	int version = 0;
	int patch = 0;

	hifs_parse_version(&version, &patch);
	snprintf(patch_str, sizeof(patch_str), "%04d", patch);
	hive_version_field =
		json_string_or_null(HIFS_VERSION,
				    hive_version_buf, sizeof(hive_version_buf));
	hive_patch_field =
		json_string_or_null(patch_str,
				    hive_patch_buf, sizeof(hive_patch_buf));

	if (percent > 100)
		percent = 100;
	snprintf(progress_text, sizeof(progress_text), "%u%%", percent);
	cluster_id_field = json_number_or_null(cluster_id_buf,
					       sizeof(cluster_id_buf),
					       hbc.cluster_id);
	node_id_field = json_number_or_null(node_id_buf,
					    sizeof(node_id_buf),
					    hbc.storage_node_id);
	token_field = json_string_or_null(hbc.bootstrap_token,
					  token_buf, sizeof(token_buf));
	ts_field = json_string_or_null(hbc.first_boot_ts,
				       ts_buf, sizeof(ts_buf));

	if (stat(g_socket_path, &st) != 0) {
		snprintf(status_buf, sizeof(status_buf),
			 "{\"ok\":false,"
			 "\"command\":\"status\","
			 "\"status\":\"IN_ERROR\","
			 "\"config_status\":\"IN_ERROR\","
			 "\"config_progress\":\"0%%\","
			 "\"config_msg\":\"NO BOOTSTRAPS\","
			 "\"cluster_state\":\"unknown\","
			 "\"database_state\":\"unknown\","
			 "\"kv_state\":\"unknown\","
			 "\"cont1_state\":\"unknown\","
			 "\"cont2_state\":\"unknown\","
			 "\"node_id\":null,"
			 "\"cluster_id\":null,"
			 "\"min_nodes_req\":0,"
			 "\"bootstrap_token\":null,"
			 "\"first_boot_ts\":null,"
			 "\"hive_version\":%s,"
			 "\"hive_patch_level\":%s,"
			 "\"pub_key\":%s}",
			 hive_version_field,
			 hive_patch_field,
			 pub_key_field);
		return status_buf;
	}

	snprintf(status_buf, sizeof(status_buf),
		 "{\"ok\":true,"
		 "\"command\":\"status\","
		 "\"status\":\"%s\","
		 "\"config_status\":\"%s\","
		 "\"config_progress\":\"%s\","
		 "\"config_msg\":\"%s\","
		 "\"node_id\":%s,"
		 "\"cluster_id\":%s,"
		 "\"cluster_state\":\"%s\","
		 "\"database_state\":\"%s\","
		 "\"kv_state\":\"%s\","
		 "\"cont1_state\":\"%s\","
		 "\"cont2_state\":\"%s\","
		 "\"min_nodes_req\":%llu,"
		 "\"bootstrap_token\":%s,"
		 "\"first_boot_ts\":%s,"
		 "\"hive_version\":%s,"
		 "\"hive_patch_level\":%s,"
		 "\"pub_key\":%s}",
		 status_text,
		 config_state,
		 progress_text,
		 config_msg,
		 node_id_field,
		 cluster_id_field,
		 state_or_unknown(hbc.cluster_state),
		 state_or_unknown(hbc.database_state),
		 state_or_unknown(hbc.kv_state),
		 state_or_unknown(hbc.cont1_state),
		 state_or_unknown(hbc.cont2_state),
		 (unsigned long long)hbc.min_nodes_req,
		 token_field,
		 ts_field,
		 hive_version_field,
		 hive_patch_field,
		 pub_key_field);
	fprintf(stdout, "bootstrap_status: %s\n", status_buf);
	fflush(stdout);
	return status_buf;
}

static bool parse_bootstrap_request(const char *json,
				    struct hive_bootstrap_request *req)
{
	if (!json || !req)
		return false;
	memset(req, 0, sizeof(*req));
	safe_copy(req->raw_payload, sizeof(req->raw_payload), json);

    printf("Json: %s\n", json);
	parse_json_string_value(json, "command",
				req->command, sizeof(req->command));
	parse_json_u64_value(json, "cluster_id", &req->cluster_id);
	parse_json_u64_value(json, "clister_id", &req->cluster_id);
	parse_json_string_value(json, "cluster_state",
				req->cluster_state, sizeof(req->cluster_state));
	parse_json_string_value(json, "cluster_name",
				req->cluster_name, sizeof(req->cluster_name));
	parse_json_string_value(json, "cluster_desc",
				req->cluster_desc, sizeof(req->cluster_desc));
	parse_json_u64_value(json, "node_id", &req->node_id);
	parse_json_string_value(json, "node_name",
				req->node_name, sizeof(req->node_name));
	parse_json_string_value(json, "join_token",
				req->join_token, sizeof(req->join_token));
	uint64_t min_nodes_val = 0;
	if (parse_json_u64_value(json, "min_nodes_req", &min_nodes_val))
		req->min_nodes_req = (uint16_t)min_nodes_val;

	return req->command[0] != '\0';
}
