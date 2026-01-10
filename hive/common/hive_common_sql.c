/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

 /*
  *
  * Common SQL helpers
  * 
  */

#include <arpa/inet.h>
#include <errno.h>
#include <limits.h>
#include <netdb.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "../guard/hive_guard.h"
#include "../guard/hive_guard_sql.h"
#include "hive_common_sql.h"
#include "../bootstrap/hive_bootstrap.h"

struct SQLDB sqldb;
unsigned int storage_node_id;
char storage_node_name[50];
char storage_node_address[64];
char storage_node_uid[128];
char storage_node_serial[100];
uint16_t storage_node_guard_port;
uint16_t storage_node_stripe_port;
uint64_t storage_node_last_heartbeat;
uint8_t storage_node_fenced;
uint32_t storage_node_hive_version;
uint32_t storage_node_hive_patch_level;
uint64_t storage_node_last_maintenance;
char storage_node_maintenance_reason[256];
uint64_t storage_node_maintenance_started_at;
uint64_t storage_node_maintenance_ended_at;
uint64_t storage_node_date_added_to_cluster;
uint64_t storage_node_storage_capacity_bytes;
uint64_t storage_node_storage_used_bytes;
uint64_t storage_node_storage_reserved_bytes;
uint64_t storage_node_storage_overhead_bytes;
uint32_t storage_node_client_connect_timeout_ms;
uint32_t storage_node_storage_node_connect_timeout_ms;
static struct hive_storage_node *loaded_storage_nodes;
static size_t loaded_storage_node_count;
static struct hive_storage_node cached_local_node;
static bool cached_local_node_valid;

enum {
	HIVE_NODE_NAME_LEN = sizeof(((struct hive_storage_node *)0)->name),
	HIVE_NODE_ADDR_LEN = sizeof(((struct hive_storage_node *)0)->address),
	HIVE_NODE_UID_LEN = sizeof(((struct hive_storage_node *)0)->uid),
	HIVE_NODE_SERIAL_LEN = sizeof(((struct hive_storage_node *)0)->serial),
};

void hifs_safe_strcpy(char *dst, size_t dst_len, const char *src)
{
	if (!dst || dst_len == 0)
		return;
	if (!src)
		src = "";
	size_t max_copy = dst_len - 1;
	size_t to_copy = strnlen(src, max_copy);
	memcpy(dst, src, to_copy);
	dst[to_copy] = '\0';
}

long hifs_get_host_id(void)
{
	long id = gethostid();

	if (id == -1) {
		char hostname[256];

		if (gethostname(hostname, sizeof(hostname)) == 0) {
			unsigned long hash = 5381;

			for (const unsigned char *p =
				     (const unsigned char *)hostname;
			     *p;
			     ++p)
				hash = ((hash << 5) + hash) + *p;
			id = (long)hash;
		} else {
			id = 0;
		}
	}
	return id;
}

const char *hifs_cached_host_serial(void)
{
	static char serial_buf[32];
	static long cached_host_id = LONG_MIN;
	long host_id = hifs_get_host_id();

	if (host_id != cached_host_id) {
		int len = snprintf(serial_buf, sizeof(serial_buf), "%ld", host_id);

		if (len < 0 || (size_t)len >= sizeof(serial_buf))
			serial_buf[0] = '\0';
		cached_host_id = host_id;
	}
	return serial_buf;
}

char *hifs_get_machine_id(void)
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

static uint32_t sql_to_u32(const char *s)
{
	return s ? (uint32_t)strtoul(s, NULL, 10) : 0;
}

static uint64_t sql_to_u64(const char *s)
{
	return s ? strtoull(s, NULL, 10) : 0ULL;
}

const struct hive_storage_node *hifs_get_storage_nodes(size_t *count)
{
	if (count)
		*count = loaded_storage_node_count;
	return loaded_storage_nodes;
}

const struct hive_storage_node *hifs_get_local_storage_node(void)
{
	return cached_local_node_valid ? &cached_local_node : NULL;
}

static struct hive_storage_node *
hifs_match_local_storage_node(struct hive_storage_node *nodes, size_t count)
{
	if (!nodes || count == 0)
		return NULL;

	uint32_t id_hint = (storage_node_id > 0) ? (uint32_t)storage_node_id : 0;

	if (id_hint != 0) {
		for (size_t i = 0; i < count; ++i) {
			if (nodes[i].id == id_hint)
				return &nodes[i];
		}
	}

	const char *local_uid = hifs_get_machine_id();
	const char *local_serial = hifs_cached_host_serial();

	if (!local_uid || !*local_uid || !local_serial || !*local_serial)
		return NULL;

	for (size_t i = 0; i < count; ++i) {
		if (nodes[i].uid[0] == '\0' || nodes[i].serial[0] == '\0')
			continue;
		if (strcmp(nodes[i].uid, local_uid) == 0 &&
		    strcmp(nodes[i].serial, local_serial) == 0)
			return &nodes[i];
	}
	return NULL;
}

static void hifs_update_local_storage_node(const struct hive_storage_node *node)
{
	if (!node) {
		memset(&cached_local_node, 0, sizeof(cached_local_node));
		cached_local_node_valid = false;
		storage_node_id = 0;
		storage_node_guard_port = 0;
		storage_node_stripe_port = 0;
		return;
	}

	cached_local_node = *node;
	cached_local_node_valid = true;
	storage_node_id = (int)node->id;
	storage_node_guard_port = node->guard_port;
	storage_node_stripe_port = node->stripe_port;
}

static bool hifs_sql_node_data_up_to_date(struct hive_storage_node *local)
{
	static uint64_t local_state_generation;
	static uint64_t persisted_state_generation;
	static time_t last_persist_attempt;

	if (!local)
		return false;

	bool changed = false;
	struct hive_storage_node desired;
	bool have_desired = hifs_get_local_node_identity(&desired);

	if (have_desired) {
		if (desired.address[0] &&
		    strcmp(local->address, desired.address) != 0) {
			hifs_safe_strcpy(local->address, sizeof(local->address),
					 desired.address);
			changed = true;
		}
		if (desired.guard_port &&
		    local->guard_port != desired.guard_port) {
			local->guard_port = desired.guard_port;
			changed = true;
		}
		if (desired.stripe_port &&
		    local->stripe_port != desired.stripe_port) {
			local->stripe_port = desired.stripe_port;
			changed = true;
		}
		if (desired.name[0] &&
		    strcmp(local->name, desired.name) != 0) {
			hifs_safe_strcpy(local->name, sizeof(local->name),
					 desired.name);
			changed = true;
		}
		if (desired.uid[0] &&
		    strcmp(local->uid, desired.uid) != 0) {
			hifs_safe_strcpy(local->uid, sizeof(local->uid),
					 desired.uid);
			changed = true;
		}
		if (desired.serial[0] &&
		    strcmp(local->serial, desired.serial) != 0) {
			hifs_safe_strcpy(local->serial, sizeof(local->serial),
					 desired.serial);
			changed = true;
		}
	}

	if (!local->stripe_port && local->guard_port) {
		local->stripe_port = local->guard_port;
		changed = true;
	}

	time_t now = time(NULL);
	time_t hb = (time_t)local->last_heartbeat;

	if (hb == 0 || now < hb ||
	    (now - hb) >= HIFS_STORAGE_NODE_HEARTBEAT_MAX_AGE) {
		local->last_heartbeat = (uint64_t)now;
		changed = true;
	}

	storage_node_id = local->id;
	hifs_safe_strcpy(storage_node_name, sizeof(storage_node_name),
			 local->name);
	hifs_safe_strcpy(storage_node_address, sizeof(storage_node_address),
			 local->address);
	hifs_safe_strcpy(storage_node_uid, sizeof(storage_node_uid),
			 local->uid);
	hifs_safe_strcpy(storage_node_serial, sizeof(storage_node_serial),
			 local->serial);
	storage_node_guard_port = local->guard_port;
	storage_node_stripe_port = local->stripe_port;
	storage_node_last_heartbeat = local->last_heartbeat;
	storage_node_fenced = local->fenced;
	storage_node_hive_version = local->hive_version;
	storage_node_hive_patch_level = local->hive_patch_level;
	storage_node_last_maintenance = local->last_maintenance;
	hifs_safe_strcpy(storage_node_maintenance_reason,
			 sizeof(storage_node_maintenance_reason),
			 local->maintenance_reason);
	storage_node_maintenance_started_at = local->maintenance_started_at;
	storage_node_maintenance_ended_at = local->maintenance_ended_at;
	storage_node_date_added_to_cluster = local->date_added_to_cluster;
	storage_node_storage_capacity_bytes = local->storage_capacity_bytes;
	storage_node_storage_used_bytes = local->storage_used_bytes;
	storage_node_storage_reserved_bytes =
		local->storage_reserved_bytes;
	storage_node_storage_overhead_bytes =
		local->storage_overhead_bytes;
	storage_node_client_connect_timeout_ms =
		local->client_connect_timeout_ms;
	storage_node_storage_node_connect_timeout_ms =
		local->storage_node_connect_timeout_ms;

	if (changed)
		++local_state_generation;

	bool pending = (local_state_generation > persisted_state_generation);
	if (pending && local->id != 0) {
		if (last_persist_attempt == 0 ||
		    (now - last_persist_attempt) >=
			    HIFS_STORAGE_NODE_DATA_SYNC_MIN_SECS) {
			const char *addr = local->address[0] ?
					   local->address :
					   (have_desired &&
					    desired.address[0] ?
						    desired.address :
						    NULL);
			uint16_t port = local->guard_port ?
					local->guard_port :
					(have_desired ? desired.guard_port :
							0);
			last_persist_attempt = now;
			if (hifs_persist_storage_node(local->id, addr, port)) {
				persisted_state_generation =
					local_state_generation;
				pending = false;
			}
		}
	}

	return !pending;
}

static MYSQL_RES *hifs_execute_sql(const char *sql_string)
{
	MYSQL_RES *res = NULL;

	if (!hifs_execute_query(sql_string, &res))
		return NULL;
	return res;
}

void init_hive_link(void)
{
	if (sqldb.sql_init)
		return;

	sqldb.conn = mysql_init(NULL);
	if (!sqldb.conn) {
		hifs_err("Failed to initialise MariaDB handle\n");
		return;
	}

	if (!mysql_real_connect(sqldb.conn,
				DB_HOST,
				DB_USER,
				DB_PASS,
				DB_NAME,
				DB_PORT,
				DB_SOCKET,
				DB_FLAGS)) {
		hifs_err("Connection to MariaDB failed: %s\n",
			 mysql_error(sqldb.conn));
		mysql_close(sqldb.conn);
		sqldb.conn = NULL;
		return;
	}

	sqldb.sql_init = true;
	unsigned long version = mysql_get_server_version(sqldb.conn);

	hifs_notice("hi_command: Connected to MariaDB server version %lu\n",
		    version);
}

void close_hive_link(void)
{
	if (sqldb.last_query) {
		mysql_free_result(sqldb.last_query);
		sqldb.last_query = NULL;
	}

	if (sqldb.conn)
		mysql_close(sqldb.conn);

	memset(&sqldb, 0, sizeof(sqldb));
}

int get_hive_vers(void)
{
	if (!sqldb.sql_init || !sqldb.conn)
		return 0;
	return (int)mysql_get_server_version(sqldb.conn);
}

bool hifs_load_storage_nodes(void)
{
	enum {
		COL_ID = 0,
		COL_NAME,
		COL_ADDRESS,
		COL_UID,
		COL_SERIAL,
		COL_GUARD_PORT,
		COL_DATA_PORT,
		COL_LAST_HEARTBEAT,
		COL_VERSION,
		COL_PATCH_LEVEL,
		COL_FENCED,
		COL_DOWN,
		COL_LAST_MAINT,
		COL_MAINT_REASON,
		COL_MAINT_STARTED,
		COL_MAINT_ENDED,
		COL_DATE_ADDED,
		COL_SN_CAPCITY,
		COL_SN_USED_CAPACITY,
		COL_SN_RESERVED_CAPACITY,
		COL_OVERHEAD_CAPCACITY,
		COL_CLIENT_CONNECT_TMOUT,
		COL_SN_CONNECT_TMOUT
	};
	MYSQL_RES *res = NULL;
	size_t row_count = 0;
	struct hive_storage_node *nodes = NULL;
	struct hive_storage_node *old_nodes = NULL;
	struct hive_storage_node *local = NULL;

	if (!sqldb.sql_init || !sqldb.conn)
		return false;

	res = hifs_execute_sql(SQL_STORAGE_NODES_SELECT);
	if (!res)
		return false;

	row_count = (size_t)mysql_num_rows(res);
	if (row_count == 0) {
		mysql_free_result(res);
		sqldb.last_query = NULL;
		struct hive_storage_node local_seed;

		if (!hifs_get_local_node_identity(&local_seed))
			return false;
		if (storage_node_id == 0)
			storage_node_id = 1;
		local_seed.id = storage_node_id;
		int ver = 0;
		int patch = 0;

		hifs_parse_version(&ver, &patch);
		local_seed.hive_version = (uint32_t)ver;
		local_seed.hive_patch_level = (uint32_t)patch;
		local_seed.last_heartbeat = (uint64_t)time(NULL);
		local_seed.storage_node_connect_timeout_ms = 0;
		local_seed.client_connect_timeout_ms = 0;
		(void)hifs_persist_storage_node(local_seed.id,
						local_seed.address,
						local_seed.guard_port);

		nodes = calloc(1, sizeof(*nodes));
		if (!nodes)
			return false;
		nodes[0] = local_seed;
		row_count = 1;
		sqldb.rows = (int)row_count;
		goto loaded_nodes_ready;
	}

	nodes = calloc(row_count, sizeof(*nodes));
	if (!nodes)
		goto fail;

	for (size_t i = 0; i < row_count; ++i) {
		MYSQL_ROW row = mysql_fetch_row(res);

		if (!row)
			goto fail;
		nodes[i].id = sql_to_u32(row[COL_ID]);
		if (row[COL_NAME])
			strncpy(nodes[i].name, row[COL_NAME],
				sizeof(nodes[i].name) - 1);
		if (row[COL_ADDRESS])
			strncpy(nodes[i].address, row[COL_ADDRESS],
				sizeof(nodes[i].address) - 1);
		if (row[COL_UID])
			strncpy(nodes[i].uid, row[COL_UID],
				sizeof(nodes[i].uid) - 1);
		if (row[COL_SERIAL])
			strncpy(nodes[i].serial, row[COL_SERIAL],
				sizeof(nodes[i].serial) - 1);
		nodes[i].guard_port =
			(uint16_t)sql_to_u32(row[COL_GUARD_PORT]);
		nodes[i].stripe_port =
			(uint16_t)sql_to_u32(row[COL_DATA_PORT]);
		nodes[i].online = row[COL_DOWN]
			? (uint8_t)!sql_to_u32(row[COL_DOWN]) : 1;
		nodes[i].fenced = (uint8_t)sql_to_u32(row[COL_FENCED]);
		nodes[i].last_heartbeat =
			sql_to_u64(row[COL_LAST_HEARTBEAT]);
		nodes[i].hive_version = sql_to_u32(row[COL_VERSION]);
		nodes[i].hive_patch_level =
			sql_to_u32(row[COL_PATCH_LEVEL]);
		nodes[i].last_maintenance =
			sql_to_u64(row[COL_LAST_MAINT]);
		nodes[i].maintenance_started_at =
			sql_to_u64(row[COL_MAINT_STARTED]);
		nodes[i].maintenance_ended_at =
			sql_to_u64(row[COL_MAINT_ENDED]);
		if (row[COL_MAINT_REASON])
			strncpy(nodes[i].maintenance_reason,
				row[COL_MAINT_REASON],
				sizeof(nodes[i].maintenance_reason) - 1);
		nodes[i].date_added_to_cluster =
			sql_to_u64(row[COL_DATE_ADDED]);
		nodes[i].storage_capacity_bytes =
			sql_to_u64(row[COL_SN_CAPCITY]);
		nodes[i].storage_used_bytes =
			sql_to_u64(row[COL_SN_USED_CAPACITY]);
		nodes[i].storage_reserved_bytes =
			sql_to_u64(row[COL_SN_RESERVED_CAPACITY]);
		nodes[i].storage_overhead_bytes =
			sql_to_u64(row[COL_OVERHEAD_CAPCACITY]);
		nodes[i].client_connect_timeout_ms =
			sql_to_u32(row[COL_CLIENT_CONNECT_TMOUT]);
		nodes[i].storage_node_connect_timeout_ms =
			sql_to_u32(row[COL_SN_CONNECT_TMOUT]);
		hifs_debug("node_id=%u address=%s guard_port=%u",
			   nodes[i].id,
			   nodes[i].address,
			   nodes[i].guard_port);
	}

	mysql_free_result(res);
	sqldb.last_query = NULL;
	sqldb.rows = (int)row_count;

loaded_nodes_ready:
	old_nodes = loaded_storage_nodes;
	loaded_storage_nodes = nodes;
	loaded_storage_node_count = row_count;
	nodes = NULL;

	local = hifs_match_local_storage_node(loaded_storage_nodes,
					      loaded_storage_node_count);
	if (local)
		hifs_sql_node_data_up_to_date(local);
	if (!local) {
		if (cached_local_node_valid)
			hifs_update_local_storage_node(NULL);
	} else if (!cached_local_node_valid ||
		   memcmp(local, &cached_local_node, sizeof(*local)) != 0) {
		hifs_update_local_storage_node(local);
	}

	free(old_nodes);
	return true;

fail:
	free(nodes);
	if (res) {
		mysql_free_result(res);
		sqldb.last_query = NULL;
	}
	sqldb.rows = 0;
	return false;
}

uint16_t hifs_local_guard_port(void)
{
	const char *env = getenv(ENV_LISTEN_PORT);
	long port = env && *env ? strtol(env, NULL, 10) : 0;

	if (port <= 0 || port > UINT16_MAX)
		port = atoi(HIFS_GUARD_PORT_STR);
	return (uint16_t)port;
}

void hifs_parse_version(int *version_out, int *patch_out)
{
	int version = 0;
	int patch = 0;
	const char *ver = HIFS_VERSION;

	if (ver && *ver) {
		char *endptr = NULL;
		long v = strtol(ver, &endptr, 10);

		if (v >= 0)
			version = (int)v;
		if (endptr && *endptr == '.') {
			long p = strtol(endptr + 1, NULL, 10);

			if (p >= 0)
				patch = (int)p;
		}
	}

	if (version_out)
		*version_out = version;
	if (patch_out)
		*patch_out = patch;
}

bool hifs_get_local_node_identity(struct hive_storage_node *node)
{
	if (!node)
		return false;

	memset(node, 0, sizeof(*node));

	char hostname[sizeof(node->name)] = {0};

	if (gethostname(hostname, sizeof(hostname)) != 0)
		hifs_safe_strcpy(hostname, sizeof(hostname), "hive_guard");
	hostname[sizeof(hostname) - 1] = '\0';
	hifs_safe_strcpy(node->name, sizeof(node->name), hostname);

	const char *uid = hifs_get_machine_id();

	if (uid)
		hifs_safe_strcpy(node->uid, sizeof(node->uid), uid);

	const char *serial = hifs_cached_host_serial();

	if (serial)
		hifs_safe_strcpy(node->serial, sizeof(node->serial), serial);

	bool have_addr = false;
	struct addrinfo hints = {0};
	struct addrinfo *res = NULL;

	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	if (getaddrinfo(hostname, NULL, &hints, &res) == 0) {
		for (struct addrinfo *it = res; it && !have_addr; it = it->ai_next) {
			if (it->ai_family != AF_INET)
				continue;
			struct sockaddr_in *sin = (struct sockaddr_in *)it->ai_addr;

			if (inet_ntop(AF_INET, &sin->sin_addr,
				      node->address, sizeof(node->address)))
				have_addr = true;
		}
		freeaddrinfo(res);
	}

	if (!have_addr) {
		struct hostent *he = gethostbyname(hostname);

		if (he && he->h_addr_list && he->h_addr_list[0]) {
			struct in_addr addr;

			memcpy(&addr, he->h_addr_list[0], sizeof(addr));
			const char *ip = inet_ntoa(addr);

			if (ip) {
				strncpy(node->address, ip, sizeof(node->address) - 1);
				node->address[sizeof(node->address) - 1] = '\0';
				have_addr = true;
			}
		}
	}

	if (!have_addr)
		hifs_safe_strcpy(node->address, sizeof(node->address), "127.0.0.1");

	node->guard_port = hifs_local_guard_port();
	node->stripe_port = node->guard_port;
	return true;
}

bool hifs_execute_query(const char *sql, MYSQL_RES **out_res)
{
	if (!sqldb.sql_init || !sqldb.conn) {
		hifs_err("MariaDB connection is not initialised\n");
		if (out_res)
			*out_res = NULL;
		return false;
	}

	if (sqldb.last_query) {
		mysql_free_result(sqldb.last_query);
		sqldb.last_query = NULL;
	}

	hifs_debug("SQL query is [%s]", sql);

	if (mysql_query(sqldb.conn, sql) != 0) {
		hifs_err("SQL execution failed: %s\n", mysql_error(sqldb.conn));
		if (out_res)
			*out_res = NULL;
		return false;
	}

	unsigned int field_count = mysql_field_count(sqldb.conn);

	sqldb.last_affected = mysql_affected_rows(sqldb.conn);
	sqldb.last_insert_id = mysql_insert_id(sqldb.conn);

	if (field_count == 0) {
		sqldb.rec_count = 0;
		sqldb.rows = (int)sqldb.last_affected;
		sqldb.cols = 0;
		if (out_res)
			*out_res = NULL;
		return true;
	}

	MYSQL_RES *res = mysql_store_result(sqldb.conn);
	if (!res) {
		hifs_err("Failed to fetch result set: %s\n", mysql_error(sqldb.conn));
		if (out_res)
			*out_res = NULL;
		return false;
	}

	sqldb.last_query = res;
	sqldb.rec_count = (int)mysql_num_rows(res);
	sqldb.rows = sqldb.rec_count;
	sqldb.cols = mysql_num_fields(res);

	if (out_res)
		*out_res = res;

	return true;
}

bool hifs_insert_sql(const char *sql_string)
{
	MYSQL_RES *res = NULL;

	return hifs_execute_query(sql_string, &res);
}

bool hifs_persist_storage_node(uint64_t node_id,
			       const char *node_address,
			       uint16_t node_port)
{
	static struct {
		uint64_t node_id;
		char address[64];
		uint16_t port;
		time_t ts;
		bool ok;
	} cache;
	char sql_query[MAX_QUERY_SIZE];

	if (!sqldb.sql_init || !sqldb.conn || node_id == 0)
		return false;

	struct hive_storage_node local = {0};

	if (!hifs_get_local_node_identity(&local))
		return false;

	if (node_address && *node_address)
		hifs_safe_strcpy(local.address, sizeof(local.address), node_address);
	if (node_port)
		local.guard_port = node_port;
	if (!local.stripe_port)
		local.stripe_port = local.guard_port;

	time_t now = time(NULL);
	if (cache.node_id == node_id &&
	    cache.port == local.guard_port &&
	    cache.address[0] != '\0' &&
	    strcmp(cache.address, local.address) == 0 &&
	    cache.ts != 0 &&
	    (now - cache.ts) < HIFS_STORAGE_NODE_PERSIST_MIN_SECS)
		return cache.ok;

	int version = 0;
	int patch = 0;

	hifs_parse_version(&version, &patch);

	int written = snprintf(sql_query, sizeof(sql_query), SQL_STORAGE_NODE_UPSERT,
			       (unsigned long long)node_id,
			       local.name[0] ? local.name : "hive_guard",
			       local.address[0] ? local.address : "127.0.0.1",
			       local.uid[0] ? local.uid : hifs_get_machine_id(),
			       local.serial[0] ? local.serial : hifs_cached_host_serial(),
			       (unsigned)local.guard_port,
			       (unsigned)local.stripe_port,
			       version,
			       patch,
			       0ULL, 0ULL, 0ULL, 0ULL);
	if (written < 0 || (size_t)written >= sizeof(sql_query))
		return false;

	bool ok = hifs_insert_sql(sql_query);
	cache.node_id = node_id;
	hifs_safe_strcpy(cache.address, sizeof(cache.address), local.address);
	cache.port = local.guard_port;
	cache.ts = now;
	cache.ok = ok;
	return ok;
}

static uint64_t lookup_existing_node_id_sql(const char *uid, const char *serial)
{
	if (!sqldb.conn)
		return 0;

	const char *uid_src = uid ? uid : "";
	const char *serial_src = serial ? serial : "";
	char uid_sql[HIVE_NODE_UID_LEN * 2 + 1];
	char serial_sql[HIVE_NODE_SERIAL_LEN * 2 + 1];
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
			fprintf(stderr,
				"bootstrap: node lookup store_result failed: %s\n",
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

static uint64_t lookup_highest_node_id_sql(void)
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

static bool update_storage_node_row_common(uint64_t cluster_id,
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
	const char *serial_src = local->serial[0] ?
				 local->serial :
				 hifs_cached_host_serial();

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
		hifs_safe_strcpy(cluster_buf, sizeof(cluster_buf), "NULL");

	int written = snprintf(sql_query, sizeof(sql_query),
			       SQL_STORAGE_NODE_JOIN_UPDATE,
			       cluster_buf,
			       name_sql,
			       addr_sql,
			       uid_sql,
			       serial_sql,
			       (unsigned)local->guard_port,
			       (unsigned)(local->stripe_port ?
					      local->stripe_port :
					      local->guard_port),
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

int hifs_update_node_for_add(struct hive_bootstrap_config *config,
			     struct hive_storage_node *local,
			     bool is_node_join_request,
			     uint64_t requested_node_id)
{
	if (!config || !local)
		return -1;

	init_hive_link();
	if (!sqldb.sql_init || !sqldb.conn) {
		fprintf(stderr,
			"bootstrap: MariaDB connection not available\n");
		return -1;
	}

	uint64_t node_id = config->storage_node_id;

	if (!node_id)
		node_id = lookup_existing_node_id_sql(local->uid, local->serial);

	if (!node_id) {
		if (is_node_join_request) {
			uint64_t highest = lookup_highest_node_id_sql();

			node_id = highest ? highest + 1 : 1;
		} else if (requested_node_id) {
			node_id = requested_node_id;
		} else {
			node_id = 1;
		}
	}

	local->id = (uint32_t)node_id;
	config->storage_node_id = (uint32_t)node_id;

	int version = 0;
	int patch = 0;

	hifs_parse_version(&version, &patch);
	if (!hifs_persist_storage_node(node_id, local->address, local->guard_port))
		return -1;
	if (!update_storage_node_row_common(config->cluster_id, node_id,
					    local, version, patch))
		return -1;

	uint64_t now = local->last_heartbeat ? local->last_heartbeat
					     : (uint64_t)time(NULL);

	config->storage_node_last_heartbeat = now;
	config->storage_node_date_added_to_cluster = now;
	config->storage_node_guard_port = local->guard_port;
	config->storage_node_stripe_port = local->stripe_port;
	config->storage_node_fenced = 1;
	config->storage_node_hive_version = (uint32_t)version;
	config->storage_node_hive_patch_level = (uint32_t)patch;
	config->storage_node_client_connect_timeout_ms = 60000;
	config->storage_node_storage_node_connect_timeout_ms = 30000;
	config->storage_node_storage_capacity_bytes = 0;
	config->storage_node_storage_used_bytes = 0;
	config->storage_node_storage_reserved_bytes = 0;
	config->storage_node_storage_overhead_bytes = 0;
	config->storage_node_last_maintenance = 0;
	config->storage_node_maintenance_reason[0] = '\0';
	config->storage_node_maintenance_started_at = 0;
	config->storage_node_maintenance_ended_at = 0;
	hifs_safe_strcpy(config->storage_node_name,
			 sizeof(config->storage_node_name),
			 local->name);
	hifs_safe_strcpy(config->storage_node_address,
			 sizeof(config->storage_node_address),
			 local->address);
	hifs_safe_strcpy(config->storage_node_uid,
			 sizeof(config->storage_node_uid),
			 local->uid);
	hifs_safe_strcpy(config->storage_node_serial,
			 sizeof(config->storage_node_serial),
			 local->serial);

	hifs_safe_strcpy(config->database_state,
			 sizeof(config->database_state),
			 "configured");
	hifs_safe_strcpy(config->kv_state,
			 sizeof(config->kv_state),
			 "configured");
	hifs_safe_strcpy(config->cont1_state,
			 sizeof(config->cont1_state),
			 "configured");
	hifs_safe_strcpy(config->cont2_state,
			 sizeof(config->cont2_state),
			 "configured");

	return 0;
}
