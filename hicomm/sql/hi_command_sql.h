/***********************
 * Database Constants
 ***********************/
#define HOST "10.100.122.20"
#define DATABASE "hiveFS"
#define USER "postgres"
#define PASSWORD "Postgres!909"
#define HIVE "10.100.122.20"
#define PORT "5432"
#define NO_RECORDS 0x099
#define DBSTRING "user=" USER " dbname=" DATABASE " password=" PASSWORD " hostaddr=" HOST " port=" PORT

/* SQL Connect */
static struct {
	PGconn *hive_conn;	/* Connection to hive */
	PGresult   *last_qury; /* Last query result */
	PGresult   *last_ins; /* Last insert result */
	int  rec_count;     /* Number of records in last query */
	int  row;
	int  col;
	bool *sql_init;
} sql;
