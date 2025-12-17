/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */
#pragma once

#include "hive_guard.h"

/***********************
 * Database  Definitions
 ***********************/

#if defined(__has_include)
#  if __has_include(<mysql/mysql.h>)
#    include <mysql/mysql.h>
#  elif __has_include(<mariadb/mysql.h>)
#    include <mariadb/mysql.h>
#  else
#    error "MariaDB client headers not found"
#  endif
#else
#  include <mysql/mysql.h>
#endif

#include <stdbool.h>
#include <stdint.h>

#include "../../hicomm/hi_command.h"

#define DB_HOST     "127.0.0.1"
#define DB_NAME     "hive_meta"
#define DB_USER     "hiveadmin"
#define DB_PASS     "hiveadmin"
#define DB_PORT     3306
#define DB_SOCKET   NULL
#define DB_FLAGS    0

#define NO_RECORDS 0x099

#define MAX_QUERY_SIZE 4096
#define MAX_INSERT_SIZE 512

/***********************
 * SQL Format Strings
 ***********************/
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

struct stripe_location {
	uint8_t stripe_index;
	uint32_t storage_node_id;
	uint32_t shard_id;
	uint64_t estripe_id;
	uint64_t block_offset;
};

/* SELECT all storage nodes */
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

/* UPSERT storage node info */
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


#define SQL_STORAGE_NODE_STATS_INSERT \
	"INSERT INTO storage_node_stats " \
	"(node_id, s_ts, cpu, mem_used, mem_avail, read_iops, write_iops, total_iops, " \
	"meta_chan_ps, incoming_mbps, cl_outgoing_mbps, sn_node_in_mbps, sn_node_out_mbps, " \
	"writes_mbps, reads_mbps, t_throughput, c_net_in, c_net_out, s_net_in, s_net_out, " \
	"avg_wr_latency, avg_rd_latency, lavg, sees_warning, sees_error, message, " \
	"cont1_isok, cont2_isok, cont1_message, cont2_message, clients) " \
	"VALUES (%llu, FROM_UNIXTIME(%llu), %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, '%s', %u, %u, '%s', %u, %u, '%s', '%s', %u)"

#define SQL_STORAGE_NODE_STATS_TRIM_TO_5_MINUTES \
	"DELETE FROM storage_node_stats " \
	"WHERE node_id=%llu AND s_ts < NOW() - INTERVAL 20 MINUTE " \
	"AND (UNIX_TIMESTAMP(s_ts) %% 300) != 0"

#define SQL_STORAGE_NODE_STATS_TRIM_TO_20_MINUTES \
	"DELETE FROM storage_node_stats " \
	"WHERE node_id=%llu AND s_ts < NOW() - INTERVAL 2 WEEK " \
	"AND (UNIX_TIMESTAMP(s_ts) %% 1200) != 0"


/* Prototypes */
void init_hive_link(void);
void close_hive_link(void);
int get_hive_vers(void);
MYSQL_RES *hifs_get_hive_host_data(char *machine_id);
int get_host_info(void);
char *hifs_get_quoted_value(const char *in_str);
char *hifs_get_unquoted_value(const char *in_str);
void hifs_release_query(void);
bool hifs_insert_data(const char *q_string);
int hifs_get_hive_host_sbs(void);
int save_binary_data(char *data_block, char *hash);

/* Remote superblock helpers */
bool hifs_volume_super_get(uint64_t volume_id, struct hifs_volume_superblock *out);
bool hifs_volume_super_set(uint64_t volume_id, const struct hifs_volume_superblock *vsb);
bool hifs_root_dentry_load(uint64_t volume_id, struct hifs_volume_root_dentry *out);
bool hifs_root_dentry_store(uint64_t volume_id, const struct hifs_volume_root_dentry *root);
bool hifs_volume_dentry_load_by_inode(uint64_t volume_id, uint64_t inode,
                                      struct hifs_volume_dentry *out);
bool hifs_volume_dentry_load_by_name(uint64_t volume_id, uint64_t parent,
                                     const char *name_hex, uint32_t name_hex_len,
                                     struct hifs_volume_dentry *out);
bool hifs_volume_dentry_store(uint64_t volume_id,
                              const struct hifs_volume_dentry *dent);
bool hifs_volume_inode_load(uint64_t volume_id, uint64_t inode,
                            struct hifs_inode_wire *out);
bool hifs_volume_inode_store(uint64_t volume_id,
                             const struct hifs_inode_wire *inode);
bool hifs_volume_block_load(uint64_t volume_id, uint64_t block_no,
                            uint8_t *buf, uint32_t *len);
bool hifs_volume_block_store(uint64_t volume_id, uint64_t block_no,
                             const uint8_t *buf, uint32_t len,
			     enum hifs_hash_algorithm algo,
			     const uint8_t hash[HIFS_BLOCK_HASH_SIZE]);
bool hifs_volume_block_store_encoded(uint64_t volume_id, uint64_t block_no,
                                     const struct hifs_ec_stripe_set *ec);
bool hifs_volume_block_ec_encode(const uint8_t *buf, uint32_t len,
			 enum hifs_hash_algorithm algo,
			 const uint8_t *pre_hash,
			 struct hifs_ec_stripe_set *out);
void hifs_volume_block_ec_free(struct hifs_ec_stripe_set *ec);
bool hifs_load_storage_nodes(void);
const struct hive_storage_node *hifs_get_storage_nodes(size_t *count);
const struct hive_storage_node *hifs_get_local_storage_node(void);
bool hifs_store_block_to_stripe_locations(uint64_t volume_id,
                                          uint64_t block_no,
                                          uint8_t hash_algo,
                                          const uint8_t hash[HIFS_BLOCK_HASH_SIZE],
                                          const struct stripe_location *locations,
                                          size_t count);

/* SQL Connect */


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
extern struct SQLDB sqldb;
