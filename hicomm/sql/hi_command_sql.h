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
#define HOST "10.100.122.20"
#define DATABASE "hivefs"
#define USER "postgres"
#define PASSWORD "Postgres!909"
#define HIVE "10.100.122.20"
#define PORT "5432"
#define NO_RECORDS 0x099
#define DBSTRING "user=" USER " dbname=" DATABASE " password=" PASSWORD " hostaddr=" HOST " port=" PORT

#define MAX_QUERY_SIZE 512
#define MAX_INSERT_SIZE 512

/***********************
 * SQL Definitions
 ***********************/
#define MACHINE_INSERT(buffer, a, b, c, d, e, f, g, h) \
    sprintf(buffer, "INSERT INTO machines (name, machine_id, host_id, ip_address, os_name, os_version, os_release, machine_type) VALUES ('%s', '%s', %ld, '%s', '%s', '%s', '%s', '%s');", a, b, c, d, e, f, g, h)

#define MACHINE_GETINFO(buffer, a) \
    sprintf(buffer, "SELECT m_id, name, host_id, ip_address, os_name, os_version, machine_type, os_release FROM machines WHERE machine_id = '%s';", a)

#define MACHINE_GETSBS(buffer, a) \
    sprintf(buffer, "SELECT s_id, mach_id, f_id, magic_num, block_size, mount_time, write_time, mount_count, max_mount_count, filesys_state, max_inodes, max_blocks, free_blocks, free_inodes, blocks_shared FROM superblocks WHERE m_id = '%s';", a)

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
struct PSQL {
	PGconn *hive_conn;	/* Connection to hive */
	PGresult   *last_qury; /* Last query result */
	PGresult   *last_ins; /* Last insert result */
	int  rec_count;     /* Number of records in last query */
	int  rows;
	int  cols;
	int  rows_ins;
	bool sql_init;
	struct machine host;
	struct superblock sb[50];
};
extern struct PSQL sqldb;