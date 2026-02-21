/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */


 /* Install Boostrap Here. */
 /* Let's setup a Node.    */
 /* And maybe a cluster.   */


#include <errno.h>
#include <limits.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "hive_bootstrap.h"
#include "hive_bootstrap_sock.h"
#include "../common/hive_common_sql.h"
#include <syslog.h>


#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

struct hive_bootstrap_config hbc;

static const unsigned int MAX_STAGE_ATTEMPTS = 3;

__attribute__((weak)) void hicomm_log(int level, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf((level <= LOG_ERR) ? stderr : stdout, fmt, ap);
	fputc('\n', (level <= LOG_ERR) ? stderr : stdout);
	va_end(ap);

	va_start(ap, fmt);
	vsyslog(level, fmt, ap);
	va_end(ap);
}

__attribute__((weak)) int hg_kv_init(const char *path)
{
	(void)path;
	return 0;
}

static bool ensure_directory(const char *path)
{
	if (!path || !*path)
		return false;
	if (mkdir(path, 0755) == 0)
		return true;
	if (errno == EEXIST)
		return true;
	fprintf(stderr, "hive_guard: failed mkdir(%s): %s\n",
		path, strerror(errno));
	return false;
}

static void setup_bootstrap_logging(void)
{
	if (!ensure_directory(HIVE_LOG_DIR))
		return;
	FILE *fp = fopen(HIVE_HBS_LOG_FILE, "a");

	if (!fp)
		return;
	setvbuf(fp, NULL, _IOLBF, 0);
	int fd = fileno(fp);

	if (fd >= 0) {
		dup2(fd, fileno(stdout));
		dup2(fd, fileno(stderr));
	}
}

static const char *skip_ws(const char *p)
{
	while (p && *p && isspace((unsigned char)*p))
		++p;
	return p;
}

static const char *find_json_key(const char *json, const char *key,
				 size_t *needle_len_out)
{
	char needle[128];
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
	if (strncmp(p, "true", 4) == 0) {
		*out = 1;
		return true;
	}
	if (strncmp(p, "false", 5) == 0) {
		*out = 0;
		return true;
	}

	errno = 0;
	char *end = NULL;
	unsigned long long v = strtoull(p, &end, 10);
	if (p == end || errno == ERANGE)
		return false;
	*out = (uint64_t)v;
	return true;
}

static bool equals_ignore_case(const char *lhs, const char *rhs)
{
	if (!lhs || !rhs)
		return false;
	while (*lhs && *rhs) {
		if (tolower((unsigned char)*lhs) != tolower((unsigned char)*rhs))
			return false;
		++lhs;
		++rhs;
	}
	return *lhs == '\0' && *rhs == '\0';
}

static bool parse_json_boolish(const char *json, const char *key, bool *out)
{
	char buf[16];

	if (!out)
		return false;
	if (parse_json_string_value(json, key, buf, sizeof(buf))) {
		size_t len = strlen(buf);

		for (size_t i = 0; i < len; ++i)
			buf[i] = (char)tolower((unsigned char)buf[i]);
		if (strcmp(buf, "yes") == 0 || strcmp(buf, "true") == 0 ||
		    strcmp(buf, "ready") == 0 || strcmp(buf, "1") == 0) {
			*out = true;
			return true;
		}
		if (strcmp(buf, "no") == 0 || strcmp(buf, "false") == 0 ||
		    strcmp(buf, "0") == 0 || strcmp(buf, "null") == 0 ||
		    buf[0] == '\0') {
			*out = false;
			return true;
		}
	}
	uint64_t num;

	if (parse_json_u64_value(json, key, &num)) {
		*out = (num != 0);
		return true;
	}
	return false;
}

static bool replace_json_value(const char *key, const char *replacement,
			       bool quoted)
{
	FILE *fp;
	char *buf = NULL;
	char *new_buf = NULL;
	long sz;

	if (!key || !replacement)
		return false;
	fp = fopen(HIVE_NODE_CONF_PATH, "r");
	if (!fp) {
		fprintf(stderr, "bootstrap: unable to open %s: %s\n",
			HIVE_NODE_CONF_PATH, strerror(errno));
		return false;
	}
	if (fseek(fp, 0, SEEK_END) != 0) {
		fclose(fp);
		return false;
	}
	sz = ftell(fp);
	if (sz < 0) {
		fclose(fp);
		return false;
	}
	rewind(fp);
	buf = malloc((size_t)sz + 1);
	if (!buf) {
		fclose(fp);
		return false;
	}
	size_t len = fread(buf, 1, (size_t)sz, fp);
	fclose(fp);
	buf[len] = '\0';
	size_t needle_len = 0;
	const char *key_pos = find_json_key(buf, key, &needle_len);

	if (!key_pos) {
		free(buf);
		return false;
	}
	const char *value_pos = skip_ws(key_pos + needle_len);

	if (*value_pos != ':') {
		free(buf);
		return false;
	}
	value_pos = skip_ws(value_pos + 1);
	if (!*value_pos) {
		free(buf);
		return false;
	}

	const char *scan = value_pos;

	if (*scan == '"') {
		++scan;
		while (*scan) {
			if (*scan == '"' && scan[-1] != '\\')
				break;
			++scan;
		}
		if (*scan == '"')
			++scan;
	} else {
		while (*scan && *scan != ',' && *scan != '}' &&
		       *scan != '\n' && *scan != '\r')
			++scan;
	}

	const char *suffix = scan;
	size_t prefix_len = (size_t)(value_pos - buf);
	size_t suffix_len = len - (size_t)(suffix - buf);
	size_t repl_len = strlen(replacement);
	size_t total = prefix_len + suffix_len + repl_len + (quoted ? 2 : 0);

	new_buf = malloc(total + 1);
	if (!new_buf) {
		free(buf);
		return false;
	}

	memcpy(new_buf, buf, prefix_len);
	size_t pos = prefix_len;

	if (quoted)
		new_buf[pos++] = '"';
	memcpy(new_buf + pos, replacement, repl_len);
	pos += repl_len;
	if (quoted)
		new_buf[pos++] = '"';
	memcpy(new_buf + pos, suffix, suffix_len);
	pos += suffix_len;
	new_buf[pos] = '\0';

	fp = fopen(HIVE_NODE_CONF_PATH, "w");
	if (!fp) {
		free(buf);
		free(new_buf);
		return false;
	}
	bool ok = fwrite(new_buf, 1, pos, fp) == pos;

	fclose(fp);
	free(buf);
	free(new_buf);
	return ok;
}

bool hive_bootstrap_update_string_field(const char *key, const char *value)
{
	if (!value)
		value = "";
	return replace_json_value(key, value, true);
}

bool hive_bootstrap_update_number_field(const char *key, uint64_t value)
{
	char buffer[32];

	snprintf(buffer, sizeof(buffer), "%llu",
		 (unsigned long long)value);
	return replace_json_value(key, buffer, false);
}

static void apply_node_config(const char *json)
{
	uint64_t val;
	bool ready_flag;

	if (parse_json_u64_value(json, "storage_node_id", &val))
		hbc.storage_node_id = (unsigned int)val;
	parse_json_string_value(json, "job_id",
		hbc.job_id, sizeof(hbc.job_id));
	if (parse_json_u64_value(json, "cluster_id", &val))
		hbc.cluster_id = (unsigned int)val;
	parse_json_string_value(json, "cluster_state",
		hbc.cluster_state, sizeof(hbc.cluster_state));
	parse_json_string_value(json, "database_state",
		hbc.database_state, sizeof(hbc.database_state));
	parse_json_string_value(json, "kv_state",
		hbc.kv_state, sizeof(hbc.kv_state));		
	parse_json_string_value(json, "cont1_state",
		hbc.cont1_state, sizeof(hbc.cont1_state));
	parse_json_string_value(json, "cont2_state",
		hbc.cont2_state, sizeof(hbc.cont2_state));
	if (parse_json_u64_value(json, "min_nodes_req", &val))
		hbc.min_nodes_req = (unsigned int)val;
	parse_json_string_value(json, "bootstrap_token",
		hbc.bootstrap_token, sizeof(hbc.bootstrap_token));	
	parse_json_string_value(json, "first_boot_ts",
		hbc.first_boot_ts, sizeof(hbc.first_boot_ts));

	parse_json_string_value(json, "storage_node_name",
		hbc.storage_node_name, sizeof(hbc.storage_node_name));

	parse_json_string_value(json, "storage_node_address",
		hbc.storage_node_address, sizeof(hbc.storage_node_address));
	parse_json_string_value(json, "storage_node_uid",
		hbc.storage_node_uid, sizeof(hbc.storage_node_uid));
	parse_json_string_value(json, "storage_node_cduid",
		hbc.storage_node_cduid, sizeof(hbc.storage_node_cduid));
	parse_json_string_value(json, "storage_node_serial",
		hbc.storage_node_serial, sizeof(hbc.storage_node_serial));
	if (parse_json_u64_value(json, "storage_node_guard_port", &val))
		hbc.storage_node_guard_port = (uint16_t)val;
	if (parse_json_u64_value(json, "storage_node_stripe_port", &val))
		hbc.storage_node_stripe_port = (uint16_t)val;
	if (parse_json_u64_value(json, "storage_node_last_heartbeat", &val))
		hbc.storage_node_last_heartbeat = val;
	if (parse_json_u64_value(json, "storage_node_fenced", &val))
		hbc.storage_node_fenced = (uint8_t)val;
	if (parse_json_u64_value(json, "storage_node_hive_version", &val))
		hbc.storage_node_hive_version = (uint32_t)val;
	if (parse_json_u64_value(json, "storage_node_hive_patch_level", &val))
		hbc.storage_node_hive_patch_level = (uint32_t)val;
	if (parse_json_u64_value(json, "storage_node_last_maintenance", &val))
		hbc.storage_node_last_maintenance = val;
	parse_json_string_value(json, "storage_node_maintenance_reason",
		hbc.storage_node_maintenance_reason,
		sizeof(hbc.storage_node_maintenance_reason));
	if (parse_json_u64_value(json, "storage_node_maintenance_started_at", &val))
		hbc.storage_node_maintenance_started_at = val;
	if (parse_json_u64_value(json, "storage_node_maintenance_ended_at", &val))
		hbc.storage_node_maintenance_ended_at = val;
	if (parse_json_u64_value(json, "storage_node_date_added_to_cluster", &val))
		hbc.storage_node_date_added_to_cluster = val;
	if (parse_json_u64_value(json, "storage_node_storage_capacity_bytes", &val))
		hbc.storage_node_storage_capacity_bytes = val;
	if (parse_json_u64_value(json, "storage_node_storage_used_bytes", &val))
		hbc.storage_node_storage_used_bytes = val;
	if (parse_json_u64_value(json, "storage_node_storage_reserved_bytes", &val))
		hbc.storage_node_storage_reserved_bytes = val;
	if (parse_json_u64_value(json, "storage_node_storage_overhead_bytes", &val))
		hbc.storage_node_storage_overhead_bytes = val;
	if (parse_json_u64_value(json, "storage_node_client_connect_timeout_ms", &val))
		hbc.storage_node_client_connect_timeout_ms = (uint32_t)val;
	if (parse_json_u64_value(json, "storage_node_storage_node_connect_timeout_ms", &val))
		hbc.storage_node_storage_node_connect_timeout_ms = (uint32_t)val;
	if (parse_json_u64_value(json, "stage_of_config", &val))
		hbc.stage_of_config = (uint32_t)val;
	if (parse_json_u64_value(json, "num_of_attempts_this_stage", &val))
		hbc.num_attempts_this_stage = (uint32_t)val;
	if (parse_json_boolish(json, "ready_4_web_conf", &ready_flag))
		hbc.ready_for_web_conf = ready_flag;
}

static bool load_node_config(void)
{
	struct stat st;
	FILE *f;
	char *buf;
	size_t len, rd;

	if (stat(HIVE_NODE_CONF_PATH, &st) != 0 || st.st_size <= 0)
		return false;

	len = (size_t)st.st_size;
	buf = malloc(len + 1);
	if (!buf)
		return false;

	f = fopen(HIVE_NODE_CONF_PATH, "r");
	if (!f) {
		free(buf);
		return false;
	}

	rd = fread(buf, 1, len, f);
	fclose(f);
	buf[rd] = '\0';

	apply_node_config(buf);
	free(buf);
	return true;
}

static bool is_state_configured(const char *state)
{
	return state && equals_ignore_case(state, "configured");
}

static bool containers_configured(void)
{
	return is_state_configured(hbc.cont1_state) &&
	       is_state_configured(hbc.cont2_state);
}

static bool set_stage_field(uint32_t stage)
{
	if (stage > HIVE_STAGE_COMPLETE)
		stage = HIVE_STAGE_COMPLETE;
	if (!hive_bootstrap_update_number_field("stage_of_config", stage))
		return false;
	hbc.stage_of_config = stage;
	return true;
}

static bool set_stage_attempts_field(uint32_t attempts)
{
	if (!hive_bootstrap_update_number_field("num_of_attempts_this_stage", attempts))
		return false;
	hbc.num_attempts_this_stage = attempts;
	return true;
}

static bool set_ready_flag(bool ready)
{
	const char *value = ready ? "yes" : "no";

	if (!hive_bootstrap_update_string_field("ready_4_web_conf", value))
		return false;
	hbc.ready_for_web_conf = ready;
	return true;
}

static bool ensure_ready_flag_for_stage(uint32_t stage)
{
	bool should_be_ready =
		(stage == HIVE_STAGE_WEB_READY &&
		 hbc.stage_of_config < HIVE_STAGE_COMPLETE);

	if (hbc.ready_for_web_conf == should_be_ready)
		return true;
	return set_ready_flag(should_be_ready);
}

static bool normalize_stage_from_states(void)
{
	uint32_t desired_stage;
	bool was_complete = (hbc.stage_of_config >= HIVE_STAGE_COMPLETE);

	if (!is_state_configured(hbc.database_state))
		desired_stage = HIVE_STAGE_DATABASE;
	else if (!is_state_configured(hbc.kv_state))
		desired_stage = HIVE_STAGE_KV;
	else if (!containers_configured())
		desired_stage = HIVE_STAGE_CONTAINERS;
	else if (hbc.cluster_state[0] != '\0' &&
		 !equals_ignore_case(hbc.cluster_state, "unconfigured"))
		desired_stage = HIVE_STAGE_COMPLETE;
	else
		desired_stage = HIVE_STAGE_WEB_READY;

	if (desired_stage != hbc.stage_of_config) {
		if (was_complete && desired_stage < HIVE_STAGE_COMPLETE)
			return ensure_ready_flag_for_stage(hbc.stage_of_config);
		if (!set_stage_field(desired_stage))
			return false;
		if (!set_stage_attempts_field(0))
			return false;
	}
	return ensure_ready_flag_for_stage(hbc.stage_of_config);
}

static bool ensure_stage_marker(uint32_t stage)
{
	if (hbc.stage_of_config != stage) {
		if (!set_stage_field(stage))
			return false;
		if (!set_stage_attempts_field(0))
			return false;
	}
	return ensure_ready_flag_for_stage(stage);
}

static bool increment_stage_attempts(void)
{
	uint32_t next = hbc.num_attempts_this_stage + 1;

	if (!set_stage_attempts_field(next))
		return false;
	return true;
}

static bool advance_to_stage(uint32_t next_stage)
{
	if (!set_stage_field(next_stage))
		return false;
	if (!set_stage_attempts_field(0))
		return false;
	return ensure_ready_flag_for_stage(next_stage);
}

bool hive_bootstrap_set_stage(enum hive_bootstrap_stage stage, bool reset_attempts)
{
	if (!set_stage_field((uint32_t)stage))
		return false;
	if (reset_attempts) {
		if (!set_stage_attempts_field(0))
			return false;
	}
	return ensure_ready_flag_for_stage((uint32_t)stage);
}

bool hive_bootstrap_reset_stage_attempts(void)
{
	return set_stage_attempts_field(0);
}

bool hive_bootstrap_increment_stage_attempts(void)
{
	return increment_stage_attempts();
}

static bool reload_node_config_and_normalize(void)
{
	if (!load_node_config()) {
		fprintf(stderr,
			"bootstrap: failed to reload %s\n",
			HIVE_NODE_CONF_PATH);
		return false;
	}
	return normalize_stage_from_states();
}

typedef bool (*stage_is_complete_fn)(void);
typedef int (*stage_executor_fn)(void);

static bool run_simple_stage(uint32_t stage_id, uint32_t next_stage,
			     const char *label,
			     stage_is_complete_fn is_complete,
			     stage_executor_fn executor)
{
	if (!ensure_stage_marker(stage_id))
		return false;

	while (hbc.stage_of_config == stage_id) {
		if (is_complete && is_complete()) {
			if (!advance_to_stage(next_stage))
				return false;
			return reload_node_config_and_normalize();
		}
		if (hbc.num_attempts_this_stage >= MAX_STAGE_ATTEMPTS) {
			fprintf(stderr,
				"bootstrap: %s stage failed after %u attempts; please contact support\n",
				label, MAX_STAGE_ATTEMPTS);
			return false;
		}
		if (!increment_stage_attempts())
			return false;
		unsigned int attempt = hbc.num_attempts_this_stage;

		fprintf(stderr, "bootstrap: %s stage (attempt %u)\n",
			label, attempt);
		if (executor && executor() != 0)
			fprintf(stderr,
				"bootstrap: %s stage attempt %u did not complete cleanly\n",
				label, attempt);
		if (!reload_node_config_and_normalize())
			return false;
	}
	return true;
}

static bool run_database_stage(void);
static bool run_kv_stage(void);
static bool run_container_stage(void);

static bool mysql_execute_statement(const char *stmt, size_t len)
{
	size_t start = 0;

	while (start < len && isspace((unsigned char)stmt[start]))
		++start;
	while (len > start && isspace((unsigned char)stmt[len - 1]))
		--len;
	if (len <= start)
		return true;
	if (!sqldb.sql_init || !sqldb.conn)
		return false;
	if (mysql_real_query(sqldb.conn, stmt + start,
			   (unsigned long)(len - start)) != 0) {
		fprintf(stderr, "db_config: SQL failed: %s\n",
			sqldb.conn ? mysql_error(sqldb.conn) : "no connection");
		return false;
	}
	MYSQL_RES *res = mysql_store_result(sqldb.conn);
	if (res) {
		mysql_free_result(res);
	} else if (mysql_field_count(sqldb.conn) != 0) {
		fprintf(stderr, "db_config: failed to fetch SQL result: %s\n",
			mysql_error(sqldb.conn));
		return false;
	}
	return true;
}

static bool mysql_execute_script_buffer(const char *script, size_t len)
{
	if (!script || len == 0)
		return true;
	char *stmt = malloc(len + 1);
	if (!stmt)
		return false;
	size_t stmt_len = 0;
	bool in_single = false;
	bool in_double = false;
	bool escaped = false;
	bool in_comment = false;

	for (size_t i = 0; i < len; ++i) {
		unsigned char c = (unsigned char)script[i];
		unsigned char next = (i + 1 < len) ? (unsigned char)script[i + 1] : 0;

		if (in_comment) {
			if (c == '\n' || c == '\r')
				in_comment = false;
			continue;
		}

		if (!in_single && !in_double) {
			if (c == '-' && next == '-') {
				in_comment = true;
				++i;
				continue;
			}
			if (c == '#') {
				in_comment = true;
				continue;
			}
		}

		bool statement_end = (!in_single && !in_double && c == ';');

		stmt[stmt_len++] = (char)c;

		if (c == '\\' && (in_single || in_double)) {
			escaped = !escaped;
		} else {
			escaped = false;
		}
		if (c == '\'' && !in_double && !escaped)
			in_single = !in_single;
		else if (c == '"' && !in_single && !escaped)
			in_double = !in_double;

		if (statement_end) {
			if (stmt_len > 0)
				--stmt_len; /* drop ';' */
			while (stmt_len > 0 &&
			       isspace((unsigned char)stmt[stmt_len - 1]))
				--stmt_len;
			if (!mysql_execute_statement(stmt, stmt_len)) {
				free(stmt);
				return false;
			}
			stmt_len = 0;
		}
	}

	bool ok = mysql_execute_statement(stmt, stmt_len);
	free(stmt);
	return ok;
}

static bool execute_sql_script_file(const char *path)
{
	struct stat st;
	FILE *fp;
	char *buf;
	size_t len;

	if (!path || stat(path, &st) != 0 || st.st_size <= 0) {
		fprintf(stderr, "db_config: unable to stat %s: %s\n",
			path ? path : "(null)", strerror(errno));
		return false;
	}
	len = (size_t)st.st_size;
	fp = fopen(path, "r");
	if (!fp) {
		fprintf(stderr, "db_config: unable to open %s: %s\n",
			path, strerror(errno));
		return false;
	}
	buf = malloc(len + 1);
	if (!buf) {
		fclose(fp);
		return false;
	}
	size_t rd = fread(buf, 1, len, fp);
	fclose(fp);
	buf[rd] = '\0';
	bool ok = mysql_execute_script_buffer(buf, rd);
	free(buf);
	if (!ok)
		fprintf(stderr, "db_config: failed executing SQL from %s\n", path);
	return ok;
}

static bool run_sql_scripts(const char *const *paths, size_t count, bool optional)
{
	bool ran = false;

	for (size_t i = 0; i < count; ++i) {
		const char *path = paths[i];
		if (!path || access(path, R_OK) != 0)
			continue;
		ran = true;
		if (!execute_sql_script_file(path))
			return false;
	}

	if (!ran && !optional) {
		fprintf(stderr, "db_config: no SQL scripts found\n");
		return false;
	}
	return true;
}

static bool ensure_mariadb_ready(void)
{
	init_hive_link();
	if (!sqldb.sql_init || !sqldb.conn) {
		fprintf(stderr, "db_config: MariaDB connection not initialised\n");
		return false;
	}
	if (mysql_ping(sqldb.conn) != 0) {
		fprintf(stderr, "db_config: MariaDB ping failed: %s\n",
			mysql_error(sqldb.conn));
		return false;
	}
	return true;
}

static bool run_mariadb_hardening(void)
{
	static const char *const hardening_paths[] = {
		"/etc/hivefs/mariadb_hardening.sql",
		"hive/guard/sql/mariadb_hardening.sql",
		"../guard/sql/mariadb_hardening.sql",
	};
	return run_sql_scripts(hardening_paths,
			ARRAY_SIZE(hardening_paths), true);
}

static bool run_mariadb_schema(void)
{
	static const char *const schema_paths[] = {
		"hive/guard/sql/make_hive.sql",
		"../guard/sql/make_hive.sql",
		"/usr/share/hivefs/make_hive.sql",
		"/usr/local/share/hivefs/make_hive.sql",
		"/etc/hivefs/make_hive.sql",
	};
	return run_sql_scripts(schema_paths,
			ARRAY_SIZE(schema_paths), false);
}

static bool update_database_state_flag(const char *state)
{
	if (!state || !*state)
		return false;
	if (!hive_bootstrap_update_string_field("database_state", state))
		return false;
	strncpy(hbc.database_state, state, sizeof(hbc.database_state));
	hbc.database_state[sizeof(hbc.database_state) - 1] = '\0';
	return true;
}

int db_config (void)
{

	// Make all the directories needed for the Node if they don't exist in the distro.
	if (!ensure_directory(HIVE_DATA_DIR) ||
	    !ensure_directory(HIVE_GUARD_RAFT_DIR) ||
	    !ensure_directory(HIVE_GUARD_KV_DIR)) {
		fprintf(stderr, "main: failed to prepare state directories\n");
		return 1;
	}

	if (hg_kv_init(HIVE_GUARD_KV_DIR) != 0) {
		fprintf(stderr, "main: failed to initialize RocksDB at %s\n",
			HIVE_GUARD_KV_DIR);
		return 1;
	}

	if (!ensure_mariadb_ready())
		return 1;

	if (!run_mariadb_hardening())
		return 1;

	if (!run_mariadb_schema())
		return 1;

	if (!update_database_state_flag("configured")) {
		fprintf(stderr, "db_config: failed to update node config\n");
		return 1;
	}

	fprintf(stderr, "db_config: database configured\n");


	return 0;
}

struct ui_container_unit {
	const char *service_name;
	const char *description;
	const char *machine_name;
	const char *root_dir;
};

static bool directory_exists(const char *path)
{
	struct stat st;

	if (!path || stat(path, &st) != 0) {
		fprintf(stderr, "ui_config: unable to stat %s: %s\n",
			path ? path : "(null)", strerror(errno));
		return false;
	}
	if (!S_ISDIR(st.st_mode)) {
		fprintf(stderr, "ui_config: %s is not a directory\n", path);
		return false;
	}
	return true;
}

static bool run_command(const char *const argv[])
{
	pid_t pid = fork();

	if (pid < 0) {
		fprintf(stderr, "ui_config: fork failed: %s\n", strerror(errno));
		return false;
	}
	if (pid == 0) {
		execvp(argv[0], (char *const *)argv);
		fprintf(stderr, "ui_config: execvp(%s) failed: %s\n",
			argv[0], strerror(errno));
		_exit(127);
	}
	int status;
	while (waitpid(pid, &status, 0) < 0) {
		if (errno == EINTR)
			continue;
		fprintf(stderr, "ui_config: waitpid failed: %s\n",
			strerror(errno));
		return false;
	}
	if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
		return true;
	fprintf(stderr, "ui_config: command %s exited abnormally (%d)\n",
		argv[0], status);
	return false;
}

static bool write_unit_file(const struct ui_container_unit *unit)
{
	char unit_path[PATH_MAX];
	char content[1024];
	FILE *fp;
	int n;

	if (!unit || !unit->service_name || !unit->root_dir)
		return false;

	n = snprintf(unit_path, sizeof(unit_path), "%s/%s",
		     HIVE_UI_SYSTEMD_DIR, unit->service_name);
	if (n < 0 || (size_t)n >= sizeof(unit_path))
		return false;

	n = snprintf(content, sizeof(content),
		"[Unit]\n"
		"Description=%s\n"
		"After=network-online.target\n"
		"Wants=network-online.target\n\n"
		"[Service]\n"
		"Type=notify\n"
		"ExecStart=/usr/bin/systemd-nspawn --quiet --keep-unit "
		"--boot --machine=%s -D %s\n"
		"KillMode=mixed\n"
		"Restart=always\n"
		"RestartSec=5\n\n"
		"[Install]\n"
		"WantedBy=multi-user.target\n",
		unit->description, unit->machine_name, unit->root_dir);
	if (n < 0 || (size_t)n >= sizeof(content))
		return false;

	fp = fopen(unit_path, "w");
	if (!fp) {
		fprintf(stderr, "ui_config: unable to write %s: %s\n",
			unit_path, strerror(errno));
		return false;
	}
	size_t len = (size_t)n;
	if (fwrite(content, 1, len, fp) != len) {
		fprintf(stderr, "ui_config: short write to %s\n", unit_path);
		fclose(fp);
		return false;
	}
	if (fflush(fp) != 0 || fsync(fileno(fp)) != 0) {
		fprintf(stderr, "ui_config: failed to flush %s: %s\n",
			unit_path, strerror(errno));
		fclose(fp);
		return false;
	}
	if (fchmod(fileno(fp), 0644) != 0) {
		fprintf(stderr, "ui_config: failed to set perms on %s: %s\n",
			unit_path, strerror(errno));
		fclose(fp);
		return false;
	}
	fclose(fp);
	return true;
}

int ui_config(void)
{
	static const struct ui_container_unit units[] = {
		{
			.service_name = "hive-ui-backend.service",
			.description = "Hive UI Backend Container",
			.machine_name = "hive-ui-backend",
			.root_dir = HIVE_UI_BACKEND_ROOT,
		},
		{
			.service_name = "hive-ui-frontend.service",
			.description = "Hive UI Frontend Container",
			.machine_name = "hive-ui-frontend",
			.root_dir = HIVE_UI_FRONTEND_ROOT,
		},
	};

	for (size_t i = 0; i < ARRAY_SIZE(units); ++i) {
		if (!directory_exists(units[i].root_dir))
			return 1;
		if (!write_unit_file(&units[i]))
			return 1;
	}

	if (!run_command((const char *const[]){"systemctl", "daemon-reload", NULL}))
		return 1;

	for (size_t i = 0; i < ARRAY_SIZE(units); ++i) {
		const char *const enable_cmd[] = {
			"systemctl", "enable", "--now", units[i].service_name, NULL,
		};
		if (!run_command(enable_cmd))
			return 1;
	}

	fprintf(stderr, "ui_config: frontend/backend containers started\n");
	return 0;
}

static int kv_config(void)
{
	/* Placeholder for KV store configuration hook. */
	return 0;
}

static bool is_database_stage_complete(void)
{
	return is_state_configured(hbc.database_state);
}

static bool is_kv_stage_complete(void)
{
	return is_state_configured(hbc.kv_state);
}

static bool run_database_stage(void)
{
	return run_simple_stage(HIVE_STAGE_DATABASE, HIVE_STAGE_KV,
				"database", is_database_stage_complete,
				db_config);
}

static bool run_kv_stage(void)
{
	return run_simple_stage(HIVE_STAGE_KV, HIVE_STAGE_CONTAINERS, "kv",
				is_kv_stage_complete, kv_config);
}

static bool mark_containers_configured(void)
{
	static const char configured[] = "configured";

	if (!hive_bootstrap_update_string_field("cont1_state", configured))
		return false;
	strncpy(hbc.cont1_state, configured, sizeof(hbc.cont1_state));
	hbc.cont1_state[sizeof(hbc.cont1_state) - 1] = '\0';

	if (!hive_bootstrap_update_string_field("cont2_state", configured))
		return false;
	strncpy(hbc.cont2_state, configured, sizeof(hbc.cont2_state));
	hbc.cont2_state[sizeof(hbc.cont2_state) - 1] = '\0';

	return true;
}

static bool run_container_stage(void)
{
	if (!ensure_stage_marker(HIVE_STAGE_CONTAINERS))
		return false;

	while (hbc.stage_of_config == HIVE_STAGE_CONTAINERS) {
		if (containers_configured()) {
			if (!advance_to_stage(HIVE_STAGE_WEB_READY))
				return false;
			return reload_node_config_and_normalize();
		}
		if (hbc.num_attempts_this_stage >= MAX_STAGE_ATTEMPTS) {
			fprintf(stderr,
				"bootstrap: container stage failed after %u attempts; please contact support\n",
				MAX_STAGE_ATTEMPTS);
			return false;
		}
		if (!increment_stage_attempts())
			return false;
		unsigned int attempt = hbc.num_attempts_this_stage;

		fprintf(stderr, "bootstrap: container stage (attempt %u)\n",
			attempt);
		int rc = ui_config();

		if (rc == 0) {
			if (!mark_containers_configured())
				return false;
		} else {
			fprintf(stderr,
				"bootstrap: ui_config attempt %u failed\n",
				attempt);
		}
		if (!reload_node_config_and_normalize())
			return false;
	}
	return true;
}

int main(void)
{
	setup_bootstrap_logging();

	load_node_config();
	if (!normalize_stage_from_states())
		return 1;

	fprintf(stderr, "main: starting\n");
	fflush(stderr);

	while (hbc.stage_of_config < HIVE_STAGE_WEB_READY) {
		switch (hbc.stage_of_config) {
		case HIVE_STAGE_INITIAL:
			if (!advance_to_stage(HIVE_STAGE_DATABASE))
				return 1;
			if (!reload_node_config_and_normalize())
				return 1;
			break;
		case HIVE_STAGE_DATABASE:
			if (!run_database_stage())
				return 1;
			break;
		case HIVE_STAGE_KV:
			if (!run_kv_stage())
				return 1;
			break;
		case HIVE_STAGE_CONTAINERS:
			if (!run_container_stage())
				return 1;
			break;
		default:
			if (!normalize_stage_from_states())
				return 1;
			break;
		}
	}

	if (!ensure_ready_flag_for_stage(hbc.stage_of_config))
		return 1;

	if (hbc.stage_of_config >= HIVE_STAGE_WEB_READY) {
		unsigned int attempt = 0;

		for (;;) {
			fprintf(stderr,
				"bootstrap: listening for cluster/node requests (attempt %u)\n",
				attempt + 1);
			fflush(stderr);
			int rc = hive_bootstrap_sock_run(NULL);

			if (rc == 0) {
				fprintf(stderr,
					"bootstrap: request handled, continuing to monitor\n");
				if (!reload_node_config_and_normalize())
					return 1;
				attempt = 0;
				continue;
			}

			fprintf(stderr,
				"bootstrap: socket listener failed (attempt %u)\n",
				attempt + 1);
			++attempt;
			if (attempt >= MAX_STAGE_ATTEMPTS) {
				fprintf(stderr,
					"bootstrap: persistent socket failures; backing off before retry\n");
				attempt = 0;
				sleep(60);
			} else {
				sleep(5);
			}
		}
	}

	return 0;
}
