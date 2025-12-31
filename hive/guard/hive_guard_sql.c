/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

#include <arpa/inet.h>
#include <stdlib.h>
#include <endian.h>
#include <netdb.h>
#include <limits.h>
#include <sys/utsname.h>
#include <openssl/sha.h>

#include "hive_guard.h"
#include "hive_guard_sql.h"
#include "hive_guard_kv.h"
#include "hive_guard_erasure_code.h"
#include "hive_guard_sn_tcp.h"
#include "../common/hive_common_sql.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
static bool hifs_sql_node_data_up_to_date(struct hive_storage_node *local);

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

struct stripe_locations {
	struct stripe_location entries[HIFS_EC_STRIPES];
	size_t count;
	bool ok;
};


static struct stripe_locations hifs_read_block_to_stripe_locations(uint64_t volume_id,
							      uint64_t block_no,
							      const uint8_t hash[HIFS_BLOCK_HASH_SIZE],
							      size_t count);

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

static void bytes_to_hex(const uint8_t *src, size_t len, char *dst)
{
	static const char hexdigits[] = "0123456789abcdef";
	size_t i;

	for (i = 0; i < len; ++i) {
		dst[i * 2] = hexdigits[(src[i] >> 4) & 0xF];
		dst[i * 2 + 1] = hexdigits[src[i] & 0xF];
	}
	dst[len * 2] = '\0';
}

static int hex_nibble(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static bool hex_to_bytes(const char *hex, size_t hex_len, uint8_t *dst, size_t dst_len)
{
	size_t i;

	if (!hex || !dst)
		return false;
	if (hex_len % 2 != 0)
		return false;
	if (dst_len < hex_len / 2)
		return false;

	for (i = 0; i < hex_len / 2; ++i) {
		int hi = hex_nibble(hex[i * 2]);
		int lo = hex_nibble(hex[i * 2 + 1]);
		if (hi < 0 || lo < 0)
			return false;
		dst[i] = (uint8_t)((hi << 4) | lo);
	}
	if (dst_len > hex_len / 2)
		memset(dst + hex_len / 2, 0, dst_len - hex_len / 2);
	return true;
}

static uint32_t sql_to_u32(const char *s)
{
	return s ? (uint32_t)strtoul(s, NULL, 10) : 0;
}

static uint64_t sql_to_u64(const char *s)
{
	return s ? strtoull(s, NULL, 10) : 0ULL;
}

static const char *safe_str(const char *s)
{
	return s ? s : "";
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

static MYSQL_RES *hifs_execute_sql(const char *sql_string)
{
	MYSQL_RES *res = NULL;
	if (!hifs_execute_query(sql_string, &res))
		return NULL;
	return res;
}

MYSQL_RES *hifs_get_hive_host_data(char *machine_id)
{
	char sql_query[MAX_QUERY_SIZE];
	MYSQL_RES *res;

snprintf(sql_query, sizeof(sql_query), SQL_HOST_SELECT_BY_SERIAL,
	 safe_str(machine_id));
res = hifs_execute_sql(sql_query);

	if (!res) {
		sqldb.rows = 0;
		return NULL;
	}

	sqldb.last_query = res;
	sqldb.rows = (int)mysql_num_rows(res);

	return res;
}
static long str_to_long(const char *s)
{
	if (!s)
		return 0;
	return strtol(s, NULL, 10);
}

int get_host_info(void)
{
	char ip_address[64] = {0};
	char *hive_mach_id = NULL;
	long hive_host_id = 0;
	struct hostent *host_entry;
	struct in_addr addr;
	struct utsname uts;

	char name[100] = {0};
	if (gethostname(name, sizeof(name)) != 0) {
		hifs_err("gethostname failed: %s\n", strerror(errno));
		return 0;
	}
	hifs_debug("Host name is [%s]\n", name);

	host_entry = gethostbyname(name);
	if (!host_entry) {
		hifs_crit("gethostbyname failed: %s\n", hstrerror(h_errno));
		return 0;
	}

	memcpy(&addr.s_addr, host_entry->h_addr_list[0], host_entry->h_length);
	snprintf(ip_address, sizeof(ip_address), "%s", inet_ntoa(addr));
	hifs_debug("IP address is [%s]\n", ip_address);

	if (uname(&uts) != 0) {
		hifs_err("uname failed: %s\n", strerror(errno));
		return 0;
	}

	hive_mach_id = hifs_get_machine_id();
	hive_host_id = hifs_get_host_id();
	if (hive_mach_id)
		hifs_safe_strcpy(storage_node_uid, sizeof(storage_node_uid), hive_mach_id);
	snprintf(storage_node_serial, sizeof(storage_node_serial), "%ld", hive_host_id);
	hifs_safe_strcpy(storage_node_name, sizeof(storage_node_name), name);
	hifs_safe_strcpy(storage_node_address, sizeof(storage_node_address), ip_address);

	hifs_info("Hive machine ID is [%s] and host ID is [%ld]\n",
		  hive_mach_id ? hive_mach_id : "unknown", hive_host_id);
	return 1;
}

bool get_hive_host_data(void)
{

	char sql_query[MAX_QUERY_SIZE];
	char ip_address[64] = {0};
	char *hive_mach_id = hifs_get_machine_id();
	long hive_host_id = hifs_get_host_id();
	struct hostent *host_entry;
	struct in_addr addr;
	struct utsname uts;
	char name[100] = {0};
	MYSQL_RES *res = hifs_get_hive_host_data(hive_mach_id);
	if (!res) {
		hifs_err("Failed to query hive host data\n");
		return 0;
	}

	MYSQL_ROW row;
	unsigned int row_count = 0;
	mysql_data_seek(res, 0);
	while ((row = mysql_fetch_row(res)) != NULL) {
		row_count++;
		sqldb.host.serial = row[0];
		sqldb.host.name = row[1];
		sqldb.host.host_id = row[2] ? str_to_long(row[2]) : 0;
		sqldb.host.os_name = row[3];
		sqldb.host.os_version = row[4];
		sqldb.host.create_time = row[5];

		hifs_debug("serial: %s, name: %s, host_id: %ld, os: %s %s, created: %s",
			   safe_str(sqldb.host.serial), safe_str(sqldb.host.name),
			   sqldb.host.host_id,
			   safe_str(sqldb.host.os_name),
			   safe_str(sqldb.host.os_version),
			   safe_str(sqldb.host.create_time));
	}

	if (row_count > 0)
		return 1;

	if (gethostname(name, sizeof(name)) != 0) {
		hifs_err("gethostname failed: %s\n", strerror(errno));
		return 0;
	}

	host_entry = gethostbyname(name);
	if (!host_entry) {
		hifs_err("gethostbyname failed: %s\n", hstrerror(h_errno));
		return 0;
	}

	memcpy(&addr.s_addr, host_entry->h_addr_list[0], host_entry->h_length);
	snprintf(ip_address, sizeof(ip_address), "%s", inet_ntoa(addr));

	if (uname(&uts) != 0) {
		hifs_err("uname failed: %s\n", strerror(errno));
		return 0;
	}

	hive_mach_id = hifs_get_machine_id();
	hive_host_id = hifs_get_host_id();

	hifs_notice("This host does not exist in the hive. Filesystems cannot be mounted without a hive connection.");
	hifs_notice("Would you like to add this host to the hive? [y/n]\n");

	char response = 'n';
	if (scanf(" %c", &response) != 1 || (response != 'y' && response != 'Y')) {
		hifs_notice("Not registering host to Hive.");
		return 0;
	}

	sqldb.host.serial = hive_mach_id;
	sqldb.host.name = name;
	sqldb.host.host_id = hive_host_id;
	sqldb.host.os_name = uts.sysname;
	sqldb.host.os_version = uts.release;
	sqldb.host.create_time = NULL;

	char *serial_q = hifs_get_quoted_value(hive_mach_id);
	char *name_q = hifs_get_quoted_value(name);
	char *addr_q = hifs_get_quoted_value(ip_address);
	char *os_name_q = hifs_get_quoted_value(uts.sysname);
	char *os_version_q = hifs_get_quoted_value(uts.release);
	if (!serial_q || !name_q || !addr_q || !os_name_q || !os_version_q) {
		free(serial_q);
		free(name_q);
		free(addr_q);
		free(os_name_q);
		free(os_version_q);
		hifs_err("Out of memory while preparing host registration query");
		return 0;
	}

	unsigned int hicom_port = hifs_local_guard_port();
	uint64_t epoch = (uint64_t)time(NULL);
	unsigned int fenced = 0;

	snprintf(sql_query, sizeof(sql_query), SQL_HOST_UPSERT,
		 safe_str(serial_q), safe_str(name_q), hive_host_id,
		 safe_str(addr_q), safe_str(os_name_q), safe_str(os_version_q),
		 hicom_port, (unsigned long long)epoch, fenced);

	free(serial_q);
	free(name_q);
	free(addr_q);
	free(os_name_q);
	free(os_version_q);

	hifs_notice("Registering host to Hive.");
	if (!hifs_insert_sql(sql_query))
		return 0;

	hifs_info("Registered host to hive: name [%s] machine ID [%s] host ID [%ld] "
		  "IP address [%s] OS [%s %s]",
		  name, hive_mach_id, hive_host_id, ip_address,
		  uts.sysname, uts.release);
	return 1;
}

char *hifs_get_quoted_value(const char *in_str)
{
	if (!sqldb.sql_init || !sqldb.conn || !in_str)
		return NULL;

	size_t len = strlen(in_str);
	char *escaped = malloc(len * 2 + 1);
	if (!escaped)
		return NULL;

	unsigned long written = mysql_real_escape_string(sqldb.conn, escaped, in_str, len);
	escaped[written] = '\0';
	return escaped;
}

char *hifs_get_unquoted_value(const char *in_str)
{
	if (!in_str)
		return NULL;
	return strdup(in_str);
}

void hifs_release_query(void)
{
	if (sqldb.last_query) {
		mysql_free_result(sqldb.last_query);
		sqldb.last_query = NULL;
	}
}

bool hifs_insert_data(const char *q_string)
{
	if (!q_string)
		return false;

	if (!hifs_insert_sql(q_string))
		return false;

	hifs_debug("Inserted %llu rows\n", sqldb.last_affected);
	return true;
}

int hifs_get_hive_host_sbs(void)
{
	char sql_query[MAX_QUERY_SIZE];
	MYSQL_RES *res;

	snprintf(sql_query, sizeof(sql_query), "%s", SQL_VOLUME_SUPER_LIST);
	res = hifs_execute_sql(sql_query);
	if (!res) {
		sqldb.rows = 0;
		return 0;
	}

	sqldb.rows = (int)mysql_num_rows(res);
	mysql_data_seek(res, 0);

	for (int i = 0; i < sqldb.rows && i < (int)ARRAY_SIZE(sqldb.sb); ++i) {
		MYSQL_ROW row = mysql_fetch_row(res);
		if (!row)
			break;

		sqldb.sb[i].volume_id = row[0] ? strtoull(row[0], NULL, 10) : 0;
		sqldb.sb[i].s_magic = row[1] ? (uint32_t)strtoul(row[1], NULL, 10) : 0;
		sqldb.sb[i].s_blocksize = row[2] ? (uint32_t)strtoul(row[2], NULL, 10) : 0;
		sqldb.sb[i].s_blocksize_bits = row[3] ? (uint32_t)strtoul(row[3], NULL, 10) : 0;
		sqldb.sb[i].s_blocks_count = row[4] ? strtoull(row[4], NULL, 10) : 0;
		sqldb.sb[i].s_free_blocks = row[5] ? strtoull(row[5], NULL, 10) : 0;
		sqldb.sb[i].s_inodes_count = row[6] ? strtoull(row[6], NULL, 10) : 0;
		sqldb.sb[i].s_free_inodes = row[7] ? strtoull(row[7], NULL, 10) : 0;

		hifs_debug("volume_id=%llu magic=%#x blocksize=%u blocks=%llu free_blocks=%llu "
			   "inodes=%llu free_inodes=%llu",
			   (unsigned long long)sqldb.sb[i].volume_id,
			   sqldb.sb[i].s_magic,
			   sqldb.sb[i].s_blocksize,
			   (unsigned long long)sqldb.sb[i].s_blocks_count,
			   (unsigned long long)sqldb.sb[i].s_free_blocks,
			   (unsigned long long)sqldb.sb[i].s_inodes_count,
			   (unsigned long long)sqldb.sb[i].s_free_inodes);
	}

	return sqldb.rows > 0;
}

bool hifs_volume_super_get(uint64_t volume_id, struct hifs_volume_superblock *out)
{
	char sql_query[MAX_QUERY_SIZE];
	MYSQL_RES *res;
	MYSQL_ROW row;
	unsigned long *lengths;
	bool ok = false;

	if (!sqldb.sql_init || !sqldb.conn || !out)
		return false;

	snprintf(sql_query, sizeof(sql_query), SQL_VOLUME_SUPER_SELECT,
		 (unsigned long long)volume_id);

	res = hifs_execute_sql(sql_query);
	if (!res)
		return false;

	if (mysql_num_rows(res) == 0)
		goto out;

	row = mysql_fetch_row(res);
	lengths = mysql_fetch_lengths(res);
	if (!row || !lengths)
		goto out;

	memset(out, 0, sizeof(*out));
	out->s_magic = row[0] ? (uint32_t)strtoul(row[0], NULL, 10) : 0;
	out->s_blocksize = row[1] ? (uint32_t)strtoul(row[1], NULL, 10) : 0;
	out->s_blocksize_bits = row[2] ? (uint32_t)strtoul(row[2], NULL, 10) : 0;
	out->s_blocks_count = row[3] ? strtoull(row[3], NULL, 10) : 0;
	out->s_free_blocks = row[4] ? strtoull(row[4], NULL, 10) : 0;
	out->s_inodes_count = row[5] ? strtoull(row[5], NULL, 10) : 0;
	out->s_free_inodes = row[6] ? strtoull(row[6], NULL, 10) : 0;
	out->s_maxbytes = row[7] ? strtoull(row[7], NULL, 10) : 0;
	out->s_feature_compat = row[8] ? (uint32_t)strtoul(row[8], NULL, 10) : 0;
	out->s_feature_ro_compat = row[9] ? (uint32_t)strtoul(row[9], NULL, 10) : 0;
	out->s_feature_incompat = row[10] ? (uint32_t)strtoul(row[10], NULL, 10) : 0;
	out->s_rev_level = row[12] ? (uint32_t)strtoul(row[12], NULL, 10) : 0;
	out->s_wtime = row[13] ? (uint32_t)strtoul(row[13], NULL, 10) : 0;
	out->s_flags = row[14] ? (uint32_t)strtoul(row[14], NULL, 10) : 0;

	if (!row[11] || !hex_to_bytes(row[11], lengths[11], out->s_uuid,
				     sizeof(out->s_uuid)))
		goto out;
	if (row[15] && lengths[15] > 0) {
		if (!hex_to_bytes(row[15], lengths[15],
				 (uint8_t *)out->s_volume_name,
				 sizeof(out->s_volume_name)))
			goto out;
	} else {
		memset(out->s_volume_name, 0, sizeof(out->s_volume_name));
	}

	ok = true;

out:
	mysql_free_result(res);
	sqldb.last_query = NULL;
	return ok;
}

bool hifs_volume_super_set(uint64_t volume_id, const struct hifs_volume_superblock *vsb)
{
	char sql_query[MAX_QUERY_SIZE];
	char uuid_hex[sizeof(vsb->s_uuid) * 2 + 1];
	char name_hex[sizeof(vsb->s_volume_name) * 2 + 1];

	if (!sqldb.sql_init || !sqldb.conn || !vsb)
		return false;

	bytes_to_hex(vsb->s_uuid, sizeof(vsb->s_uuid), uuid_hex);
	bytes_to_hex((const uint8_t *)vsb->s_volume_name,
		     sizeof(vsb->s_volume_name), name_hex);

	snprintf(sql_query, sizeof(sql_query), SQL_VOLUME_SUPER_UPSERT,
		 (unsigned long long)volume_id,
		 vsb->s_magic, vsb->s_blocksize, vsb->s_blocksize_bits,
		 (unsigned long long)vsb->s_blocks_count,
		 (unsigned long long)vsb->s_free_blocks,
		 (unsigned long long)vsb->s_inodes_count,
		 (unsigned long long)vsb->s_free_inodes,
		 (unsigned long long)vsb->s_maxbytes,
		 vsb->s_feature_compat, vsb->s_feature_ro_compat, vsb->s_feature_incompat,
		 uuid_hex, vsb->s_rev_level, vsb->s_wtime, vsb->s_flags, name_hex);

	return hifs_insert_sql(sql_query);
}

bool hifs_root_dentry_load(uint64_t volume_id, struct hifs_volume_root_dentry *out)
{
	char sql_query[MAX_QUERY_SIZE];
	MYSQL_RES *res;
	MYSQL_ROW row;
	unsigned long *lengths;
	bool ok = false;

	if (!sqldb.sql_init || !sqldb.conn || !out)
		return false;

	snprintf(sql_query, sizeof(sql_query), SQL_ROOT_DENTRY_SELECT,
		 (unsigned long long)volume_id);

	res = hifs_execute_sql(sql_query);
	if (!res)
		return false;

	if (mysql_num_rows(res) == 0)
		goto out;

	row = mysql_fetch_row(res);
	lengths = mysql_fetch_lengths(res);
	if (!row || !lengths)
		goto out;

	memset(out, 0, sizeof(*out));
	out->rd_inode = row[0] ? strtoull(row[0], NULL, 10) : 0;
	out->rd_mode = row[1] ? (uint32_t)strtoul(row[1], NULL, 10) : 0;
	out->rd_uid = row[2] ? (uint32_t)strtoul(row[2], NULL, 10) : 0;
	out->rd_gid = row[3] ? (uint32_t)strtoul(row[3], NULL, 10) : 0;
	out->rd_flags = row[4] ? (uint32_t)strtoul(row[4], NULL, 10) : 0;
	out->rd_size = row[5] ? strtoull(row[5], NULL, 10) : 0;
	out->rd_blocks = row[6] ? strtoull(row[6], NULL, 10) : 0;
	out->rd_atime = row[7] ? (uint32_t)strtoul(row[7], NULL, 10) : 0;
	out->rd_mtime = row[8] ? (uint32_t)strtoul(row[8], NULL, 10) : 0;
	out->rd_ctime = row[9] ? (uint32_t)strtoul(row[9], NULL, 10) : 0;
	out->rd_links = row[10] ? (uint32_t)strtoul(row[10], NULL, 10) : 0;
	out->rd_name_len = row[11] ? (uint32_t)strtoul(row[11], NULL, 10) : 0;

	if (row[12] && lengths[12] > 0) {
		if (!hex_to_bytes(row[12], lengths[12],
				  (uint8_t *)out->rd_name,
				  sizeof(out->rd_name)))
			goto out;
	} else {
		memset(out->rd_name, 0, sizeof(out->rd_name));
	}

	ok = true;

out:
	mysql_free_result(res);
	sqldb.last_query = NULL;
	return ok;
}

bool hifs_root_dentry_store(uint64_t volume_id, const struct hifs_volume_root_dentry *root)
{
	char sql_query[MAX_QUERY_SIZE];
	char name_hex[HIFS_MAX_NAME_SIZE * 2 + 1];
	uint32_t name_len;

	if (!sqldb.sql_init || !sqldb.conn || !root)
		return false;

	name_len = root->rd_name_len;
	if (name_len > HIFS_MAX_NAME_SIZE)
		name_len = HIFS_MAX_NAME_SIZE;
	bytes_to_hex((const uint8_t *)root->rd_name, name_len, name_hex);

	snprintf(sql_query, sizeof(sql_query), SQL_ROOT_DENTRY_UPSERT,
		 (unsigned long long)volume_id,
		 (unsigned long long)root->rd_inode,
		 root->rd_mode, root->rd_uid, root->rd_gid, root->rd_flags,
		 (unsigned long long)root->rd_size,
		 (unsigned long long)root->rd_blocks,
		 root->rd_atime, root->rd_mtime, root->rd_ctime,
		 root->rd_links, root->rd_name_len, name_hex);

	return hifs_insert_sql(sql_query);
}

bool hifs_volume_dentry_load_by_inode(uint64_t volume_id, uint64_t inode,
				 struct hifs_volume_dentry *out)
{
	char sql_query[MAX_QUERY_SIZE];
	MYSQL_RES *res;
	MYSQL_ROW row;
	unsigned long *lengths;
	bool ok = false;

	if (!sqldb.sql_init || !sqldb.conn || !out)
		return false;

	snprintf(sql_query, sizeof(sql_query), SQL_DENTRY_BY_INODE,
		 (unsigned long long)volume_id, (unsigned long long)inode);

	res = hifs_execute_sql(sql_query);
	if (!res)
		return false;

	if (mysql_num_rows(res) == 0)
		goto out;

	row = mysql_fetch_row(res);
	lengths = mysql_fetch_lengths(res);
	if (!row || !lengths)
		goto out;

	memset(out, 0, sizeof(*out));
	out->de_parent = row[0] ? strtoull(row[0], NULL, 10) : 0;
	out->de_inode = row[1] ? strtoull(row[1], NULL, 10) : 0;
	out->de_epoch = row[2] ? (uint32_t)strtoul(row[2], NULL, 10) : 0;
	out->de_type = row[3] ? (uint32_t)strtoul(row[3], NULL, 10) : 0;
	out->de_name_len = row[4] ? (uint32_t)strtoul(row[4], NULL, 10) : 0;

	if (row[5] && lengths[5] > 0) {
		if (!hex_to_bytes(row[5], lengths[5],
				 (uint8_t *)out->de_name,
				 sizeof(out->de_name)))
			goto out;
	} else {
		memset(out->de_name, 0, sizeof(out->de_name));
	}

	ok = true;

out:
	mysql_free_result(res);
	sqldb.last_query = NULL;
	return ok;
}

bool hifs_volume_dentry_load_by_name(uint64_t volume_id, uint64_t parent,
				 const char *name_hex, uint32_t name_hex_len,
				 struct hifs_volume_dentry *out)
{
	char sql_query[MAX_QUERY_SIZE];
	MYSQL_RES *res;
	MYSQL_ROW row;
	unsigned long *lengths;
	bool ok = false;

	if (!sqldb.sql_init || !sqldb.conn || !out || !name_hex)
		return false;

	snprintf(sql_query, sizeof(sql_query), SQL_DENTRY_BY_NAME,
		 (unsigned long long)volume_id,
		 (unsigned long long)parent,
		 (int)name_hex_len,
		 name_hex);

	res = hifs_execute_sql(sql_query);
	if (!res)
		return false;

	if (mysql_num_rows(res) == 0)
		goto out;

	row = mysql_fetch_row(res);
	lengths = mysql_fetch_lengths(res);
	if (!row || !lengths)
		goto out;

	memset(out, 0, sizeof(*out));
	out->de_parent = row[0] ? strtoull(row[0], NULL, 10) : 0;
	out->de_inode = row[1] ? strtoull(row[1], NULL, 10) : 0;
	out->de_epoch = row[2] ? (uint32_t)strtoul(row[2], NULL, 10) : 0;
	out->de_type = row[3] ? (uint32_t)strtoul(row[3], NULL, 10) : 0;
	out->de_name_len = row[4] ? (uint32_t)strtoul(row[4], NULL, 10) : 0;
	if (row[5] && lengths[5] > 0) {
		if (!hex_to_bytes(row[5], lengths[5],
				 (uint8_t *)out->de_name,
				 sizeof(out->de_name)))
			goto out;
	} else {
		memset(out->de_name, 0, sizeof(out->de_name));
	}

	ok = true;

out:
	mysql_free_result(res);
	sqldb.last_query = NULL;
	return ok;
}

bool hifs_volume_dentry_store(uint64_t volume_id,
				 const struct hifs_volume_dentry *dent)
{
	char sql_query[MAX_QUERY_SIZE];
	char delete_query[MAX_QUERY_SIZE];
	char name_hex[HIFS_MAX_NAME_SIZE * 2 + 1];
	uint32_t name_len;

	if (!sqldb.sql_init || !sqldb.conn || !dent)
		return false;

	name_len = dent->de_name_len;
	if (name_len > HIFS_MAX_NAME_SIZE)
		name_len = HIFS_MAX_NAME_SIZE;
	bytes_to_hex((const uint8_t *)dent->de_name, name_len, name_hex);

	hifs_debug("volume_dentry_store: vol=%llu parent=%llu inode=%llu epoch=%u "
		   "type=%u name_len=%u name_hex=%s",
		   (unsigned long long)volume_id,
		   (unsigned long long)dent->de_parent,
		   (unsigned long long)dent->de_inode,
		   dent->de_epoch,
		   dent->de_type,
		   dent->de_name_len,
		   name_hex);

	snprintf(delete_query, sizeof(delete_query), SQL_DENTRY_DELETE_BY_NAME,
		 (unsigned long long)volume_id,
		 (unsigned long long)dent->de_parent,
		 (int)(name_len * 2),
		 name_hex);
	hifs_insert_sql(delete_query);

	snprintf(sql_query, sizeof(sql_query), SQL_DENTRY_UPSERT,
		 (unsigned long long)volume_id,
		 (unsigned long long)dent->de_parent,
		 (unsigned long long)dent->de_inode,
		 dent->de_epoch,
		 dent->de_type,
		 dent->de_name_len,
		 name_hex);

	bool ok = hifs_insert_sql(sql_query);
	hifs_debug("volume_dentry_store: metadata write %s (affected=%llu last_id=%llu)",
		   ok ? "succeeded" : "failed",
		   (unsigned long long)sqldb.last_affected,
		   (unsigned long long)sqldb.last_insert_id);
	return ok;
}

bool hifs_volume_inode_load(uint64_t volume_id, uint64_t inode,
				 struct hifs_inode_wire *out)
{
	char sql_query[MAX_QUERY_SIZE];
	MYSQL_RES *res = NULL;
	MYSQL_ROW row;
	unsigned long *lengths;
	bool ok = false;
	int idx = 0;

	if (!sqldb.sql_init || !sqldb.conn || !out)
		return false;

	snprintf(sql_query, sizeof(sql_query), SQL_VOLUME_INODE_SELECT,
		 (unsigned long long)volume_id,
		 (unsigned long long)inode);

	res = hifs_execute_sql(sql_query);
	if (!res)
		return false;

	if (mysql_num_rows(res) == 0)
		goto out;

	row = mysql_fetch_row(res);
	lengths = mysql_fetch_lengths(res);
	if (!row || !lengths)
		goto out;

	memset(out, 0, sizeof(*out));

	out->i_msg_flags = htole32(sql_to_u32(row[idx++]));
	out->i_version = row[idx] ? (uint8_t)strtoul(row[idx], NULL, 10) : 0;
	idx++;
	out->i_flags = row[idx] ? (uint8_t)strtoul(row[idx], NULL, 10) : 0;
	idx++;
	out->i_mode = htole32(sql_to_u32(row[idx++]));
	out->i_ino = htole64(sql_to_u64(row[idx++]));
	out->i_uid = htole16((uint16_t)sql_to_u32(row[idx++]));
	out->i_gid = htole16((uint16_t)sql_to_u32(row[idx++]));
	out->i_hrd_lnk = htole16((uint16_t)sql_to_u32(row[idx++]));
	out->i_atime = htole32(sql_to_u32(row[idx++]));
	out->i_mtime = htole32(sql_to_u32(row[idx++]));
	out->i_ctime = htole32(sql_to_u32(row[idx++]));
	out->i_size = htole32(sql_to_u32(row[idx++]));

	if (!row[idx] || !hex_to_bytes(row[idx], lengths[idx],
				       out->i_name, sizeof(out->i_name)))
		goto out;
	idx++;

	for (size_t i = 0; i < HIFS_INODE_TSIZE; ++i)
		out->extents[i].block_start = htole32(sql_to_u32(row[idx++]));
	for (size_t i = 0; i < HIFS_INODE_TSIZE; ++i)
		out->extents[i].block_count = htole32(sql_to_u32(row[idx++]));
	out->i_blocks = htole32(sql_to_u32(row[idx++]));
	out->i_bytes = htole32(sql_to_u32(row[idx++]));
	out->i_links = row[idx] ? (uint8_t)strtoul(row[idx], NULL, 10) : 0;
	idx++;
	out->i_hash_count = htole16((uint16_t)sql_to_u32(row[idx++]));
	out->i_hash_reserved = htole16((uint16_t)sql_to_u32(row[idx++]));

	/* Load fingerprints from RocksDB */
	uint16_t hash_count_host = le16toh(out->i_hash_count);
	for (uint16_t i = 0; i < hash_count_host && i < HIFS_MAX_BLOCK_HASHES; ++i) {
		struct hifs_block_fingerprint_wire fp = {0};
		if (hg_kv_get_vif_entry(volume_id, inode, i, &fp) == 0) {
			out->i_block_fingerprints[i] = fp;
		} else {
			memset(&out->i_block_fingerprints[i], 0,
			       sizeof(out->i_block_fingerprints[i]));
		}
	}

	ok = true;

out:
	if (res) {
		mysql_free_result(res);
		sqldb.last_query = NULL;
	}
	return ok;
}

bool hifs_volume_inode_store(uint64_t volume_id,
			 const struct hifs_inode_wire *inode)
{
	char *sql_query = NULL;
	char name_hex[HIFS_MAX_NAME_SIZE * 2 + 1];
	uint32_t epoch;
	uint64_t ino_host;
	uint32_t msg_flags, mode, atime, mtime, ctime, size;
	uint32_t extent_start[HIFS_INODE_TSIZE];
	uint32_t extent_count[HIFS_INODE_TSIZE];
	uint32_t blocks, bytes;
	uint16_t uid, gid, hrd_lnk, hash_count, hash_reserved;
	uint8_t version, flags, links;

	if (!sqldb.sql_init || !sqldb.conn || !inode)
		return false;

	msg_flags = le32toh(inode->i_msg_flags);
	version = inode->i_version;
	flags = inode->i_flags;
	mode = le32toh(inode->i_mode);
	ino_host = le64toh(inode->i_ino);
	uid = le16toh(inode->i_uid);
	gid = le16toh(inode->i_gid);
	hrd_lnk = le16toh(inode->i_hrd_lnk);
	atime = le32toh(inode->i_atime);
	mtime = le32toh(inode->i_mtime);
	ctime = le32toh(inode->i_ctime);
	size = le32toh(inode->i_size);
	bytes_to_hex((const uint8_t *)inode->i_name, sizeof(inode->i_name), name_hex);
	for (size_t i = 0; i < HIFS_INODE_TSIZE; ++i) {
		extent_start[i] = le32toh(inode->extents[i].block_start);
		extent_count[i] = le32toh(inode->extents[i].block_count);
	}
	blocks = le32toh(inode->i_blocks);
	bytes = le32toh(inode->i_bytes);
	links = inode->i_links;
	hash_count = le16toh(inode->i_hash_count);
	hash_reserved = le16toh(inode->i_hash_reserved);
	epoch = ctime;

	hifs_debug("volume_inode_store: vol=%llu ino=%llu mode=%#x uid=%u gid=%u "
		   "size=%u blocks=%u bytes=%u links=%u hash_count=%u epoch=%u "
		   "name_hex=%s extent_start=[%u,%u,%u,%u] extent_count=[%u,%u,%u,%u]",
		   (unsigned long long)volume_id,
		   (unsigned long long)ino_host,
		   mode,
		   uid,
		   gid,
		   size,
		   blocks,
		   bytes,
		   links,
		   hash_count,
		   epoch,
		   name_hex,
		   extent_start[0], extent_start[1], extent_start[2], extent_start[3],
		   extent_count[0], extent_count[1], extent_count[2], extent_count[3]);

	size_t sql_len = snprintf(NULL, 0, SQL_VOLUME_INODE_UPSERT,
				  (unsigned long long)volume_id,
				  (unsigned long long)ino_host,
				  msg_flags, version, flags, mode,
				  (unsigned long long)ino_host, uid, gid,
				  hrd_lnk, atime, mtime, ctime, size, name_hex,
				  extent_start[0], extent_start[1], extent_start[2], extent_start[3],
				  extent_count[0], extent_count[1], extent_count[2], extent_count[3],
				  blocks, bytes, links, hash_count, hash_reserved, epoch) + 1;
	sql_query = malloc(sql_len);
	if (!sql_query)
		return false;

	snprintf(sql_query, sql_len, SQL_VOLUME_INODE_UPSERT,
		 (unsigned long long)volume_id,
		 (unsigned long long)ino_host,
		 msg_flags, version, flags, mode,
		 (unsigned long long)ino_host, uid, gid,
		 hrd_lnk, atime, mtime, ctime, size, name_hex,
		 extent_start[0], extent_start[1], extent_start[2], extent_start[3],
		 extent_count[0], extent_count[1], extent_count[2], extent_count[3],
		 blocks, bytes, links, hash_count, hash_reserved, epoch);

	bool insert_ok = hifs_insert_sql(sql_query);
	hifs_debug("volume_inode_store: metadata write %s (affected=%llu last_id=%llu)",
		   insert_ok ? "succeeded" : "failed",
		   (unsigned long long)sqldb.last_affected,
		   (unsigned long long)sqldb.last_insert_id);
	free(sql_query);
	if (!insert_ok)
		return false;

	bool fp_ok = hifs_volume_inode_fp_sync(volume_id,
					       ino_host,
					       inode->i_block_fingerprints,
					       hash_count);
	hifs_debug("volume_inode_store: fingerprint sync %s (hash_count=%u)",
		   fp_ok ? "succeeded" : "failed",
		   hash_count);
	if (!fp_ok)
		return false;

	return true;
}

static size_t count_present_chunks(uint8_t **chunks, size_t total)
{
	size_t present = 0;
	if (!chunks)
		return 0;
	for (size_t i = 0; i < total; ++i) {
		if (chunks[i])
			++present;
	}
	return present;
}

static const struct hive_storage_node *find_storage_node(uint32_t node_id)
{
	size_t count = 0;
	const struct hive_storage_node *nodes = hifs_get_storage_nodes(&count);
	if (!nodes)
		return NULL;
	for (size_t i = 0; i < count; ++i) {
		if (nodes[i].id == node_id)
			return &nodes[i];
	}
	return NULL;
}

static bool resolve_node_endpoint(const struct hive_storage_node *node,
					 char *host,
					 size_t host_len,
					 uint16_t *port_out)
{
	if (!node || !host || host_len == 0 || !port_out)
		return false;

	const char *addr = node->address;
	const char *host_start = addr;
	size_t copy_len = 0;
	const char *port_str = NULL;

	if (addr && *addr) {
		if (addr[0] == '[') {
			const char *end = strchr(addr, ']');
			if (end && end > addr + 1) {
				host_start = addr + 1;
				copy_len = (size_t)(end - host_start);
				if (*(end + 1) == ':')
					port_str = end + 2;
			}
		}
		if (!port_str) {
			const char *colon = strrchr(addr, ':');
			if (colon && strchr(colon + 1, ':') == NULL) {
				host_start = addr;
				copy_len = (size_t)(colon - addr);
				port_str = colon + 1;
			}
		}
		if (copy_len == 0)
			copy_len = strlen(host_start);
	} else {
		host_start = "127.0.0.1";
		copy_len = strlen(host_start);
	}

	if (copy_len >= host_len)
		copy_len = host_len - 1;
	memcpy(host, host_start, copy_len);
	host[copy_len] = '\0';
	if (!*host)
		snprintf(host, host_len, "%s", "127.0.0.1");

	uint16_t port = node->stripe_port;
	if (port_str) {
		char *endptr = NULL;
		unsigned long val = strtoul(port_str, &endptr, 10);
		if (endptr && *endptr == '\0' && val > 0 && val <= UINT16_MAX)
			port = (uint16_t)val;
	}
	if (!port)
		port = HIFS_STRIPE_TCP_DEFAULT_PORT;
	*port_out = port;
	return true;
}

static bool fetch_stripe_via_tcp(const struct stripe_location *loc,
				   uint8_t **out_data,
				   size_t *out_len)
{
	if (!loc || !out_data || !out_len)
		return false;
	if (loc->storage_node_id == 0 || loc->estripe_id == 0)
		return false;

	const struct hive_storage_node *node = find_storage_node(loc->storage_node_id);
	if (!node) {
		hifs_warning("stripe fetch: storage node %u missing for estripe %llu",
			     loc->storage_node_id,
			     (unsigned long long)loc->estripe_id);
		return false;
	}

	char host[64];
	uint16_t port = 0;
	if (!resolve_node_endpoint(node, host, sizeof(host), &port))
		return false;

	if (hifs_sn_tcp_fetch(node->id,
			        loc->shard_id,
			        host,
			        port,
			        loc->estripe_id,
			        out_data,
			        out_len) != 0) {
		hifs_warning("stripe fetch: TCP fetch failed for node %u (%s:%u) estripe %llu",
			     node->id,
			     host,
			     (unsigned)port,
			     (unsigned long long)loc->estripe_id);
		return false;
	}

	return true;
}

static bool fetch_stripe_from_node(const struct stripe_location *loc,
				     uint8_t **out_data,
				     size_t *out_len)
{
	if (!loc || !out_data || !out_len || loc->estripe_id == 0)
		return false;

	if (fetch_stripe_via_tcp(loc, out_data, out_len))
		return true;

	if (hg_kv_get_estripe_chunk(loc->estripe_id, out_data, out_len) == 0)
		return true;

	hifs_warning("stripe fetch: unable to locate estripe %llu (node %u shard %u)",
		     (unsigned long long)loc->estripe_id,
		     loc->storage_node_id,
		     loc->shard_id);
	return false;
}

static void fetch_remote_stripes(const struct stripe_locations *locs,
					   uint8_t **encoded_chunks,
					   size_t total_slots,
					   size_t *frag_size,
					   size_t min_required)
{
	if (!locs || !locs->ok || locs->count == 0 || !encoded_chunks || total_slots == 0)
		return;

	size_t have = count_present_chunks(encoded_chunks, total_slots);
	for (size_t i = 0; i < locs->count; ++i) {
		const struct stripe_location *loc = &locs->entries[i];
		if (loc->stripe_index >= total_slots)
			continue;
		if (encoded_chunks[loc->stripe_index])
			continue;

		uint8_t *chunk = NULL;
		size_t chunk_len = 0;
		if (!fetch_stripe_from_node(loc, &chunk, &chunk_len))
			continue;

		if (*frag_size == 0) {
			*frag_size = chunk_len;
		} else if (chunk_len != *frag_size) {
			hifs_warning("Remote stripe %u size mismatch: expected %zu got %zu",
				     (unsigned)loc->stripe_index,
				     *frag_size,
				     chunk_len);
			free(chunk);
			continue;
		}

		encoded_chunks[loc->stripe_index] = chunk;
		++have;
		if (min_required && have >= min_required)
			break;
	}
}

bool hifs_volume_block_load(uint64_t volume_id, uint64_t block_no,
                            uint8_t *buf, uint32_t *len)
{
    bool ok = false;
    unsigned int hash_algo = 0;
    char hash_hex[sizeof(((struct VbEntry *)0)->block_hash) * 2 + 1];
    struct VbEntry vb_entry = {0};
    struct H2SEntry h2s = {0};

    if (!sqldb.sql_init || !sqldb.conn || !buf || !len)
        return false;

    /* 0) Ensure EC is initialized */
    if (hifs_ec_ensure_init() != 0) {
        if (hicomm_erasure_coding_init() != 0) {
            hifs_warning("EC init failed in load");
            return false;
        }
    }


	    if (hg_kv_get_vb_entry(volume_id, block_no, &vb_entry) != 0) {
	        hifs_warning("No block metadata for volume=%llu block=%llu",
	                     (unsigned long long)volume_id,
                     (unsigned long long)block_no);
        return false;
    }

    hash_algo = vb_entry.hash_algo;
    if (hash_algo == HIFS_HASH_ALGO_NONE) {
        hifs_warning("Invalid hash algo for volume=%llu block=%llu",
                     (unsigned long long)volume_id,
                     (unsigned long long)block_no);
        return false;
    }
    bytes_to_hex(vb_entry.block_hash, sizeof(vb_entry.block_hash), hash_hex);

    if (hg_kv_get_h2s(hash_algo, vb_entry.block_hash, &h2s) != 0) {
        hifs_warning("Missing hash->stripe mapping for volume=%llu block=%llu",
                     (unsigned long long)volume_id,
                     (unsigned long long)block_no);
        return false;
    }

    enum { NUM_DATA = HIFS_EC_K, NUM_PARITY = HIFS_EC_M, TOTAL = HIFS_EC_K + HIFS_EC_M };
	    uint64_t stripe_ids[TOTAL];
	    size_t h2s_count = sizeof(h2s.estripe_ids) / sizeof(h2s.estripe_ids[0]);
	    size_t copy = (TOTAL < h2s_count) ? TOTAL : h2s_count;
	    memcpy(stripe_ids, h2s.estripe_ids, copy * sizeof(uint64_t));
	    for (size_t i = copy; i < TOTAL; ++i)
	        stripe_ids[i] = 0;

	    uint8_t *encoded_chunks[TOTAL] = {0};
	    size_t frag_size = 0;
	    struct stripe_locations stripe_locs =
	        hifs_read_block_to_stripe_locations(volume_id, block_no,
	                                            vb_entry.block_hash, TOTAL);
	    fetch_remote_stripes(&stripe_locs, encoded_chunks, TOTAL, &frag_size, NUM_DATA);
	    size_t found = count_present_chunks(encoded_chunks, TOTAL);

	    for (size_t i = 0; i < NUM_DATA && found < NUM_DATA; ++i) {
	        if (stripe_ids[i] == 0 || encoded_chunks[i])
	            continue;
	        uint8_t *chunk = NULL;
	        size_t chunk_len = 0;
        if (hg_kv_get_estripe_chunk(stripe_ids[i], &chunk, &chunk_len) != 0)
            continue;
        if (frag_size == 0) {
            frag_size = chunk_len;
        } else if (chunk_len != frag_size) {
            hifs_warning("Fragment size mismatch: expected %zu, got %zu", frag_size, chunk_len);
            free(chunk);
            continue;
        }
        encoded_chunks[i] = chunk;
        ++found;
    }

	    if (found < NUM_DATA) {
	        for (size_t i = NUM_DATA; i < TOTAL && found < NUM_DATA; ++i) {
	            if (stripe_ids[i] == 0 || encoded_chunks[i])
	                continue;
	            uint8_t *chunk = NULL;
	            size_t chunk_len = 0;
            if (hg_kv_get_estripe_chunk(stripe_ids[i], &chunk, &chunk_len) != 0)
                continue;
            if (frag_size == 0) {
                frag_size = chunk_len;
            } else if (chunk_len != frag_size) {
                hifs_warning("Fragment size mismatch: expected %zu, got %zu", frag_size, chunk_len);
                free(chunk);
                continue;
            }
            encoded_chunks[i] = chunk;
            ++found;
        }
    }

    if (frag_size == 0) {
        hifs_warning("No EC fragments found for hash %s", hash_hex);
        goto fail_chunks;
    }

    /* 5) Decode */
    {
        size_t out_len = (size_t)*len;  /* capacity in, size out */
        int rc;

        size_t have_total = 0;
        for (size_t i = 0; i < TOTAL; ++i) if (encoded_chunks[i]) ++have_total;

        if (have_total == TOTAL) {
            rc = hicomm_erasure_coding_decode(encoded_chunks, frag_size,
                                              NUM_DATA, NUM_PARITY,
                                              buf, &out_len);
        } else if (have_total >= NUM_DATA) {
            rc = hicomm_erasure_coding_rebuild_from_partial(encoded_chunks, frag_size,
                                                            NUM_DATA, NUM_PARITY,
                                                            buf, &out_len);
        } else {
            hifs_warning("Not enough fragments for decode: have %zu, need %u",
                         have_total, (unsigned)NUM_DATA);
            goto fail_chunks;
        }

        if (rc == -2) {
            *len = (uint32_t)out_len; /* tell caller required size */
            hifs_warning("Insufficient buffer: need %zu bytes", out_len);
            goto fail_chunks;
        }
        if (rc != 0) {
            hifs_warning("EC decode failed (rc=%d) for %s", rc, hash_hex);
            goto fail_chunks;
        }

        *len = (uint32_t)out_len;
        ok = true;
    }

    /* 6) Cleanup */
fail_chunks:
    for (size_t i = 0; i < TOTAL; ++i) {
        free(encoded_chunks[i]);
        encoded_chunks[i] = NULL;
    }

    return ok;
}

static void __attribute__((unused))
hash_bytes_to_hex(const uint8_t *src, size_t len, char *dst)
{
	bytes_to_hex(src, len, dst);
}

/*
int save_binary_data(char *data_block, char *hash)
{
	(void)data_block;
	(void)hash;
	return 0;
}
*/

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
		/* Fresh DB: seed with this node and persist it */
		mysql_free_result(res);
		sqldb.last_query = NULL;
		struct hive_storage_node local_seed;
		if (!hifs_get_local_node_identity(&local_seed))
			return false;
		if (storage_node_id == 0)
			storage_node_id = 1;
		local_seed.id = storage_node_id;
		int ver = 0, patch = 0;
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
		/* re-use post-load path with synthetic nodes */
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
			strncpy(nodes[i].name, row[COL_NAME], sizeof(nodes[i].name) - 1);
		if (row[COL_ADDRESS])
			strncpy(nodes[i].address, row[COL_ADDRESS], sizeof(nodes[i].address) - 1);
		if (row[COL_UID])
			strncpy(nodes[i].uid, row[COL_UID], sizeof(nodes[i].uid) - 1);
		if (row[COL_SERIAL])
			strncpy(nodes[i].serial, row[COL_SERIAL], sizeof(nodes[i].serial) - 1);
		nodes[i].guard_port = (uint16_t)sql_to_u32(row[COL_GUARD_PORT]);
		nodes[i].stripe_port = (uint16_t)sql_to_u32(row[COL_DATA_PORT]);
		nodes[i].online = row[COL_DOWN] ? (uint8_t)!sql_to_u32(row[COL_DOWN]) : 1;
		nodes[i].fenced = (uint8_t)sql_to_u32(row[COL_FENCED]);
		nodes[i].last_heartbeat = sql_to_u64(row[COL_LAST_HEARTBEAT]);
		nodes[i].hive_version = sql_to_u32(row[COL_VERSION]);
		nodes[i].hive_patch_level = sql_to_u32(row[COL_PATCH_LEVEL]);
		nodes[i].last_maintenance = sql_to_u64(row[COL_LAST_MAINT]);
		nodes[i].maintenance_started_at = sql_to_u64(row[COL_MAINT_STARTED]);
		nodes[i].maintenance_ended_at = sql_to_u64(row[COL_MAINT_ENDED]);
		if (row[COL_MAINT_REASON])
			strncpy(nodes[i].maintenance_reason,
				row[COL_MAINT_REASON],
				sizeof(nodes[i].maintenance_reason) - 1);
		nodes[i].date_added_to_cluster = sql_to_u64(row[COL_DATE_ADDED]);
		nodes[i].storage_capacity_bytes = sql_to_u64(row[COL_SN_CAPCITY]);
		nodes[i].storage_used_bytes = sql_to_u64(row[COL_SN_USED_CAPACITY]);
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

bool hifs_store_block_to_stripe_locations(uint64_t volume_id, uint64_t block_no,
                                          uint8_t hash_algo, const uint8_t hash[HIFS_BLOCK_HASH_SIZE],
                                          const struct stripe_location *locations, size_t count)
{
	char sql_query[MAX_QUERY_SIZE];
	char hash_hex[HIFS_BLOCK_HASH_SIZE * 2 + 1];
	size_t used = 0;

	if (!sqldb.sql_init || !sqldb.conn || !locations || count == 0)
		return false;

	bytes_to_hex(hash, HIFS_BLOCK_HASH_SIZE, hash_hex);

	hifs_debug("store_block_to_stripe_locations: volume=%llu block=%llu hash_algo=%u hash=%s count=%zu",
		   (unsigned long long)volume_id,
		   (unsigned long long)block_no,
		   hash_algo,
		   hash_hex,
		   count);
	for (size_t i = 0; i < count; ++i) {
		const struct stripe_location *loc = &locations[i];
		hifs_debug("store_block_to_stripe_locations: stripe[%zu]=idx=%u node=%u shard=%u estripe=%llu offset=%llu",
			   i,
			   loc->stripe_index,
			   loc->storage_node_id,
			   loc->shard_id,
			   (unsigned long long)loc->estripe_id,
			   (unsigned long long)loc->block_offset);
	}

	used += snprintf(sql_query + used, sizeof(sql_query) - used,
			 "INSERT INTO block_stripe_locations "
			 "(volume_id, block_no, hash_algo, block_hash, stripe_index, "
			 "storage_node_id, shard_id, estripe_id, block_offset, ref_count) VALUES ");

	for (size_t i = 0; i < count; ++i) {
		const struct stripe_location *loc = &locations[i];
		used += snprintf(sql_query + used, sizeof(sql_query) - used,
				 "(%llu,%llu,%u,UNHEX('%s'),%u,%u,%u,%llu,%llu,1)%s",
				 (unsigned long long)volume_id,
				 (unsigned long long)block_no,
				 (unsigned)hash_algo,
				 hash_hex,
				 (unsigned)loc->stripe_index,
				 loc->storage_node_id,
				 loc->shard_id,
				 (unsigned long long)loc->estripe_id,
				 (unsigned long long)loc->block_offset,
				 (i + 1 == count) ? "" : ",");
		if (used >= sizeof(sql_query))
			return false;
	}

	used += snprintf(sql_query + used, sizeof(sql_query) - used,
			 " ON DUPLICATE KEY UPDATE "
			 "storage_node_id=VALUES(storage_node_id), "
			 "shard_id=VALUES(shard_id), "
			 "estripe_id=VALUES(estripe_id), "
			 "block_offset=VALUES(block_offset), "
			 "ref_count=ref_count+1");

	bool ok = hifs_insert_sql(sql_query);
	hifs_debug("store_block_to_stripe_locations: metadata write %s (affected=%llu last_id=%llu)",
		   ok ? "succeeded" : "failed",
		   (unsigned long long)sqldb.last_affected,
		   (unsigned long long)sqldb.last_insert_id);
	return ok;
}

static struct stripe_locations hifs_read_block_to_stripe_locations(uint64_t volume_id, uint64_t block_no,
						      const uint8_t hash[HIFS_BLOCK_HASH_SIZE],
						      size_t count)
{
	struct stripe_locations out = {0};
	char sql_query[MAX_QUERY_SIZE];
	char hash_hex[HIFS_BLOCK_HASH_SIZE * 2 + 1];
	size_t limit = count;

	if (!sqldb.sql_init || !sqldb.conn || !hash || count == 0)
		return out;
	if (limit > ARRAY_SIZE(out.entries))
		limit = ARRAY_SIZE(out.entries);

	bytes_to_hex(hash, HIFS_BLOCK_HASH_SIZE, hash_hex);
	int written = snprintf(sql_query, sizeof(sql_query),
			       "SELECT stripe_index, storage_node_id, shard_id, estripe_id, block_offset "
			       "FROM block_stripe_locations "
			       "WHERE volume_id=%llu AND block_no=%llu AND block_hash=UNHEX('%s') "
			       "ORDER BY stripe_index LIMIT %zu",
			       (unsigned long long)volume_id,
			       (unsigned long long)block_no,
			       hash_hex,
			       limit);
	if (written < 0 || (size_t)written >= sizeof(sql_query))
		return out;

	MYSQL_RES *res = hifs_execute_sql(sql_query);
	if (!res)
		return out;

	MYSQL_ROW row;
	size_t idx = 0;
	while (idx < limit && (row = mysql_fetch_row(res)) != NULL) {
		struct stripe_location *loc = &out.entries[idx];
		loc->stripe_index = (uint8_t)sql_to_u32(row[0]);
		loc->storage_node_id = sql_to_u32(row[1]);
		loc->shard_id = sql_to_u32(row[2]);
		loc->estripe_id = sql_to_u64(row[3]);
		loc->block_offset = sql_to_u64(row[4]);
		++idx;
	}

	mysql_free_result(res);
	sqldb.last_query = NULL;
	out.count = idx;
	out.ok = (idx > 0);
	if (idx == 0) {
		hifs_warning("stripe location rows missing for volume=%llu block=%llu hash=%s",
			     (unsigned long long)volume_id,
			     (unsigned long long)block_no,
			     hash_hex);
	} else if (idx < limit) {
		hifs_notice("stripe location gap for volume=%llu block=%llu hash=%s: have %zu/%zu rows",
			    (unsigned long long)volume_id,
			    (unsigned long long)block_no,
			    hash_hex,
			    idx,
			    limit);
	}
	return out;
}
