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

/* SQL Connect */
struct PSQL {
	PGconn *hive_conn;	/* Connection to hive */
	PGresult   *last_qury; /* Last query result */
	PGresult   *last_ins; /* Last insert result */
	int  rec_count;     /* Number of records in last query */
	int  row;
	int  col;
	bool sql_init;
};
extern struct PSQL sqldb;