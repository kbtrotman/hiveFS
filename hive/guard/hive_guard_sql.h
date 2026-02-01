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
#include "../common/hive_common_sql.h"

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

#define DB_HOST     NULL
#define DB_NAME     "hive_meta"
#define DB_USER     "hiveadmin"
#define DB_PASS     "hiveadmin"
#define DB_PORT     0
#define DB_SOCKET   "/run/mysqld/mysqld.sock"
#define DB_FLAGS    0

#define NO_RECORDS 0x099

#define MAX_QUERY_SIZE 4096
#define MAX_INSERT_SIZE 512

/* SQL Connect */

/* SQL format strings are generally declared in hive/common/hive_common_sql.h */

#define SQL_HOST_TOKEN_SELECT_NODE_JOIN_LATEST \
	"SELECT token FROM host_tokens " \
	"WHERE machine_uid='%s' AND t_type='node_join' " \
	"AND used=0 AND revoked=0 AND expired=0 " \
	"ORDER BY issued_at DESC LIMIT 1"

#define SQL_HOST_AUTH_SELECT_APPROVED_PUBKEY \
	"SELECT pub_key_pem FROM host_auth " \
	"WHERE machine_uid='%s' AND status='approved' " \
	"ORDER BY issued_on DESC LIMIT 1"

#define SQL_STORAGE_NODE_STATS_TRIM_TO_5_MINUTES \
	"DELETE FROM storage_node_stats " \
	"WHERE node_id=%llu AND s_ts < NOW() - INTERVAL 20 MINUTE " \
	"AND (UNIX_TIMESTAMP(s_ts) %% 300) != 0"

#define SQL_STORAGE_NODE_STATS_TRIM_TO_20_MINUTES \
	"DELETE FROM storage_node_stats " \
	"WHERE node_id=%llu AND s_ts < NOW() - INTERVAL 2 WEEK " \
	"AND (UNIX_TIMESTAMP(s_ts) %% 1200) != 0"

#define SQL_RAFT_SNAPSHOT_TABLE_DDL \
	"CREATE TABLE IF NOT EXISTS meta_snapshots (" \
	"snapshot_id BIGINT UNSIGNED NOT NULL PRIMARY KEY," \
	"snapshot_name VARCHAR(128) DEFAULT NULL," \
	"snapshot_description TEXT DEFAULT NULL," \
	"created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6)," \
	"updated_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) " \
	"ON UPDATE CURRENT_TIMESTAMP(6)," \
	"created_by VARCHAR(128) NOT NULL DEFAULT 'system'," \
	"container VARCHAR(64) NOT NULL DEFAULT 'meta'," \
	"container_ref VARCHAR(128) DEFAULT NULL," \
	"raft_index_at_snap BIGINT UNSIGNED NOT NULL," \
	"cluster_version VARCHAR(32) DEFAULT NULL," \
	"cluster_wide BOOLEAN NOT NULL DEFAULT 1," \
	"scope_node_id INT UNSIGNED DEFAULT NULL," \
	"owner VARCHAR(128) DEFAULT NULL," \
	"owner_group VARCHAR(128) DEFAULT NULL," \
	"permissions VARCHAR(16) DEFAULT NULL," \
	"retention_hours INT UNSIGNED DEFAULT NULL," \
	"retention_days INT UNSIGNED DEFAULT NULL," \
	"logical_size_bytes BIGINT UNSIGNED NOT NULL DEFAULT 0," \
	"physical_size_bytes BIGINT UNSIGNED NOT NULL DEFAULT 0," \
	"is_mutable BOOLEAN NOT NULL DEFAULT 0," \
	"auto_delete BOOLEAN NOT NULL DEFAULT 0," \
	"auto_delete_at TIMESTAMP(6) NULL DEFAULT NULL," \
	"tags JSON DEFAULT NULL," \
	"KEY idx_meta_snapshots_created (created_at)," \
	"KEY idx_meta_snapshots_container (container, container_ref)," \
	"KEY idx_meta_snapshots_scope (cluster_wide, scope_node_id)" \
	") ENGINE=InnoDB"
#define SQL_RAFT_SNAPSHOT_ISOLATION_REPEATABLE_READ \
	"SET SESSION TRANSACTION ISOLATION LEVEL REPEATABLE READ"

#define SQL_RAFT_SNAPSHOT_START_CONSISTENT \
	"START TRANSACTION WITH CONSISTENT SNAPSHOT"

#define SQL_RAFT_SNAPSHOT_INSERT_OR_UPDATE \
	"INSERT INTO meta_snapshots (snapshot_id, raft_index_at_snap, created_at, updated_at) " \
	"VALUES (%llu, %llu, NOW(6), NOW(6)) " \
	"ON DUPLICATE KEY UPDATE raft_index_at_snap=VALUES(raft_index_at_snap), " \
	"updated_at=VALUES(updated_at)"


/* Prototypes */
void init_hive_link(void);
void close_hive_link(void);
int get_hive_vers(void);
MYSQL_RES *hifs_get_hive_host_data(char *machine_id);
bool hifs_metadata_async_execute(const char *sql_string);
void hifs_metadata_async_shutdown(void);

/*
 * Filesystem stats (storage_node_fs_stats)
 * Insert one row per filesystem per sampling interval.
 */
bool hifs_store_fs_stat(uint64_t node_id,
			 uint64_t ts_unix,
			 const char *fs_name,
			 const char *fs_path,
			 const char *fs_type,
			 uint64_t fs_total_bytes,
			 uint64_t fs_used_bytes,
			 uint64_t fs_avail_bytes,
			 double fs_used_pct,
			 uint64_t in_total,
			 uint64_t in_used,
			 uint64_t in_avail,
			 double in_used_pct,
			 const char *health);

/*
 * Physical disk stats (storage_node_disk_stats)
 * Insert one row per block device per sampling interval.
 */
bool hifs_store_disk_stat(uint64_t node_id,
			  uint64_t ts_unix,
			  const char *disk_name,
			  const char *disk_path,
			  uint64_t disk_size_bytes,
			  unsigned int disk_rotational,
			  uint64_t reads_completed,
			  uint64_t writes_completed,
			  uint64_t read_bytes,
			  uint64_t write_bytes,
			  uint64_t io_in_progress,
			  uint64_t io_ms,
			  const char *fs_path,
			  const char *health);

MYSQL *hg_sql_get_db(void);
int get_host_info(void);
char *hifs_get_quoted_value(const char *in_str);
char *hifs_get_unquoted_value(const char *in_str);
void hifs_release_query(void);
bool hifs_insert_data(const char *q_string);
int hifs_get_hive_host_sbs(void);
int save_binary_data(char *data_block, char *hash);

/* Remote metadata/SQL helpers */
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
int hg_sql_snapshot_take(MYSQL *db, uint64_t snap_id, uint64_t snap_index);

int hg_sql_snapshot_create_artifact(const char *snapshot_dir,
                                   const char *mysqldump_path,
                                   const char *mysql_defaults_file,
                                   const char *db_name,
                                   uint64_t snap_id,
                                   uint64_t snap_index,
                                   char *out_path,
                                   size_t out_path_sz);

int hg_sql_snapshot_restore_artifact(const char *snapshot_dir,
                                   const char *mysqldump_path,
                                   const char *mysql_defaults_file,
                                   const char *db_name,
                                   uint64_t snap_id,
                                   uint64_t snap_index,
                                   char *out_path,
                                   size_t out_path_sz);
