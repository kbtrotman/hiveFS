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
	"INSERT INTO host (serial, name, host_id, os_name, os_version, create_time) " \
	"VALUES ('%s', '%s', %ld, '%s', '%s', NOW()) " \
	"ON DUPLICATE KEY UPDATE name=VALUES(name), host_id=VALUES(host_id), " \
	"os_name=VALUES(os_name), os_version=VALUES(os_version)"

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
	"i_addrb0, i_addrb1, i_addrb2, i_addrb3, " \
	"i_addre0, i_addre1, i_addre2, i_addre3, " \
	"i_blocks, i_bytes, i_links, i_hash_count, i_hash_reserved " \
	"FROM volume_inodes WHERE volume_id=%llu AND inode=%llu"

#define SQL_VOLUME_INODE_UPSERT \
	"INSERT INTO volume_inodes " \
	"(volume_id, inode, i_msg_flags, i_version, i_flags, i_mode, i_ino, i_uid, i_gid, " \
	"i_hrd_lnk, i_atime, i_mtime, i_ctime, i_size, i_name, " \
	"i_addrb0, i_addrb1, i_addrb2, i_addrb3, " \
	"i_addre0, i_addre1, i_addre2, i_addre3, " \
	"i_blocks, i_bytes, i_links, i_hash_count, i_hash_reserved, epoch) " \
	"VALUES (%llu, %llu, %u, %u, %u, %u, %llu, %u, %u, %u, %u, %u, %u, %u, UNHEX('%s'), " \
	"%u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u) " \
	"ON DUPLICATE KEY UPDATE " \
	"i_msg_flags=VALUES(i_msg_flags), i_version=VALUES(i_version), " \
	"i_flags=VALUES(i_flags), i_mode=VALUES(i_mode), i_ino=VALUES(i_ino), " \
	"i_uid=VALUES(i_uid), i_gid=VALUES(i_gid), i_hrd_lnk=VALUES(i_hrd_lnk), " \
	"i_atime=VALUES(i_atime), i_mtime=VALUES(i_mtime), i_ctime=VALUES(i_ctime), " \
	"i_size=VALUES(i_size), i_name=VALUES(i_name), " \
	"i_addrb0=VALUES(i_addrb0), i_addrb1=VALUES(i_addrb1), " \
	"i_addrb2=VALUES(i_addrb2), i_addrb3=VALUES(i_addrb3), " \
	"i_addre0=VALUES(i_addre0), i_addre1=VALUES(i_addre1), " \
	"i_addre2=VALUES(i_addre2), i_addre3=VALUES(i_addre3), " \
	"i_blocks=VALUES(i_blocks), i_bytes=VALUES(i_bytes), " \
	"i_links=VALUES(i_links), i_hash_count=VALUES(i_hash_count), " \
	"i_hash_reserved=VALUES(i_hash_reserved), epoch=VALUES(epoch)"

#define SQL_VOLUME_INODE_FP_SELECT \
	"SELECT fp_index, block_no, hash_algo, HEX(block_hash) " \
	"FROM volume_inode_fingerprints WHERE volume_id=%llu AND inode=%llu " \
	"ORDER BY fp_index"

#define SQL_VOLUME_INODE_FP_DELETE \
	"DELETE FROM volume_inode_fingerprints WHERE volume_id=%llu AND inode=%llu"

#define SQL_VOLUME_INODE_FP_REPLACE \
	"REPLACE INTO volume_inode_fingerprints " \
	"(volume_id, inode, fp_index, block_no, hash_algo, block_hash) " \
	"VALUES (%llu, %llu, %u, %u, %u, UNHEX('%s'))"

/* SELECT: volume → (hash_algo, block_hash as HEX) */
#define SQL_VOLUME_BLOCK_MAP_SELECT \
  "SELECT hash_algo, HEX(block_hash) FROM volume_block_mappings " \
  "WHERE volume_id=%llu AND block_no=%llu"

/* volume_id + block_no -> hash mapping */
#define SQL_VOLUME_BLOCK_MAP_UPSERT \
  "INSERT INTO volume_block_mappings (volume_id, block_no, hash_algo, block_hash) " \
  "VALUES (%llu, %llu, %u, UNHEX('%s')) " \
  "ON DUPLICATE KEY UPDATE hash_algo=VALUES(hash_algo), block_hash=VALUES(block_hash)"

/* block_hash -> 4 data + 2 parity stripe ids + ref_count (4+2 EC) */
#define SQL_HASH_TO_ESTRIPES_SELECT \
  "SELECT estripe_1_id, estripe_2_id, estripe_3_id, estripe_4_id, " \
  "       estripe_p1_id, estripe_p2_id, ref_count " \
  "FROM hive_meta.hash_to_estripes " \
  "WHERE hash_algo=%u AND block_hash=UNHEX('%s')"

/* UPSERT: block_hash -> 4 data + 2 parity stripe ids, bump ref_count */
#define SQL_HASH_TO_ESTRIPES_UPSERT \
  "INSERT INTO hive_meta.hash_to_estripes (" \
  "  hash_algo, block_hash, ref_count, " \
  "  estripe_1_id, estripe_2_id, estripe_3_id, estripe_4_id, " \
  "  estripe_p1_id, estripe_p2_id, block_bck_hash) " \
  "VALUES (%u, UNHEX('%s'), %llu, %llu, %llu, %llu, %llu, %llu, %llu, NULL) " \
  "ON DUPLICATE KEY UPDATE " \
  "  ref_count = ref_count + VALUES(ref_count), " \
  "  estripe_1_id = VALUES(estripe_1_id), " \
  "  estripe_2_id = VALUES(estripe_2_id), " \
  "  estripe_3_id = VALUES(estripe_3_id), " \
  "  estripe_4_id = VALUES(estripe_4_id), " \
  "  estripe_p1_id = VALUES(estripe_p1_id), " \
  "  estripe_p2_id = VALUES(estripe_p2_id)"

/* Insert a single EC stripe payload as binary, with version */
#define SQL_ECBLOCKS_INSERT_HEX \
  "INSERT INTO hive_data.ecblocks (estripe_version, ec_block) " \
  "VALUES (%llu, UNHEX('%s'))"

/* Lookup the 6 stripe ids by binary block hash, SELECT the raw BLOB */
#define SQL_ECBLOCK_SELECT \
  "SELECT ec_block FROM hive_data.ecblocks WHERE estripe_id=%llu"


/* The great news here is we removed a LOT of slow SQL by moving Rocks to a C-Library form rather than
 * controlled by MariaDB. And the cost was only a little bit of code in hive_guard_kv. (though knowing
 * me it may bloat over time-I'm already wondering what other features I can add to the KV.) 
 * */


/* Prototypes */
void init_hive_link(void);
void close_hive_link(void);
int get_hive_vers(void);
MYSQL_RES *hifs_get_hive_host_data(char *machine_id);
int register_hive_host(void);
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
                             const uint8_t *buf, uint32_t len);
bool hifs_volume_block_store_encoded(uint64_t volume_id, uint64_t block_no,
                                     const struct hifs_ec_stripe_set *ec);
bool hifs_volume_block_ec_encode(const uint8_t *buf, uint32_t len,
				 enum hifs_hash_algorithm algo,
				 struct hifs_ec_stripe_set *out);
void hifs_volume_block_ec_free(struct hifs_ec_stripe_set *ec);

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
