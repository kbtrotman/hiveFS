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
#include <string.h>
#include <sys/stat.h>

#include "hive_bootstrap.h"
#include "../guard/hive_guard.h"
#include "../guard/hive_guard_sql.h"
#include "../guard/hive_guard_raft.h"
#include "../common/hive_common_sql.h"

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
