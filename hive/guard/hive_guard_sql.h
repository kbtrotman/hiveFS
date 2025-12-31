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

/* SQL format strings are declared in hive/common/hive_common_sql.h */

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
