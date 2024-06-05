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

#define MACHINE_INSERT(buffer, a,b,c,d,e,f,g,h) sprintf(buffer, "INSERT INTO machines (name, machine_id, host_id, ip_address, os_name, os_version, os_release, machine_type) VALUES ('%s', '%s', '%ld', '%s', '%s', '%s', '%s', '%s');",a,b,c,d,e,f,g,h);

#define MACHINE_GETINFO(a, buffer) sprintf(buffer, "SELECT FROM machines name, host_id, ip_address, os_name, os_version WHERE machine_id = '%s';", a)


/* SQL Connect */
struct PSQL {
	PGconn *hive_conn;	/* Connection to hive */
	PGresult   *last_qury; /* Last query result */
	PGresult   *last_ins; /* Last insert result */
	int  rec_count;     /* Number of records in last query */
	int  rows;
	int  cols;
	int  rows_ins;
	bool sql_init;
};
extern struct PSQL sqldb;