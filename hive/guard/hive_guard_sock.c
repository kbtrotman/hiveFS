/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

#include "hive_guard_sock.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <poll.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "hive_guard_sql.h"
#include "hive_guard_raft.h"
#include "../common/hive_common_sock.h"

static pthread_t g_guard_sock_thread;
static volatile int g_guard_sock_stop = 0;
static bool g_guard_sock_thread_running = false;
static int g_guard_sock_listener = -1;
static char g_guard_sock_path[sizeof(((struct sockaddr_un *)0)->sun_path)] =
	HIVE_GUARD_SOCK_PATH;

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

static const char *json_number_or_null(char *buf, size_t buf_sz, uint64_t value)
{
	if (!buf || buf_sz == 0)
		return "null";
	if (value == 0) {
		buf[0] = '\0';
		return "null";
	}
	snprintf(buf, buf_sz, "%llu", (unsigned long long)value);
	return buf;
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

static const char *default_state(const char *state, const char *fallback)
{
	return (state && *state) ? state : fallback;
}

int hive_guard_distribute_node_tokens(const struct hive_guard_join_context *ctx)
{
	char *token = NULL;
	char *pub_key = NULL;
	char *pub_field = NULL;
	char *json = NULL;
	char *uid_sql = NULL;
	MYSQL_RES *res = NULL;
	int rc = -1;

	if (!ctx || !ctx->machine_uid || !*ctx->machine_uid || !sqldb.conn)
		return -1;

	size_t uid_len = strlen(ctx->machine_uid);
	size_t uid_sql_len = uid_len * 2 + 1;

	uid_sql = malloc(uid_sql_len);
	if (!uid_sql)
		goto cleanup;
	mysql_real_escape_string(sqldb.conn, uid_sql, ctx->machine_uid,
				 (unsigned long)uid_len);

	char sql[512];
	int written = snprintf(sql, sizeof(sql),
			       SQL_HOST_TOKEN_SELECT_NODE_JOIN_LATEST,
			       uid_sql);
	if (written < 0 || (size_t)written >= sizeof(sql))
		goto cleanup;
	if (mysql_real_query(sqldb.conn, sql, (unsigned long)written) != 0) {
		fprintf(stderr,
			"hive_guard_sock: failed to fetch node join token: %s\n",
			mysql_error(sqldb.conn));
		goto cleanup;
	}
	res = mysql_store_result(sqldb.conn);
	if (!res) {
		if (mysql_field_count(sqldb.conn) != 0)
			fprintf(stderr,
				"hive_guard_sock: node token store_result failed: %s\n",
				mysql_error(sqldb.conn));
		goto cleanup;
	}
	MYSQL_ROW row = mysql_fetch_row(res);
	if (row && row[0])
		token = strdup(row[0]);
	mysql_free_result(res);
	res = NULL;
	if (!token) {
		fprintf(stderr,
			"hive_guard_sock: no valid join token found for machine %s\n",
			ctx->machine_uid);
		goto cleanup;
	}

	written = snprintf(sql, sizeof(sql),
			   SQL_HOST_AUTH_SELECT_APPROVED_PUBKEY,
			   uid_sql);
	if (written < 0 || (size_t)written >= sizeof(sql))
		goto cleanup;
	if (mysql_real_query(sqldb.conn, sql, (unsigned long)written) != 0) {
		fprintf(stderr,
			"hive_guard_sock: failed to fetch node public key: %s\n",
			mysql_error(sqldb.conn));
		goto cleanup;
	}
	res = mysql_store_result(sqldb.conn);
	if (!res) {
		if (mysql_field_count(sqldb.conn) != 0)
			fprintf(stderr,
				"hive_guard_sock: node pubkey store_result failed: %s\n",
				mysql_error(sqldb.conn));
		goto cleanup;
	}
	row = mysql_fetch_row(res);
	if (row && row[0])
		pub_key = strdup(row[0]);
	mysql_free_result(res);
	res = NULL;
	if (!pub_key) {
		fprintf(stderr,
			"hive_guard_sock: no public key found for machine %s\n",
			ctx->machine_uid);
		goto cleanup;
	}

	size_t pub_buf_len = strlen(pub_key) * 2 + 3;
	if (pub_buf_len < 64)
		pub_buf_len = 64;
	pub_field = malloc(pub_buf_len);
	if (!pub_field)
		goto cleanup;

	char node_id_buf[32];
	char cluster_id_buf[32];
	char token_field[256];
	char ts_field[128];
	char config_status_field[64];
	char config_progress_field[64];
	char config_msg_field[256];
	char hive_version_field[64];
	char hive_patch_field[64];
	char cluster_state_buf[32];
	char database_state_buf[32];
	char kv_state_buf[32];
	char cont1_state_buf[32];
	char cont2_state_buf[32];

	const char *node_id_json =
		json_number_or_null(node_id_buf, sizeof(node_id_buf),
				    ctx->node_id);
	const char *cluster_id_json =
		json_number_or_null(cluster_id_buf, sizeof(cluster_id_buf),
				    ctx->cluster_id);
	const char *token_json =
		json_string_or_null(token, token_field, sizeof(token_field));
	const char *pub_json =
		json_string_or_null(pub_key, pub_field, pub_buf_len);
	const char *first_boot_json = json_string_or_null(ctx->first_boot_ts,
							  ts_field,
							  sizeof(ts_field));
	const char *config_status_json = json_string_or_null(
		ctx->config_status, config_status_field,
		sizeof(config_status_field));
	const char *config_progress_json = json_string_or_null(
		ctx->config_progress, config_progress_field,
		sizeof(config_progress_field));
	const char *config_msg_json = json_string_or_null(
		ctx->config_message, config_msg_field, sizeof(config_msg_field));
	const char *hive_version_json = json_string_or_null(
		ctx->hive_version, hive_version_field,
		sizeof(hive_version_field));
	const char *hive_patch_json = json_string_or_null(
		ctx->hive_patch_level, hive_patch_field,
		sizeof(hive_patch_field));

	json_escape_string(default_state(ctx->cluster_state, "unconfigured"),
			   cluster_state_buf, sizeof(cluster_state_buf));
	json_escape_string(default_state(ctx->database_state, "configured"),
			   database_state_buf, sizeof(database_state_buf));
	json_escape_string(default_state(ctx->kv_state, "configured"),
			   kv_state_buf, sizeof(kv_state_buf));
	json_escape_string(default_state(ctx->cont1_state, "configured"),
			   cont1_state_buf, sizeof(cont1_state_buf));
	json_escape_string(default_state(ctx->cont2_state, "configured"),
			   cont2_state_buf, sizeof(cont2_state_buf));

	size_t json_cap = 1024 +
			  strlen(pub_json) + strlen(token_json) +
			  strlen(config_status_json) +
			  strlen(config_progress_json) +
			  strlen(config_msg_json) +
			  strlen(hive_version_json) +
			  strlen(hive_patch_json);
	json = malloc(json_cap);
	if (!json)
		goto cleanup;

	int json_len = snprintf(
		json, json_cap,
		"{\"command\":\"join_sec\","
		"\"hive_version\":%s,"
		"\"hive_patch_level\":%s,"
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
		"\"config_status\":%s,"
		"\"config_progress\":%s,"
		"\"config_msg\":%s,"
		"\"pub_key\":%s}\n",
		hive_version_json,
		hive_patch_json,
		node_id_json,
		cluster_id_json,
		cluster_state_buf,
		database_state_buf,
		kv_state_buf,
		cont1_state_buf,
		cont2_state_buf,
		(unsigned long long)ctx->min_nodes_required,
		token_json,
		first_boot_json,
		config_status_json,
		config_progress_json,
		config_msg_json,
		pub_json);
	if (json_len < 0 || (size_t)json_len >= json_cap)
		goto cleanup;

	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0)
		goto cleanup;

	struct sockaddr_un addr;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, HIVE_GUARD_SOCK_PATH,
		sizeof(addr.sun_path) - 1);
	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		fprintf(stderr,
			"hive_guard_sock: failed to connect to control socket %s: %s\n",
			addr.sun_path, strerror(errno));
		close(fd);
		goto cleanup;
	}

	size_t remaining = (size_t)json_len;
	const char *cursor = json;
	while (remaining > 0) {
		ssize_t wr = send(fd, cursor, remaining, 0);
		if (wr < 0) {
			if (errno == EINTR)
				continue;
			fprintf(stderr,
				"hive_guard_sock: failed to send join_sec payload: %s\n",
				strerror(errno));
			close(fd);
			goto cleanup;
		}
		if (wr == 0) {
			fprintf(stderr,
				"hive_guard_sock: control socket closed during send\n");
			close(fd);
			goto cleanup;
		}
		cursor += wr;
		remaining -= (size_t)wr;
	}
	close(fd);
	rc = 0;

cleanup:
	if (res)
		mysql_free_result(res);
	free(json);
	free(pub_field);
	free(pub_key);
	free(token);
	free(uid_sql);
	return rc;
}

static inline const char *guard_sock_skip_ws(const char *p)
{
	while (p && *p && isspace((unsigned char)*p))
		++p;
	return p;
}

static const char *guard_sock_find_key(const char *json, const char *key,
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

static bool guard_sock_parse_string_value(const char *json, const char *key,
					  char *out, size_t out_len)
{
	size_t needle_len = 0;
	const char *p = guard_sock_find_key(json, key, &needle_len);

	if (!out || out_len == 0) {
		return false;
	}
	if (!p) {
		out[0] = '\0';
		return false;
	}
	p += needle_len;
	p = guard_sock_skip_ws(p);
	if (*p != ':') {
		out[0] = '\0';
		return false;
	}
	p = guard_sock_skip_ws(p + 1);
	if (*p != '"') {
		out[0] = '\0';
		return false;
	}
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

static bool guard_sock_parse_u64_value(const char *json, const char *key,
				       uint64_t *out)
{
	size_t needle_len = 0;
	const char *p = guard_sock_find_key(json, key, &needle_len);

	if (!p || !out)
		return false;
	p += needle_len;
	p = guard_sock_skip_ws(p);
	if (*p != ':')
		return false;
	p = guard_sock_skip_ws(p + 1);

	errno = 0;
	char *end = NULL;
	unsigned long long v = strtoull(p, &end, 10);
	if (p == end || errno == ERANGE)
		return false;
	*out = (uint64_t)v;
	return true;
}

static bool guard_sock_parse_join_sec(const char *json,
				      struct hive_guard_sock_join_sec *out)
{
	char command[32];

	if (!json || !out)
		return false;
	memset(out, 0, sizeof(*out));
	if (!guard_sock_parse_string_value(json, "command",
					   command, sizeof(command)))
		return false;
	if (strcmp(command, "join_sec") != 0)
		return false;
	if (!guard_sock_parse_u64_value(json, "cluster_id",
					&out->cluster_id))
		return false;
	if (!guard_sock_parse_u64_value(json, "node_id", &out->node_id))
		return false;
	guard_sock_parse_u64_value(json, "min_nodes_req",
				   &out->min_nodes_req);
	guard_sock_parse_string_value(json, "cluster_state",
				      out->cluster_state,
				      sizeof(out->cluster_state));
	guard_sock_parse_string_value(json, "database_state",
				      out->database_state,
				      sizeof(out->database_state));
	guard_sock_parse_string_value(json, "kv_state",
				      out->kv_state,
				      sizeof(out->kv_state));
	guard_sock_parse_string_value(json, "cont1_state",
				      out->cont1_state,
				      sizeof(out->cont1_state));
	guard_sock_parse_string_value(json, "cont2_state",
				      out->cont2_state,
				      sizeof(out->cont2_state));
	guard_sock_parse_string_value(json, "bootstrap_token",
				      out->bootstrap_token,
				      sizeof(out->bootstrap_token));
	guard_sock_parse_string_value(json, "first_boot_ts",
				      out->first_boot_ts,
				      sizeof(out->first_boot_ts));
	guard_sock_parse_string_value(json, "config_status",
				      out->config_status,
				      sizeof(out->config_status));
	guard_sock_parse_string_value(json, "config_progress",
				      out->config_progress,
				      sizeof(out->config_progress));
	guard_sock_parse_string_value(json, "config_msg",
				      out->config_msg,
				      sizeof(out->config_msg));
	guard_sock_parse_string_value(json, "hive_version",
				      out->hive_version,
				      sizeof(out->hive_version));
	guard_sock_parse_string_value(json, "hive_patch_level",
				      out->hive_patch_level,
				      sizeof(out->hive_patch_level));
	guard_sock_parse_string_value(json, "pub_key",
				      out->pub_key,
				      sizeof(out->pub_key));
	return true;
}

static int guard_sock_handle_join_sec(const struct hive_guard_sock_join_sec *req)
{
	struct hive_guard_join_context ctx = {
		.cluster_id = req->cluster_id,
		.node_id = req->node_id,
		.machine_uid = NULL,
		.cluster_state = req->cluster_state[0]
			? req->cluster_state : NULL,
		.database_state = req->database_state[0]
			? req->database_state : NULL,
		.kv_state = req->kv_state[0] ? req->kv_state : NULL,
		.cont1_state = req->cont1_state[0] ? req->cont1_state : NULL,
		.cont2_state = req->cont2_state[0] ? req->cont2_state : NULL,
		.min_nodes_required = req->min_nodes_req,
		.first_boot_ts = req->first_boot_ts[0]
			? req->first_boot_ts : NULL,
		.config_status = req->config_status[0]
			? req->config_status : NULL,
		.config_progress = req->config_progress[0]
			? req->config_progress : NULL,
		.config_message = req->config_msg[0]
			? req->config_msg : NULL,
		.hive_version = req->hive_version[0]
			? req->hive_version : NULL,
		.hive_patch_level = req->hive_patch_level[0]
			? req->hive_patch_level : NULL,
	};

	/* Future commands will hook into Raft, for now log receipt. */
	fprintf(stdout,
		"hive_guard_sock: join_sec cluster=%llu node=%llu "
		"state=%s progress=%s\n",
		(unsigned long long)ctx.cluster_id,
		(unsigned long long)ctx.node_id,
		ctx.cluster_state ? ctx.cluster_state : "unknown",
		ctx.config_progress ? ctx.config_progress : "n/a");
	fflush(stdout);
	(void)ctx;
	return 0;
}

static bool guard_sock_dispatch_request(const char *json)
{
	struct hive_guard_sock_join_sec join_req;

	if (guard_sock_parse_join_sec(json, &join_req))
		return guard_sock_handle_join_sec(&join_req) == 0;
	return false;
}

static void guard_sock_process_client(int fd)
{
	char buf[HIVE_GUARD_SOCK_MSG_MAX];

	if (!hive_common_sock_read_json_request(fd, buf, sizeof(buf))) {
		hive_common_sock_respond(fd, "ERR invalid payload\n");
		return;
	}

	if (!guard_sock_dispatch_request(buf)) {
		hive_common_sock_respond(fd, "ERR unknown command\n");
		return;
	}
	hive_common_sock_respond(fd, "OK\n");
}

static void guard_sock_make_nonblocking(int fd)
{
	int flags = fcntl(fd, F_GETFL, 0);

	if (flags >= 0)
		fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void *hive_guard_sock_thread_main(void *arg)
{
	(void)arg;
	struct hive_common_sock_params params = {
		.default_path = HIVE_GUARD_SOCK_PATH,
		.path = NULL,
		.backlog = HIVE_GUARD_SOCK_BACKLOG,
		.mode = 0660,
		.resolved_path = g_guard_sock_path,
		.resolved_path_len = sizeof(g_guard_sock_path),
	};
	int listener = hive_common_sock_create_listener(&params);
	if (listener < 0) {
		fprintf(stderr,
			"hive_guard_sock: failed to create listener: %s\n",
			strerror(errno));
		return NULL;
	}

	g_guard_sock_listener = listener;
	guard_sock_make_nonblocking(listener);

	struct pollfd pfd = {
		.fd = listener,
		.events = POLLIN,
	};

	while (!g_guard_sock_stop) {
		pfd.revents = 0;
		int rc = poll(&pfd, 1, 250);

		if (rc < 0) {
			if (errno == EINTR)
				continue;
			fprintf(stderr,
				"hive_guard_sock: poll failed: %s\n",
				strerror(errno));
			break;
		}
		if (rc == 0)
			continue;
		if (pfd.revents & POLLIN) {
			for (;;) {
				int client = accept(listener, NULL, NULL);
				if (client < 0) {
					if (errno == EINTR)
						continue;
					if (errno == EAGAIN ||
					    errno == EWOULDBLOCK)
						break;
					fprintf(stderr,
						"hive_guard_sock: accept failed: %s\n",
						strerror(errno));
					g_guard_sock_stop = 1;
					break;
				}
				guard_sock_process_client(client);
				close(client);
			}
		}
		if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))
			break;
	}

	hive_common_sock_destroy_listener(listener, g_guard_sock_path);
	g_guard_sock_listener = -1;
	return NULL;
}

int hive_guard_sock_start(void)
{
	if (g_guard_sock_thread_running)
		return 0;

	g_guard_sock_stop = 0;
	int rc = pthread_create(&g_guard_sock_thread,
				NULL,
				hive_guard_sock_thread_main,
				NULL);
	if (rc != 0) {
		errno = rc;
		return -1;
	}
	g_guard_sock_thread_running = true;
	return 0;
}

void hive_guard_sock_stop(void)
{
	if (!g_guard_sock_thread_running)
		return;

	g_guard_sock_stop = 1;
	pthread_join(g_guard_sock_thread, NULL);
	g_guard_sock_thread_running = false;
}
