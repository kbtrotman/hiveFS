/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */


#include <sys/un.h>
#include <sys/wait.h>

#include "hive_bootstrap.h"
#include "hive_bootstrap_sock.h"
#include "../guard/hive_guard.h"
#include "../guard/hive_guard_sql.h"
#include "../common/hive_common_sql.h"
#include <openssl/asn1.h>
#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>


#define HIVE_BOOTSTRAP_SOCK_PATH "/run/hive_bootstrap.sock"
#define HIVE_GUARD_CONTROL_SOCK_PATH "/run/hive_guard.sock"
#define HIVE_BOOTSTRAP_MSG_MAX   4096
#define HIVE_BOOTSTRAP_BACKLOG   4
#define NODE_CERT_KEY_BITS       2048
#define NODE_CERT_VALID_DAYS     365

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
};

static char g_status_message[64] = "IDLE";
static unsigned int g_status_percent = 0;
static char g_config_state[16] = "IDLE";
static char g_socket_path[sizeof(((struct sockaddr_un *)0)->sun_path)] =
	HIVE_BOOTSTRAP_SOCK_PATH;
static const char *configure_status(void);

enum bootstrap_request_type {
	BOOTSTRAP_REQ_UNKNOWN = 0,
	BOOTSTRAP_REQ_CLUSTER,
	BOOTSTRAP_REQ_NODE_JOIN,
};

static enum bootstrap_request_type g_pending_request_type =
	BOOTSTRAP_REQ_UNKNOWN;

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

enum {
	HIVE_NODE_NAME_LEN = sizeof(((struct hive_storage_node *)0)->name),
	HIVE_NODE_ADDR_LEN = sizeof(((struct hive_storage_node *)0)->address),
	HIVE_NODE_UID_LEN = sizeof(((struct hive_storage_node *)0)->uid),
	HIVE_NODE_SERIAL_LEN = sizeof(((struct hive_storage_node *)0)->serial),
};

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

	if (!bootstrap_run_command(check_enabled)) {
		if (!bootstrap_run_command(enable_cmd))
			return false;
	}
	(void)bootstrap_run_command(restart_policy);
	(void)bootstrap_run_command(start_cmd);
	return bootstrap_run_command(status_cmd);
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

		if (len > 0)
			(void)sethostname(hbc.storage_node_name, len);
		safe_copy(node->name, sizeof(node->name), hbc.storage_node_name);
	} else if (!hbc.storage_node_name[0]) {
		safe_copy(hbc.storage_node_name,
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

static uint64_t lookup_existing_node_id(const char *uid, const char *serial)
{
	if (!sqldb.conn)
		return 0;

	char uid_sql[HIVE_NODE_UID_LEN * 2 + 1];
	char serial_sql[HIVE_NODE_SERIAL_LEN * 2 + 1];
	const char *uid_src = uid ? uid : "";
	const char *serial_src = serial ? serial : "";
	unsigned long uid_len = (unsigned long)strlen(uid_src);
	unsigned long serial_len = (unsigned long)strlen(serial_src);

	mysql_real_escape_string(sqldb.conn, uid_sql, uid_src, uid_len);
	mysql_real_escape_string(sqldb.conn, serial_sql, serial_src, serial_len);

	char sql[512];
	int written = snprintf(sql, sizeof(sql),
			       SQL_STORAGE_NODE_ID_BY_UID_OR_SERIAL,
			       uid_sql, serial_sql);
	if (written < 0 || (size_t)written >= sizeof(sql))
		return 0;

	if (mysql_real_query(sqldb.conn, sql, (unsigned long)written) != 0) {
		fprintf(stderr, "bootstrap: node lookup failed: %s\n",
			mysql_error(sqldb.conn));
		return 0;
	}

	MYSQL_RES *res = mysql_store_result(sqldb.conn);
	if (!res) {
		if (mysql_field_count(sqldb.conn) != 0)
			fprintf(stderr, "bootstrap: node lookup store_result failed: %s\n",
				mysql_error(sqldb.conn));
		return 0;
	}

	uint64_t node_id = 0;
	MYSQL_ROW row = mysql_fetch_row(res);

	if (row && row[0])
		node_id = (uint64_t)strtoull(row[0], NULL, 10);
	mysql_free_result(res);
	return node_id;
}

int distribute_node_tokens_to_existing_nodes(uint64_t cluster_id,
					     uint64_t node_id,
					     const char *machine_uid)
{
	if (!sqldb.conn || !machine_uid || !*machine_uid)
		return -1;

	int rc = -1;
	char *token = NULL;
	char *pub_key = NULL;
	char *pub_field = NULL;
	char *json = NULL;
	MYSQL_RES *res = NULL;
	char uid_sql[sizeof(hbc.storage_node_uid) * 2 + 1];
	unsigned long uid_len = (unsigned long)strlen(machine_uid);

	mysql_real_escape_string(sqldb.conn, uid_sql, machine_uid, uid_len);

	char sql[512];
	int written = snprintf(
		sql, sizeof(sql),
		"SELECT token FROM host_tokens "
		"WHERE machine_uid='%s' AND t_type='node_join' "
		"AND used=0 AND revoked=0 AND expired=0 "
		"ORDER BY issued_at DESC LIMIT 1",
		uid_sql);
	if (written < 0 || (size_t)written >= sizeof(sql))
		goto cleanup;
	if (mysql_real_query(sqldb.conn, sql, (unsigned long)written) != 0) {
		fprintf(stderr,
			"bootstrap: failed to fetch node join token: %s\n",
			mysql_error(sqldb.conn));
		goto cleanup;
	}
	res = mysql_store_result(sqldb.conn);
	if (!res) {
		if (mysql_field_count(sqldb.conn) != 0)
			fprintf(stderr,
				"bootstrap: node token store_result failed: %s\n",
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
			"bootstrap: no valid join token found for machine %s\n",
			machine_uid);
		goto cleanup;
	}

	written = snprintf(
		sql, sizeof(sql),
		"SELECT pub_key_pem FROM host_auth "
		"WHERE machine_uid='%s' AND status='approved' "
		"ORDER BY issued_on DESC LIMIT 1",
		uid_sql);
	if (written < 0 || (size_t)written >= sizeof(sql))
		goto cleanup;
	if (mysql_real_query(sqldb.conn, sql, (unsigned long)written) != 0) {
		fprintf(stderr,
			"bootstrap: failed to fetch node public key: %s\n",
			mysql_error(sqldb.conn));
		goto cleanup;
	}
	res = mysql_store_result(sqldb.conn);
	if (!res) {
		if (mysql_field_count(sqldb.conn) != 0)
			fprintf(stderr,
				"bootstrap: node pubkey store_result failed: %s\n",
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
			"bootstrap: no public key found for machine %s\n",
			machine_uid);
		goto cleanup;
	}

	size_t pub_buf_len = strlen(pub_key) * 2 + 3;
	if (pub_buf_len < 64)
		pub_buf_len = 64;
	pub_field = malloc(pub_buf_len);
	if (!pub_field)
		goto cleanup;

	char token_field[256];
	char ts_field[128];
	char config_status_field[64];
	char config_progress_field[32];
	char config_msg_field[128];
	char node_id_buf[32];
	char cluster_id_buf[32];
	char cluster_state_buf[32];
	char database_state_buf[32];
	char kv_state_buf[32];
	char cont1_state_buf[32];
	char cont2_state_buf[32];

	const char *node_id_json =
		json_number_or_null(node_id_buf, sizeof(node_id_buf), node_id);
	const char *cluster_id_json = json_number_or_null(
		cluster_id_buf, sizeof(cluster_id_buf), cluster_id);
	const char *token_json =
		json_string_or_null(token, token_field, sizeof(token_field));
	const char *pub_json =
		json_string_or_null(pub_key, pub_field, pub_buf_len);
	const char *first_boot_json = json_string_or_null(
		hbc.first_boot_ts, ts_field, sizeof(ts_field));
	const char *config_status_json = json_string_or_null(
		g_config_state, config_status_field, sizeof(config_status_field));
	char progress_pct[8];
	unsigned int percent = g_status_percent;
	if (percent > 100)
		percent = 100;
	snprintf(progress_pct, sizeof(progress_pct), "%u%%", percent);
	const char *config_progress_json = json_string_or_null(
		progress_pct, config_progress_field, sizeof(config_progress_field));
	const char *config_msg_json = json_string_or_null(
		g_status_message, config_msg_field, sizeof(config_msg_field));

	const char *cluster_state =
		hbc.cluster_state[0] ? hbc.cluster_state : "unconfigured";
	const char *database_state =
		hbc.database_state[0] ? hbc.database_state : "configured";
	const char *kv_state =
		hbc.kv_state[0] ? hbc.kv_state : "configured";
	const char *cont1_state =
		hbc.cont1_state[0] ? hbc.cont1_state : "configured";
	const char *cont2_state =
		hbc.cont2_state[0] ? hbc.cont2_state : "configured";

	json_escape_string(cluster_state, cluster_state_buf,
			   sizeof(cluster_state_buf));
	json_escape_string(database_state, database_state_buf,
			   sizeof(database_state_buf));
	json_escape_string(kv_state, kv_state_buf, sizeof(kv_state_buf));
	json_escape_string(cont1_state, cont1_state_buf,
			   sizeof(cont1_state_buf));
	json_escape_string(cont2_state, cont2_state_buf,
			   sizeof(cont2_state_buf));

	size_t json_cap = 1024 + strlen(pub_json) + strlen(token_json) +
			  strlen(config_status_json) +
			  strlen(config_progress_json) +
			  strlen(config_msg_json);
	json = malloc(json_cap);
	if (!json)
		goto cleanup;
	int json_len = snprintf(
		json, json_cap,
		"{\"command\":\"join_sec\","
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
		node_id_json,
		cluster_id_json,
		cluster_state_buf,
		database_state_buf,
		kv_state_buf,
		cont1_state_buf,
		cont2_state_buf,
		(unsigned long long)hbc.min_nodes_req,
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
	safe_copy(addr.sun_path, sizeof(addr.sun_path),
		  HIVE_GUARD_CONTROL_SOCK_PATH);
	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		fprintf(stderr,
			"bootstrap: failed to connect to hive_guard control socket %s: %s\n",
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
				"bootstrap: failed to send join_sec payload: %s\n",
				strerror(errno));
			close(fd);
			goto cleanup;
		}
		if (wr == 0) {
			fprintf(stderr,
				"bootstrap: hive_guard control socket closed during send\n");
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
	return rc;
}

static uint64_t lookup_highest_node_id(void)
{
	if (!sqldb.conn)
		return 0;

	const char sql[] = SQL_STORAGE_NODE_MAX_ID_SELECT;

	if (mysql_real_query(sqldb.conn, sql, (unsigned long)strlen(sql)) != 0) {
		fprintf(stderr, "bootstrap: highest node lookup failed: %s\n",
			mysql_error(sqldb.conn));
		return 0;
	}

	MYSQL_RES *res = mysql_store_result(sqldb.conn);

	if (!res) {
		if (mysql_field_count(sqldb.conn) != 0)
			fprintf(stderr,
				"bootstrap: highest node store_result failed: %s\n",
				mysql_error(sqldb.conn));
		return 0;
	}

	uint64_t max_id = 0;
	MYSQL_ROW row = mysql_fetch_row(res);

	if (row && row[0])
		max_id = (uint64_t)strtoull(row[0], NULL, 10);
	mysql_free_result(res);
	return max_id;
}

static bool update_storage_node_row(uint64_t cluster_id,
				    uint64_t node_id,
				    const struct hive_storage_node *local,
				    int version,
				    int patch)
{
	if (!sqldb.conn || !local || node_id == 0)
		return false;

	char sql_query[MAX_QUERY_SIZE];
	char name_sql[HIVE_NODE_NAME_LEN * 2 + 1];
	char addr_sql[HIVE_NODE_ADDR_LEN * 2 + 1];
	char uid_sql[HIVE_NODE_UID_LEN * 2 + 1];
	char serial_sql[HIVE_NODE_SERIAL_LEN * 2 + 1];
	char cluster_buf[32];

	const char *name_src = local->name[0] ? local->name : "hive_guard";
	const char *addr_src = local->address[0] ? local->address : "127.0.0.1";
	const char *uid_src = local->uid[0] ? local->uid : hifs_get_machine_id();
	const char *serial_src = local->serial[0] ? local->serial : hifs_cached_host_serial();

	mysql_real_escape_string(sqldb.conn, name_sql, name_src,
				 (unsigned long)strlen(name_src));
	mysql_real_escape_string(sqldb.conn, addr_sql, addr_src,
				 (unsigned long)strlen(addr_src));
	mysql_real_escape_string(sqldb.conn, uid_sql, uid_src,
				 (unsigned long)strlen(uid_src));
	mysql_real_escape_string(sqldb.conn, serial_sql, serial_src,
				 (unsigned long)strlen(serial_src));
	if (cluster_id)
		snprintf(cluster_buf, sizeof(cluster_buf), "%llu",
			 (unsigned long long)cluster_id);
	else
		safe_copy(cluster_buf, sizeof(cluster_buf), "NULL");

	int written = snprintf(sql_query, sizeof(sql_query),
			       SQL_STORAGE_NODE_JOIN_UPDATE,
			       cluster_buf,
			       name_sql,
			       addr_sql,
			       uid_sql,
			       serial_sql,
			       (unsigned)local->guard_port,
			       (unsigned)(local->stripe_port ? local->stripe_port : local->guard_port),
			       version,
			       patch,
			       (unsigned long long)node_id);
	if (written < 0 || (size_t)written >= sizeof(sql_query))
		return false;

	if (mysql_real_query(sqldb.conn, sql_query, (unsigned long)written) != 0) {
		fprintf(stderr, "bootstrap: storage node metadata update failed: %s\n",
			mysql_error(sqldb.conn));
		return false;
	}

	MYSQL_RES *res = mysql_store_result(sqldb.conn);
	if (res) {
		mysql_free_result(res);
	} else if (mysql_field_count(sqldb.conn) != 0) {
		fprintf(stderr, "bootstrap: storage node metadata consume failure: %s\n",
			mysql_error(sqldb.conn));
		return false;
	}
	return true;
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

static void set_invalid_node_join_state(void)
{
	bootstrap_status_update(
		"Cannot add a node to a cluster that does not exist yet",
		100, "invalid_op");
	safe_copy(hbc.cluster_state, sizeof(hbc.cluster_state), "invalid_op");
}

static bool parse_bootstrap_request(const char *json,
				    struct hive_bootstrap_request *req)
{
	if (!json || !req)
		return false;
	memset(req, 0, sizeof(*req));

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

static bool bio_to_string(BIO *bio, char **out)
{
	if (!bio || !out)
		return false;

	char *data = NULL;
	long len = BIO_get_mem_data(bio, &data);

	if (len <= 0 || !data)
		return false;
	char *buf = malloc((size_t)len + 1);

	if (!buf)
		return false;
	memcpy(buf, data, (size_t)len);
	buf[len] = '\0';
	*out = buf;
	return true;
}

static bool generate_node_certificate(char **priv_pem_out, char **pub_pem_out)
{
	if (!priv_pem_out || !pub_pem_out)
		return false;
	*priv_pem_out = NULL;
	*pub_pem_out = NULL;

	bool ok = false;
	BIGNUM *bn = BN_new();
	RSA *rsa = RSA_new();
	EVP_PKEY *pkey = EVP_PKEY_new();
	X509 *x509 = X509_new();
	BIO *priv_bio = NULL;
	BIO *cert_bio = NULL;

	if (!bn || !rsa || !pkey || !x509)
		goto cleanup;
	if (!BN_set_word(bn, RSA_F4))
		goto cleanup;
	if (RSA_generate_key_ex(rsa, NODE_CERT_KEY_BITS, bn, NULL) != 1)
		goto cleanup;
	if (EVP_PKEY_assign_RSA(pkey, rsa) != 1)
		goto cleanup;
	rsa = NULL;

	X509_set_version(x509, 2);
	uint32_t serial = (uint32_t)(time(NULL) ^ (uint32_t)getpid());

	if (!serial)
		serial = 1;
	if (!ASN1_INTEGER_set(X509_get_serialNumber(x509), (long)serial))
		goto cleanup;
	if (!X509_gmtime_adj(X509_get_notBefore(x509), 0) ||
	    !X509_gmtime_adj(X509_get_notAfter(x509),
			     (long)NODE_CERT_VALID_DAYS * 24L * 3600L))
		goto cleanup;
	if (X509_set_pubkey(x509, pkey) != 1)
		goto cleanup;

	X509_NAME *name = X509_get_subject_name(x509);
	const char *cn = hbc.storage_node_name[0]
		? hbc.storage_node_name
		: hbc.storage_node_uid;

	if (!cn || !*cn)
		cn = "hivefs-node";
	if (X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
				       (const unsigned char *)cn,
				       -1, -1, 0) != 1)
		goto cleanup;
	if (hbc.storage_node_uid[0])
		X509_NAME_add_entry_by_txt(name, "UID", MBSTRING_ASC,
					   (const unsigned char *)
						   hbc.storage_node_uid,
					   -1, -1, 0);
	if (hbc.storage_node_serial[0])
		X509_NAME_add_entry_by_txt(name, "serialNumber",
					   MBSTRING_ASC,
					   (const unsigned char *)
						   hbc.storage_node_serial,
					   -1, -1, 0);
	if (hbc.storage_node_address[0])
		X509_NAME_add_entry_by_txt(name, "L", MBSTRING_ASC,
					   (const unsigned char *)
						   hbc.storage_node_address,
					   -1, -1, 0);
	if (X509_set_issuer_name(x509, name) != 1)
		goto cleanup;
	if (X509_sign(x509, pkey, EVP_sha256()) <= 0)
		goto cleanup;

	priv_bio = BIO_new(BIO_s_mem());
	cert_bio = BIO_new(BIO_s_mem());
	if (!priv_bio || !cert_bio)
		goto cleanup;
	if (!PEM_write_bio_PrivateKey(priv_bio, pkey, NULL, NULL, 0,
				      NULL, NULL))
		goto cleanup;
	if (!PEM_write_bio_X509(cert_bio, x509))
		goto cleanup;
	if (!bio_to_string(priv_bio, priv_pem_out) ||
	    !bio_to_string(cert_bio, pub_pem_out))
		goto cleanup;

	ok = true;

cleanup:
	BIO_free(cert_bio);
	BIO_free(priv_bio);
	X509_free(x509);
	EVP_PKEY_free(pkey);
	if (rsa)
		RSA_free(rsa);
	BN_free(bn);
	if (!ok) {
		free(*priv_pem_out);
		free(*pub_pem_out);
		*priv_pem_out = NULL;
		*pub_pem_out = NULL;
	}
	return ok;
}

int add_node_mtls_token(uint32_t node_id, const char *token)
{
	(void)node_id;
	(void)token;

	if (!sqldb.conn || !hbc.storage_node_uid[0])
		return -1;

	char *priv_pem = NULL;
	char *pub_pem = NULL;
	char *priv_sql = NULL;
	char *pub_sql = NULL;
	char *sql_query = NULL;
	int rc = -1;

	if (!generate_node_certificate(&priv_pem, &pub_pem)) {
		fprintf(stderr,
			"bootstrap: failed to generate node mTLS certificate\n");
		goto cleanup;
	}

	const char *serial_src = hbc.storage_node_serial[0]
		? hbc.storage_node_serial
		: hbc.storage_node_uid;
	const char *name_src = hbc.storage_node_name[0]
		? hbc.storage_node_name
		: hbc.storage_node_uid;

	char serial_sql[sizeof(hbc.storage_node_serial) * 2 + 1];
	char name_sql[sizeof(hbc.storage_node_name) * 2 + 1];
	char uid_sql[sizeof(hbc.storage_node_uid) * 2 + 1];

	unsigned long serial_len = (unsigned long)strlen(serial_src);
	unsigned long name_len = (unsigned long)strlen(name_src);
	unsigned long uid_len =
		(unsigned long)strlen(hbc.storage_node_uid);

	unsigned long serial_sql_len = mysql_real_escape_string(
		sqldb.conn, serial_sql, serial_src, serial_len);
	serial_sql[serial_sql_len] = '\0';
	unsigned long name_sql_len = mysql_real_escape_string(
		sqldb.conn, name_sql, name_src, name_len);
	name_sql[name_sql_len] = '\0';
	unsigned long uid_sql_len = mysql_real_escape_string(
		sqldb.conn, uid_sql, hbc.storage_node_uid, uid_len);
	uid_sql[uid_sql_len] = '\0';

	size_t priv_len = strlen(priv_pem);
	size_t pub_len = strlen(pub_pem);

	priv_sql = malloc(priv_len * 2 + 1);
	pub_sql = malloc(pub_len * 2 + 1);
	if (!priv_sql || !pub_sql)
		goto cleanup;

	unsigned long priv_sql_len = mysql_real_escape_string(
		sqldb.conn, priv_sql, priv_pem, (unsigned long)priv_len);
	priv_sql[priv_sql_len] = '\0';
	unsigned long pub_sql_len = mysql_real_escape_string(
		sqldb.conn, pub_sql, pub_pem, (unsigned long)pub_len);
	pub_sql[pub_sql_len] = '\0';

	size_t query_len = sizeof(SQL_HOST_AUTH_UPSERT_CERT) +
			   serial_sql_len + uid_sql_len + name_sql_len +
			   priv_sql_len + pub_sql_len + 64;
	sql_query = malloc(query_len);
	if (!sql_query)
		goto cleanup;

	int written = snprintf(sql_query, query_len,
			       SQL_HOST_AUTH_UPSERT_CERT,
			       serial_sql,
			       uid_sql,
			       name_sql,
			       priv_sql,
			       pub_sql,
			       NODE_CERT_VALID_DAYS);
	if (written < 0 || (size_t)written >= query_len)
		goto cleanup;

	if (mysql_real_query(sqldb.conn, sql_query,
			     (unsigned long)written) != 0) {
		fprintf(stderr,
			"bootstrap: failed to persist node certificate: %s\n",
			mysql_error(sqldb.conn));
		goto cleanup;
	}

	rc = 0;

cleanup:
	free(sql_query);
	free(priv_sql);
	free(pub_sql);
	if (priv_pem) {
		memset(priv_pem, 0, strlen(priv_pem));
		free(priv_pem);
	}
	free(pub_pem);
	return rc;
}

int add_node_join_token(uint32_t node_id, const char *token)
{
	(void)node_id;

	if (!token || !*token)
		return 0;
	if (!sqldb.conn || !hbc.storage_node_uid[0])
		return -1;

	char sql_query[1024];
	char token_sql[HIVE_BOOTSTRAP_MSG_MAX * 2 + 1];
	char uid_sql[sizeof(hbc.storage_node_uid) * 2 + 1];
	unsigned long token_len = (unsigned long)strlen(token);
	unsigned long uid_len =
		(unsigned long)strlen(hbc.storage_node_uid);

	if (token_len >= (sizeof(token_sql) - 1) / 2)
		return -1;

	mysql_real_escape_string(sqldb.conn,
				 token_sql,
				 token,
				 token_len);
	mysql_real_escape_string(sqldb.conn,
				 uid_sql,
				 hbc.storage_node_uid,
				 uid_len);

	int written = snprintf(sql_query, sizeof(sql_query),
			       SQL_HOST_TOKEN_INSERT_NODE_JOIN,
			       token_sql,
			       uid_sql);

	if (written < 0 || (size_t)written >= sizeof(sql_query))
		return -1;

	if (mysql_real_query(sqldb.conn, sql_query, (unsigned long)written) != 0) {
		fprintf(stderr, "bootstrap: failed to add node add token: %s\n",
			mysql_error(sqldb.conn));
		return -1;
	}

	return 0;
}

int update_node_for_add(void)
{
	if (!ensure_hive_guard_service()) {
		fprintf(stderr,
			"bootstrap: unable to ensure hive_guard systemd service\n");
		return -1;
	}

	struct hive_storage_node local;

	if (!gather_local_identity(&local)) {
		fprintf(stderr,
			"bootstrap: unable to gather local node identity\n");
		return -1;
	}

	init_hive_link();
	if (!sqldb.sql_init || !sqldb.conn) {
		fprintf(stderr,
			"bootstrap: MariaDB connection not available\n");
		return -1;
	}

	uint64_t node_id = hbc.storage_node_id;
	const uint64_t requested_node_id = node_id;

	if (!node_id)
		node_id = lookup_existing_node_id(local.uid, local.serial);

	if (!node_id) {
		if (g_pending_request_type == BOOTSTRAP_REQ_NODE_JOIN) {
			uint64_t highest = lookup_highest_node_id();

			node_id = highest ? highest + 1 : 1;
		} else if (requested_node_id) {
			node_id = requested_node_id;
		} else {
			node_id = 1;
		}
	}
	local.id = (uint32_t)node_id;
	hbc.storage_node_id = (uint32_t)node_id;

	int version = 0;
	int patch = 0;

	hifs_parse_version(&version, &patch);
	if (!hifs_persist_storage_node(node_id, local.address, local.guard_port))
		return -1;
	if (!update_storage_node_row(hbc.cluster_id, node_id,
				     &local, version, patch))
		return -1;

	uint64_t now = local.last_heartbeat ? local.last_heartbeat
					     : (uint64_t)time(NULL);

	hbc.storage_node_last_heartbeat = now;
	hbc.storage_node_date_added_to_cluster = now;
	hbc.storage_node_guard_port = local.guard_port;
	hbc.storage_node_stripe_port = local.stripe_port;
	hbc.storage_node_fenced = 1;
	hbc.storage_node_hive_version = (uint32_t)version;
	hbc.storage_node_hive_patch_level = (uint32_t)patch;
	hbc.storage_node_client_connect_timeout_ms = 60000;
	hbc.storage_node_storage_node_connect_timeout_ms = 30000;
	hbc.storage_node_storage_capacity_bytes = 0;
	hbc.storage_node_storage_used_bytes = 0;
	hbc.storage_node_storage_reserved_bytes = 0;
	hbc.storage_node_storage_overhead_bytes = 0;
	hbc.storage_node_last_maintenance = 0;
	hbc.storage_node_maintenance_reason[0] = '\0';
	hbc.storage_node_maintenance_started_at = 0;
	hbc.storage_node_maintenance_ended_at = 0;
	safe_copy(hbc.storage_node_name, sizeof(hbc.storage_node_name),
		  local.name);
	safe_copy(hbc.storage_node_address, sizeof(hbc.storage_node_address),
		  local.address);
	safe_copy(hbc.storage_node_uid, sizeof(hbc.storage_node_uid),
		  local.uid);
	safe_copy(hbc.storage_node_serial, sizeof(hbc.storage_node_serial),
		  local.serial);

	safe_copy(hbc.database_state, sizeof(hbc.database_state), "configured");
	safe_copy(hbc.kv_state, sizeof(hbc.kv_state), "configured");
	safe_copy(hbc.cont1_state, sizeof(hbc.cont1_state), "configured");
	safe_copy(hbc.cont2_state, sizeof(hbc.cont2_state), "configured");

	add_node_join_token(hbc.storage_node_id, hbc.bootstrap_token);
	add_node_mtls_token(hbc.storage_node_id, hbc.bootstrap_token);

    if (node_id == 1) {
        fprintf(stdout,
            "bootstrap: first node (id=%u) added to cluster (id=%llu)\n",
            hbc.storage_node_id,
            (unsigned long long)hbc.cluster_id);
        fflush(stdout);
    } else {
        fprintf(stdout,
            "bootstrap: node (id=%u) added to cluster (id=%llu)\n", hbc.storage_node_id,
            (unsigned long long)hbc.cluster_id);
        fflush(stdout);

        distribute_node_tokens_to_existing_nodes(hbc.cluster_id,
                                        hbc.storage_node_id,
                                        hbc.storage_node_uid);
    }
	return 0;
}

static bool persist_cluster_record(const struct hive_bootstrap_request *req)
{
	if (!req)
		return false;

	init_hive_link();
	if (!sqldb.sql_init || !sqldb.conn) {
		fprintf(stderr, "cluster_config: MariaDB connection not available\n");
		return false;
	}

	const char *name_src = req->cluster_name[0]
		? req->cluster_name : "hive_cluster";
	const char *desc_src = req->cluster_desc[0]
		? req->cluster_desc : "Initial cluster configuration";
	size_t name_len = req->cluster_name[0]
		? strnlen(req->cluster_name, sizeof(req->cluster_name))
		: strlen(name_src);
	size_t desc_len = req->cluster_desc[0]
		? strnlen(req->cluster_desc, sizeof(req->cluster_desc))
		: strlen(desc_src);

	char name_sql[sizeof(req->cluster_name) * 2 + 1];
	char desc_sql[sizeof(req->cluster_desc) * 2 + 1];
	unsigned long esc_len = mysql_real_escape_string(
		sqldb.conn, name_sql, name_src, (unsigned long)name_len);
	name_sql[esc_len] = '\0';
	esc_len = mysql_real_escape_string(
		sqldb.conn, desc_sql, desc_src, (unsigned long)desc_len);
	desc_sql[esc_len] = '\0';

	uint16_t min_nodes = req->min_nodes_req ? req->min_nodes_req : 1;
	char sql_query[MAX_QUERY_SIZE];
	int written = snprintf(sql_query, sizeof(sql_query), SQL_CLUSTER_UPSERT,
			       (unsigned long long)req->cluster_id,
			       name_sql,
			       desc_sql,
			       1U,
			       1U,
			       "pending",
			       "ok",
			       (unsigned int)min_nodes,
			       1U);
	if (written < 0 || (size_t)written >= sizeof(sql_query)) {
		fprintf(stderr, "cluster_config: SQL buffer too small\n");
		return false;
	}

	if (mysql_real_query(sqldb.conn, sql_query, (unsigned long)written) != 0) {
		fprintf(stderr, "cluster_config: SQL failed: %s\n",
			mysql_error(sqldb.conn));
		return false;
	}

	MYSQL_RES *res = mysql_store_result(sqldb.conn);
	if (res)
		mysql_free_result(res);
	else if (mysql_field_count(sqldb.conn) != 0) {
		fprintf(stderr, "cluster_config: failed to consume SQL result: %s\n",
			mysql_error(sqldb.conn));
		return false;
	}

	return true;
}

static void configure_cluster(const struct hive_bootstrap_request *req)
{
	bootstrap_status_update("cluster_config: processing", 25, "OP_PENDING");
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

	if (!persist_cluster_record(req)) {
		bootstrap_status_update("cluster_config: cluster persist failed",
					100, "IN_ERROR");
		return;
	}
	safe_copy(hbc.cluster_state, sizeof(hbc.cluster_state), "pending");
	fprintf(stdout, "bootstrap: cluster metadata persisted in hive_api.cluster\n");
	fflush(stdout);
	update_node_for_add();

    
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

    update_node_for_add();

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
	fprintf(stdout, "bootstrap_request: %s\n", buf);
	fflush(stdout);
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

	close(listener);
	unlink(path);
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
	const char *status_text = (config_state && *config_state)
		? config_state : "IDLE";

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
			 "\"first_boot_ts\":null}");
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
		 "\"first_boot_ts\":%s}",
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
		 ts_field);
	fprintf(stdout, "bootstrap_status: %s\n", status_buf);
	fflush(stdout);
	return status_buf;
}
