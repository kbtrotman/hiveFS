/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */
#pragma once

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

#define DB_HOST     "127.0.0.1"
#define DB_NAME     "hivefs"
#define DB_USER     "hiveadmin"
#define DB_PASS     "MyHiveFS"
#define DB_PORT     3306
#define DB_SOCKET   NULL
#define DB_FLAGS    0

#define NO_RECORDS 0x099

#define MAX_QUERY_SIZE 4096
#define MAX_INSERT_SIZE 512

/***********************
 * SQL Definitions
 ***********************/
#define MACHINE_INSERT(buffer, a, b, c, d, e, f, g, h) \
    snprintf(buffer, MAX_QUERY_SIZE, \
        "INSERT INTO machines (name, machine_id, host_id, ip_address, os_name, os_version, os_release, machine_type) " \
        "VALUES ('%.*s', '%.*s', %ld, '%.*s', '%.*s', '%.*s', '%.*s', '%.*s') " \
        "ON DUPLICATE KEY UPDATE name=VALUES(name), host_id=VALUES(host_id), ip_address=VALUES(ip_address), " \
        "os_name=VALUES(os_name), os_version=VALUES(os_version), os_release=VALUES(os_release), machine_type=VALUES(machine_type);", \
        255, safe_str(a), 255, safe_str(b), (long)(c), 255, safe_str(d), 255, safe_str(e), \
        255, safe_str(f), 255, safe_str(g), 255, safe_str(h))

#define MACHINE_GETINFO(buffer, a) \
    snprintf(buffer, MAX_QUERY_SIZE, \
             "SELECT m_id, name, host_id, ip_address, os_name, os_version, machine_type, os_release FROM machines WHERE machine_id = '%s';", a)

#define MACHINE_GETSBS(buffer, a) \
    snprintf(buffer, MAX_QUERY_SIZE, \
             "SELECT s_id, mach_id, f_id, magic_num, block_size, mount_time, write_time, mount_count, max_mount_count, filesys_state, " \
             "max_inodes, max_blocks, free_blocks, free_inodes, blocks_shared FROM superblocks WHERE m_id = '%s';", a)

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

/* SQL Connect */


struct superblock {
	char *s_id;
	char *mach_id; 
	char *f_id; 
	char *magic_num; 
	int block_size; 
	char *mount_time; 
	char *write_time; 
	int mount_count; 
	int max_mount_count; 
	int filesys_state; 
	long max_inodes; 
	long max_blocks; 
	long free_blocks; 
	long free_inodes; 
	long blocks_shared;
};

struct machine {
	char *m_id;
	char *name;
	char *machine_id;
	long host_id;
	char *ip_address;
	char *os_name;
	char *os_version;
	char *os_release;
	char *machine_type;
	char *create_time;
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
extern struct SQLDB sqldb;
