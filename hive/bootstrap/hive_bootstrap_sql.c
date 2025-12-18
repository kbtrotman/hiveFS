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
#include <netdb.h>
#include <string.h>
#include <sys/stat.h>

#include "hive_bootstrap.h"
#include "../guard/hive_guard.h"
#include "../guard/hive_guard_sql.h"
#include "../guard/hive_guard_raft.h"

#ifndef HIFS_STORAGE_NODE_HEARTBEAT_MAX_AGE
#define HIFS_STORAGE_NODE_HEARTBEAT_MAX_AGE 30
#endif
#ifndef HIFS_STORAGE_NODE_PERSIST_MIN_SECS
#define HIFS_STORAGE_NODE_PERSIST_MIN_SECS 5
#endif
#ifndef HIFS_STORAGE_NODE_DATA_SYNC_MIN_SECS
#define HIFS_STORAGE_NODE_DATA_SYNC_MIN_SECS (3 * 60)
#endif

#define HIVE_BOOTSTRAP_WEAK __attribute__((weak))

struct SQLDB sqldb HIVE_BOOTSTRAP_WEAK;
unsigned int storage_node_id HIVE_BOOTSTRAP_WEAK;
char storage_node_name[50] HIVE_BOOTSTRAP_WEAK;
char storage_node_address[64] HIVE_BOOTSTRAP_WEAK;
char storage_node_uid[128] HIVE_BOOTSTRAP_WEAK;
char storage_node_serial[100] HIVE_BOOTSTRAP_WEAK;
uint16_t storage_node_guard_port HIVE_BOOTSTRAP_WEAK;
uint16_t storage_node_stripe_port HIVE_BOOTSTRAP_WEAK;
uint64_t storage_node_last_heartbeat HIVE_BOOTSTRAP_WEAK;
uint8_t storage_node_fenced HIVE_BOOTSTRAP_WEAK;
uint32_t storage_node_hive_version HIVE_BOOTSTRAP_WEAK;
uint32_t storage_node_hive_patch_level HIVE_BOOTSTRAP_WEAK;
uint64_t storage_node_last_maintenance HIVE_BOOTSTRAP_WEAK;
char storage_node_maintenance_reason[256] HIVE_BOOTSTRAP_WEAK;
uint64_t storage_node_maintenance_started_at HIVE_BOOTSTRAP_WEAK;
uint64_t storage_node_maintenance_ended_at HIVE_BOOTSTRAP_WEAK;
uint64_t storage_node_date_added_to_cluster HIVE_BOOTSTRAP_WEAK;
uint64_t storage_node_storage_capacity_bytes HIVE_BOOTSTRAP_WEAK;
uint64_t storage_node_storage_used_bytes HIVE_BOOTSTRAP_WEAK;
uint64_t storage_node_storage_reserved_bytes HIVE_BOOTSTRAP_WEAK;
uint64_t storage_node_storage_overhead_bytes HIVE_BOOTSTRAP_WEAK;
uint32_t storage_node_client_connect_timeout_ms HIVE_BOOTSTRAP_WEAK;
uint32_t storage_node_storage_node_connect_timeout_ms HIVE_BOOTSTRAP_WEAK;
#undef HIVE_BOOTSTRAP_WEAK

static struct hive_storage_node cached_local_node;
static bool cached_local_node_valid;

static long hifs_get_host_id(void);
static bool hifs_get_local_node_identity(struct hive_storage_node *node);
static bool hifs_persist_storage_node(uint64_t node_id,
				      const char *node_address,
				      uint16_t node_port);
static uint16_t hifs_local_guard_port(void);
static void hifs_parse_version(int *version_out, int *patch_out);
static bool hifs_execute_query(const char *sql, MYSQL_RES **out_res);
static bool hifs_insert_sql(const char *sql_string);

static const char *hifs_cached_host_serial(void)
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

/* Check that DB is as we expect, then return to config it. */
static bool __attribute__((unused))
check_entries_from_db(struct hg_raft_peer **out_peers,
				   char ***out_addr_bufs,
				   size_t *out_count,
				   uint64_t *out_self_id,
				   const char **out_self_addr)
{
	(void)out_peers;
	(void)out_addr_bufs;
	(void)out_count;
	(void)out_self_id;
	(void)out_self_addr;
	size_t node_count = 0;
	const struct hive_storage_node *nodes;

	init_hive_link();
	if (!hifs_load_storage_nodes())
		return false;

	// We don't expect there to be anything in the DB if this program 
	// is running now. If there is, then WTF?
	nodes = hifs_get_storage_nodes(&node_count);
	if (!nodes || node_count == 0) {
		if (!get_host_info()) {
			return false;
		}
		return true;  // we're all good, let's start the DB config.
	}

	return false;
}

static void __attribute__((unused))
hifs_update_local_storage_node(const struct hive_storage_node *node)
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

static void hifs_safe_strcpy(char *dst, size_t dst_len, const char *src)
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

static bool __attribute__((unused))
hifs_sql_node_data_up_to_date(struct hive_storage_node *local)
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

	bool pending = (local_state_generation > persisted_state_generation);\

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
		hifs_err("Connection to MariaDB failed: %s\n", mysql_error(sqldb.conn));
		mysql_close(sqldb.conn);
		sqldb.conn = NULL;
		return;
	}

	sqldb.sql_init = true;
	unsigned long version = mysql_get_server_version(sqldb.conn);
	hifs_notice("hi_command: Connected to MariaDB server version %lu\n", version);
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

static bool hifs_execute_query(const char *sql, MYSQL_RES **out_res)
{
	if (!sqldb.sql_init || !sqldb.conn) {
		hifs_err("MariaDB connection is not initialised\n");
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

int get_hive_vers(void)
{
	if (!sqldb.sql_init || !sqldb.conn)
		return 0;
	return (int)mysql_get_server_version(sqldb.conn);
}

static inline MYSQL_RES *hifs_execute_sql(const char *sql_string)
{
	MYSQL_RES *res = NULL;
	if (!hifs_execute_query(sql_string, &res))
		return NULL;
	return res;
}

static bool hifs_insert_sql(const char *sql_string)
{
	MYSQL_RES *res = NULL;
	return hifs_execute_query(sql_string, &res);
}

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

static uint16_t hifs_local_guard_port(void)
{
	const char *env = getenv(ENV_LISTEN_PORT);
	long port = env && *env ? strtol(env, NULL, 10) : 0;
	if (port <= 0 || port > UINT16_MAX)
		port = atoi(HIFS_GUARD_PORT_STR);
	return (uint16_t)port;
}

static void hifs_parse_version(int *version_out, int *patch_out)
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

static bool hifs_get_local_node_identity(struct hive_storage_node *node)
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
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	struct addrinfo *res = NULL;

	if (getaddrinfo(hostname, NULL, &hints, &res) == 0) {
		for (struct addrinfo *it = res; it && !have_addr; it = it->ai_next) {
			if (it->ai_family != AF_INET)
				continue;
			struct sockaddr_in *sin = (struct sockaddr_in *)it->ai_addr;
			if (inet_ntop(AF_INET, &sin->sin_addr,
				      node->address, sizeof(node->address))) {
				have_addr = true;
			}
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

static bool hifs_persist_storage_node(uint64_t node_id,
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
