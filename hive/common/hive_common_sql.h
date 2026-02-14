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

#pragma once

#include <mysql.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../../hicomm/hi_command.h"

struct hive_storage_node {
	uint32_t id;
	char name[100];
	char address[64];
	char uid[128];
	char serial[100];
	uint16_t guard_port;
	uint16_t stripe_port;
	uint64_t last_heartbeat;
	uint8_t online;
	uint8_t fenced;
	uint32_t hive_version;
	uint32_t hive_patch_level;
	uint64_t last_maintenance;
	char maintenance_reason[256];
	uint64_t maintenance_started_at;
	uint64_t maintenance_ended_at;
	uint64_t date_added_to_cluster;
	uint64_t storage_capacity_bytes;
	uint64_t storage_used_bytes;
	uint64_t storage_reserved_bytes;
	uint64_t storage_overhead_bytes;
	uint32_t client_connect_timeout_ms;
	uint32_t storage_node_connect_timeout_ms;
};

struct SQLDB {
	MYSQL *conn;
	MYSQL_RES *last_query;
	unsigned long long last_affected;
	unsigned long long last_insert_id;
	int rec_count;
	int rows;
	int cols;
	bool sql_init;
	struct machine host;
	struct superblock sb[50];
};

struct hive_bootstrap_config;
struct hive_bootstrap_config;
extern struct SQLDB sqldb;

#ifndef HIFS_STORAGE_NODE_HEARTBEAT_MAX_AGE
#define HIFS_STORAGE_NODE_HEARTBEAT_MAX_AGE 30
#endif

#ifndef HIFS_STORAGE_NODE_PERSIST_MIN_SECS
#define HIFS_STORAGE_NODE_PERSIST_MIN_SECS 5
#endif

#ifndef HIFS_STORAGE_NODE_DATA_SYNC_MIN_SECS
#define HIFS_STORAGE_NODE_DATA_SYNC_MIN_SECS (3 * 60)
#endif

#ifndef HIFS_CLUSTER_NAME_MAX
#define HIFS_CLUSTER_NAME_MAX 64
#endif

#ifndef HIFS_CLUSTER_DESC_MAX
#define HIFS_CLUSTER_DESC_MAX 256
#endif


/***********************
 * SQL Format Strings
 ***********************/

#ifndef SQL_CLUSTER_INSERT
#define SQL_CLUSTER_INSERT \
	"INSERT INTO hive_api.cluster " \
	"(cluster_id, cluster_name, cluster_description, is_ok, off_line, " \
	"cluster_state, cluster_health, min_node_req, cluster_node_count) " \
	"VALUES (%llu, '%s', '%s', %u, %u, '%s', '%s', %u, %u)"
#endif

#ifndef SQL_CLUSTER_UPSERT
#define SQL_CLUSTER_UPSERT \
	SQL_CLUSTER_INSERT " ON DUPLICATE KEY UPDATE " \
	"cluster_name=VALUES(cluster_name), " \
	"cluster_description=VALUES(cluster_description), " \
	"is_ok=VALUES(is_ok), " \
	"off_line=VALUES(off_line), " \
	"cluster_state=VALUES(cluster_state), " \
	"cluster_health=VALUES(cluster_health), " \
	"min_node_req=VALUES(min_node_req), " \
	"cluster_node_count=VALUES(cluster_node_count)"
#endif

#define SQL_HOST_SELECT_BY_SERIAL \
	"SELECT serial, name, host_id, os_name, os_version, create_time, " \
	"security_level, root_connect_as, def_user_connect_as, ldap_member " \
	"FROM host WHERE serial='%s'"

#define SQL_HOST_UPSERT \
	"INSERT INTO host (serial, name, host_id, host_address, os_name, os_version, create_time, hicom_port, epoch, fenced) " \
	"VALUES ('%s', '%s', %ld, '%s', '%s', '%s', NOW(), %u, %llu, %u) " \
	"ON DUPLICATE KEY UPDATE name=VALUES(name), host_id=VALUES(host_id), " \
	"host_address=VALUES(host_address), os_name=VALUES(os_name), os_version=VALUES(os_version), " \
	"hicom_port=VALUES(hicom_port), epoch=VALUES(epoch), fenced=VALUES(fenced)"

#define SQL_VOLUME_SUPER_LIST \
	"SELECT volume_id, s_magic, s_blocksize, s_blocksize_bits, " \
	"s_blocks_count, s_free_blocks, s_inodes_count, s_free_inodes " \
	"FROM volume_superblocks ORDER BY volume_id"

#define SQL_VOLUME_SUPER_SELECT \
	"SELECT s_magic, s_blocksize, s_blocksize_bits, s_blocks_count, " \
	"s_free_blocks, s_inodes_count, s_free_inodes, s_maxbytes, " \
	"s_feature_compat, s_feature_ro_compat, s_feature_incompat, " \
	"HEX(s_uuid), s_rev_level, s_wtime, s_flags, HEX(s_volume_name) " \
	"FROM volume_superblocks WHERE volume_id=%llu"

#define SQL_VOLUME_SUPER_UPSERT \
	"INSERT INTO volume_superblocks " \
	"(volume_id, s_magic, s_blocksize, s_blocksize_bits, s_blocks_count, " \
	"s_free_blocks, s_inodes_count, s_free_inodes, s_maxbytes, " \
	"s_feature_compat, s_feature_ro_compat, s_feature_incompat, s_uuid, " \
	"s_rev_level, s_wtime, s_flags, s_volume_name) " \
	"VALUES (%llu, %u, %u, %u, %llu, %llu, %llu, %llu, %llu, %u, %u, %u, " \
	"UNHEX('%s'), %u, %u, %u, UNHEX('%s')) " \
	"ON DUPLICATE KEY UPDATE " \
	"s_magic=VALUES(s_magic), s_blocksize=VALUES(s_blocksize), " \
	"s_blocksize_bits=VALUES(s_blocksize_bits), s_blocks_count=VALUES(s_blocks_count), " \
	"s_free_blocks=VALUES(s_free_blocks), s_inodes_count=VALUES(s_inodes_count), " \
	"s_free_inodes=VALUES(s_free_inodes), s_maxbytes=VALUES(s_maxbytes), " \
	"s_feature_compat=VALUES(s_feature_compat), " \
	"s_feature_ro_compat=VALUES(s_feature_ro_compat), " \
	"s_feature_incompat=VALUES(s_feature_incompat), s_uuid=VALUES(s_uuid), " \
	"s_rev_level=VALUES(s_rev_level), s_wtime=VALUES(s_wtime), " \
	"s_flags=VALUES(s_flags), s_volume_name=VALUES(s_volume_name)"

#define SQL_ROOT_DENTRY_SELECT \
	"SELECT rd_inode, rd_mode, rd_uid, rd_gid, rd_flags, rd_size, " \
	"rd_blocks, rd_atime, rd_mtime, rd_ctime, rd_links, rd_name_len, " \
	"HEX(rd_name) FROM volume_root_dentries WHERE volume_id=%llu"

#define SQL_ROOT_DENTRY_UPSERT \
	"INSERT INTO volume_root_dentries " \
	"(volume_id, rd_inode, rd_mode, rd_uid, rd_gid, rd_flags, rd_size, " \
	"rd_blocks, rd_atime, rd_mtime, rd_ctime, rd_links, rd_name_len, rd_name) " \
	"VALUES (%llu, %llu, %u, %u, %u, %u, %llu, %llu, %u, %u, %u, %u, %u, UNHEX('%s')) " \
	"ON DUPLICATE KEY UPDATE " \
	"rd_inode=VALUES(rd_inode), rd_mode=VALUES(rd_mode), rd_uid=VALUES(rd_uid), " \
	"rd_gid=VALUES(rd_gid), rd_flags=VALUES(rd_flags), rd_size=VALUES(rd_size), " \
	"rd_blocks=VALUES(rd_blocks), rd_atime=VALUES(rd_atime), rd_mtime=VALUES(rd_mtime), " \
	"rd_ctime=VALUES(rd_ctime), rd_links=VALUES(rd_links), " \
	"rd_name_len=VALUES(rd_name_len), rd_name=VALUES(rd_name)"

#define SQL_DENTRY_BY_INODE \
	"SELECT de_parent, de_inode, de_epoch, de_type, de_name_len, HEX(de_name) " \
	"FROM volume_dentries WHERE volume_id=%llu AND de_inode=%llu LIMIT 1"

#define SQL_DENTRY_BY_NAME \
	"SELECT de_parent, de_inode, de_epoch, de_type, de_name_len, HEX(de_name) " \
	"FROM volume_dentries WHERE volume_id=%llu AND de_parent=%llu " \
	"AND de_name=UNHEX('%.*s') LIMIT 1"

#define SQL_DENTRY_DELETE_BY_NAME \
	"DELETE FROM volume_dentries WHERE volume_id=%llu AND de_parent=%llu " \
	"AND de_name=UNHEX('%.*s')"

#define SQL_DENTRY_UPSERT \
	"INSERT INTO volume_dentries " \
	"(volume_id, de_parent, de_inode, de_epoch, de_type, de_name_len, de_name) " \
	"VALUES (%llu, %llu, %llu, %u, %u, %u, UNHEX('%s')) " \
	"ON DUPLICATE KEY UPDATE " \
	"de_inode=VALUES(de_inode), de_epoch=VALUES(de_epoch), " \
	"de_type=VALUES(de_type), de_name_len=VALUES(de_name_len), " \
	"de_name=VALUES(de_name)"

#define SQL_VOLUME_INODE_SELECT \
	"SELECT i_msg_flags, i_version, i_flags, i_mode, i_ino, i_uid, i_gid, " \
	"i_hrd_lnk, i_atime, i_mtime, i_ctime, i_size, HEX(i_name), " \
	"extent0_start, extent1_start, extent2_start, extent3_start, " \
	"extent0_count, extent1_count, extent2_count, extent3_count, " \
	"i_blocks, i_bytes, i_links, i_hash_count, i_hash_reserved, epoch " \
	"FROM volume_inodes WHERE volume_id=%llu AND inode=%llu"

#define SQL_VOLUME_INODE_UPSERT \
	"INSERT INTO volume_inodes " \
	"(volume_id, inode, i_msg_flags, i_version, i_flags, i_mode, i_ino, i_uid, i_gid, " \
	"i_hrd_lnk, i_atime, i_mtime, i_ctime, i_size, i_name, " \
	"extent0_start, extent1_start, extent2_start, extent3_start, " \
	"extent0_count, extent1_count, extent2_count, extent3_count, " \
	"i_blocks, i_bytes, i_links, i_hash_count, i_hash_reserved, epoch) " \
	"VALUES (%llu, %llu, %u, %u, %u, %u, %llu, %u, %u, %u, %u, %u, %u, %u, UNHEX('%s'), " \
	"%u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u) " \
	"ON DUPLICATE KEY UPDATE " \
	"i_msg_flags=VALUES(i_msg_flags), i_version=VALUES(i_version), " \
	"i_flags=VALUES(i_flags), i_mode=VALUES(i_mode), i_ino=VALUES(i_ino), " \
	"i_uid=VALUES(i_uid), i_gid=VALUES(i_gid), i_hrd_lnk=VALUES(i_hrd_lnk), " \
	"i_atime=VALUES(i_atime), i_mtime=VALUES(i_mtime), i_ctime=VALUES(i_ctime), " \
	"i_size=VALUES(i_size), i_name=VALUES(i_name), " \
	"extent0_start=VALUES(extent0_start), extent1_start=VALUES(extent1_start), " \
	"extent2_start=VALUES(extent2_start), extent3_start=VALUES(extent3_start), " \
	"extent0_count=VALUES(extent0_count), extent1_count=VALUES(extent1_count), " \
	"extent2_count=VALUES(extent2_count), extent3_count=VALUES(extent3_count), " \
	"i_blocks=VALUES(i_blocks), i_bytes=VALUES(i_bytes), " \
	"i_links=VALUES(i_links), i_hash_count=VALUES(i_hash_count), " \
	"i_hash_reserved=VALUES(i_hash_reserved), epoch=VALUES(epoch)"

#define SQL_STORAGE_NODES_SELECT \
	"SELECT node_id, node_name, node_address, node_uid, node_serial, " \
	"node_guard_port, node_data_port, UNIX_TIMESTAMP(last_heartbeat) AS last_heartbeat, " \
	"hive_version, hive_patch_level, fenced, down, " \
	"UNIX_TIMESTAMP(last_maintenance) AS last_maintenance, maintenance_reason, " \
	"UNIX_TIMESTAMP(maintenance_started_at) AS maintenance_started_at, " \
	"UNIX_TIMESTAMP(maintenance_ended_at) AS maintenance_ended_at, " \
	"date_added_to_cluster, storage_capacity_bytes, storage_used_bytes, " \
	"storage_reserved_bytes, storage_overhead_bytes, client_connect_timout, " \
	"sn_connect_timeout " \
	"FROM storage_nodes ORDER BY node_id"

#define SQL_STORAGE_NODE_UPSERT \
	"INSERT INTO storage_nodes " \
	"(node_id, node_name, node_address, node_uid, node_serial, " \
	"node_guard_port, node_data_port, last_heartbeat, hive_version, hive_patch_level, fenced, " \
	"storage_capacity_bytes, storage_used_bytes, storage_reserved_bytes, storage_overhead_bytes) " \
	"VALUES (%llu, '%s', '%s', '%s', '%s', %u, %u, NOW(), %d, %d, 0, %llu, %llu, %llu, %llu) " \
	"ON DUPLICATE KEY UPDATE " \
	"node_name=VALUES(node_name), node_address=VALUES(node_address), " \
	"node_uid=VALUES(node_uid), node_serial=VALUES(node_serial), " \
	"node_guard_port=VALUES(node_guard_port), node_data_port=VALUES(node_data_port), " \
	"last_heartbeat=VALUES(last_heartbeat), " \
	"hive_version=VALUES(hive_version), " \
	"hive_patch_level=VALUES(hive_patch_level), " \
	"fenced=VALUES(fenced), " \
	"storage_capacity_bytes=VALUES(storage_capacity_bytes), " \
	"storage_used_bytes=VALUES(storage_used_bytes), " \
	"storage_reserved_bytes=VALUES(storage_reserved_bytes), " \
	"storage_overhead_bytes=VALUES(storage_overhead_bytes)"

#define SQL_STORAGE_NODE_ID_BY_UID_OR_SERIAL \
	"SELECT node_id FROM storage_nodes " \
	"WHERE node_uid='%s' OR node_serial='%s' " \
	"ORDER BY node_id ASC LIMIT 1"

#define SQL_STORAGE_NODE_MAX_ID_SELECT \
	"SELECT node_id FROM storage_nodes ORDER BY node_id DESC LIMIT 1"

#define SQL_STORAGE_NODE_JOIN_UPDATE \
	"UPDATE storage_nodes SET " \
	"cluster_id=%s, " \
	"node_name='%s', " \
	"node_address='%s', " \
	"node_uid='%s', " \
	"node_serial='%s', " \
	"database_state='configured', " \
	"kv_state='configured', " \
	"cont1_state='configured', " \
	"cont2_state='configured', " \
	"hg_state='configured', " \
	"node_join_ts=NOW(), " \
	"fenced=1, " \
	"down=0, " \
	"node_health='ok', " \
	"node_guard_port=%u, " \
	"node_data_port=%u, " \
	"last_heartbeat=NOW(), " \
	"hive_version=%d, " \
	"hive_patch_level=%d, " \
	"client_connect_timout=60000, " \
	"sn_connect_timeout=30000 " \
	"WHERE node_id=%llu"

#define SQL_HOST_TOKEN_INSERT_NODE_JOIN \
	"INSERT INTO host_tokens " \
	"(token, t_type, machine_uid, issued_at, expires_at, approved_at, approved_by) " \
	"VALUES ('%s', 'node_join', '%s', NOW(), DATE_ADD(NOW(), INTERVAL 1 DAY), NOW(), 'bootstrap_token')"

#define SQL_HOST_AUTH_UPSERT_CERT \
	"INSERT INTO host_auth " \
	"(serial, machine_uid, name, priv_key_pem, pub_key_pem, expires_at, status) " \
	"VALUES ('%s', '%s', '%s', '%s', '%s', DATE_ADD(NOW(), INTERVAL %u DAY), 'approved') " \
	"ON DUPLICATE KEY UPDATE " \
	"serial=VALUES(serial), name=VALUES(name), priv_key_pem=VALUES(priv_key_pem), " \
	"pub_key_pem=VALUES(pub_key_pem), expires_at=VALUES(expires_at), " \
	"revoked_at=NULL, revoked_by=NULL, revocation_reason=NULL, status='approved'"

#define SQL_STORAGE_NODE_STATS_INSERT \
	"INSERT INTO storage_node_stats " \
"(node_id, s_ts, cpu, mem_used, mem_avail, read_iops, write_iops, total_iops, " \
"meta_chan_ps, incoming_mbps, cl_outgoing_mbps, sn_node_in_mbps, sn_node_out_mbps, " \
"writes_mbps, reads_mbps, t_throughput, kv_putblock_calls, kv_putblock_ps, kv_putblock_bytes, " \
"kv_putblock_dedup_hits, kv_putblock_dedup_misses, kv_rocksdb_writes, kv_rocksdb_write_ns, " \
"contig_write_calls, contig_write_bytes, tcp_rx_bytes, tcp_tx_bytes, c_net_in, c_net_out, " \
"s_net_in, s_net_out, avg_wr_latency, avg_rd_latency, lavg, sees_warning, sees_error, " \
"message, cont1_isok, cont2_isok, cont1_message, cont2_message, clients) " \
"VALUES (%llu, FROM_UNIXTIME(%llu), %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, " \
"%llu, %u, %llu, %llu, %llu, %llu, %llu, %llu, %llu, %llu, %u, %u, %u, %u, %u, %u, '%s', %u, %u, '%s', '%s', %u)"

#define SQL_STORAGE_NODE_FS_STATS_INSERT \
	"INSERT INTO storage_node_fs_stats " \
	"(node_id, fs_ts, fs_name, fs_path, fs_type, " \
	"fs_total_bytes, fs_used_bytes, fs_avail_bytes, fs_used_pct, " \
	"in_total_bytes, in_used_bytes, in_avail_bytes, in_used_pct, health) " \
	"VALUES (%llu, FROM_UNIXTIME(%llu), %s, %s, %s, %llu, %llu, %llu, %.2f, %llu, %llu, %llu, %.2f, %s)"

#define SQL_STORAGE_NODE_DISK_STATS_INSERT \
	"INSERT INTO storage_node_disk_stats " \
	"(node_id, disk_ts, disk_name, disk_path, disk_size_bytes, disk_rotational, " \
	"reads_completed, writes_completed, read_bytes, write_bytes, read_ms, write_ms, " \
	"io_in_progress, io_ms, weighted_io_ms, fs_path, health) " \
	"VALUES (%llu, FROM_UNIXTIME(%llu), %s, %s, %llu, %u, %llu, %llu, %llu, %llu, %llu, %llu, %llu, %llu, %llu, %s, %s)"



/* Prototypes */

void hifs_safe_strcpy(char *dst, size_t dst_len, const char *src);
char *hifs_get_machine_id(void);
const char *hifs_cached_host_serial(void);
long hifs_get_host_id(void);
uint16_t hifs_local_guard_port(void);
void hifs_parse_version(int *version_out, int *patch_out);
bool hifs_get_local_node_identity(struct hive_storage_node *node);
bool hifs_execute_query(const char *sql, MYSQL_RES **out_res);
bool hifs_insert_sql(const char *sql_string);
bool hifs_persist_storage_node(uint64_t node_id,
			       const char *node_address,
			       uint16_t node_port);
bool hifs_persist_cluster_record(uint64_t cluster_id,
				 const char *cluster_name,
				 const char *cluster_desc,
				 uint16_t min_nodes);
int hifs_update_node_for_add(struct hive_bootstrap_config *config,
			     struct hive_storage_node *local,
			     bool is_node_join_request,
			     uint64_t requested_node_id);

struct stripe_location {
	uint8_t stripe_index;
	uint32_t storage_node_id;
	uint32_t shard_id;
	uint64_t estripe_id;
	uint64_t block_offset;
};
bool hifs_get_stripe_locations(uint64_t inode,
				struct stripe_location **locations_out,
				size_t *num_locations_out);
void hifs_free_stripe_locations(struct stripe_location *locations, size_t num_locations);
bool hifs_init_sql_connection(void);
void hifs_close_sql_connection(void);
