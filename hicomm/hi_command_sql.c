/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

#include "hi_command.h"
#include "sql/hi_command_sql.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/utsname.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

struct SQLDB sqldb;


static char *hifs_get_machine_id(void)
{
	static char id_buf[64];
	FILE *fp = fopen("/etc/machine-id", "r");
	if (fp) {
		if (fgets(id_buf, sizeof(id_buf), fp)) {
			id_buf[strcspn(id_buf, "\n")] = '\0';
			fclose(fp);
			if (id_buf[0] != '\0')
				return id_buf;
		}
		fclose(fp);
	}

	if (gethostname(id_buf, sizeof(id_buf)) == 0)
		return id_buf;

	strcpy(id_buf, "unknown");
	return id_buf;
}

static long hifs_get_host_id(void)
{
	long id = gethostid();
	if (id == -1) {
		char hostname[256];
		if (gethostname(hostname, sizeof(hostname)) == 0) {
			unsigned long hash = 5381;
			for (const unsigned char *p = (const unsigned char *)hostname; *p; ++p)
				hash = ((hash << 5) + hash) + *p;
			id = (long)hash;
		} else {
			id = 0;
		}
	}
	return id;
}

static bool hifs_execute_query(const char *sql, MYSQL_RES **out_res)
{
	if (!sqldb.sql_init || !sqldb.conn) {
		hifs_err("MariaDB connection is not initialised\n");
		return false;
	}

	if (sqldb.last_query) {
		mysql_free_result(sqldb.last_query);
		sqldb.last_query = NULL;
	}

	hifs_debug("SQL query is [%s]", sql);

	if (mysql_query(sqldb.conn, sql) != 0) {
		hifs_err("SQL execution failed: %s\n", mysql_error(sqldb.conn));
		if (out_res)
			*out_res = NULL;
		return false;
	}

	unsigned int field_count = mysql_field_count(sqldb.conn);
	sqldb.last_affected = mysql_affected_rows(sqldb.conn);
	sqldb.last_insert_id = mysql_insert_id(sqldb.conn);

	if (field_count == 0) {
		sqldb.rec_count = 0;
		sqldb.rows = (int)sqldb.last_affected;
		sqldb.cols = 0;
		if (out_res)
			*out_res = NULL;
		return true;
	}

	MYSQL_RES *res = mysql_store_result(sqldb.conn);
	if (!res) {
		hifs_err("Failed to fetch result set: %s\n", mysql_error(sqldb.conn));
		if (out_res)
			*out_res = NULL;
		return false;
	}

	sqldb.last_query = res;
	sqldb.rec_count = (int)mysql_num_rows(res);
	sqldb.rows = sqldb.rec_count;
	sqldb.cols = mysql_num_fields(res);

	if (out_res)
		*out_res = res;

	return true;
}

static const char *safe_str(const char *s)
{
	return s ? s : "";
}

void init_hive_link(void)
{
	if (sqldb.sql_init)
		return;

	sqldb.conn = mysql_init(NULL);
	if (!sqldb.conn) {
		hifs_err("Failed to initialise MariaDB handle\n");
		return;
	}

	if (!mysql_real_connect(sqldb.conn,
				DB_HOST,
				DB_USER,
				DB_PASS,
				DB_NAME,
				DB_PORT,
				DB_SOCKET,
				DB_FLAGS)) {
		hifs_err("Connection to MariaDB failed: %s\n", mysql_error(sqldb.conn));
		mysql_close(sqldb.conn);
		sqldb.conn = NULL;
		return;
	}

	sqldb.sql_init = true;
	unsigned long version = mysql_get_server_version(sqldb.conn);
	hifs_notice("hi_command: Connected to MariaDB server version %lu\n", version);
}

void close_hive_link(void)
{
	if (sqldb.last_query) {
		mysql_free_result(sqldb.last_query);
		sqldb.last_query = NULL;
	}

	if (sqldb.conn)
		mysql_close(sqldb.conn);

	memset(&sqldb, 0, sizeof(sqldb));
}

int get_hive_vers(void)
{
	if (!sqldb.sql_init || !sqldb.conn)
		return 0;
	return (int)mysql_get_server_version(sqldb.conn);
}

static MYSQL_RES *hifs_execute_sql(const char *sql_string)
{
	MYSQL_RES *res = NULL;
	if (!hifs_execute_query(sql_string, &res))
		return NULL;
	return res;
}

MYSQL_RES *hifs_get_hive_host_data(char *machine_id)
{
	char sql_query[MAX_QUERY_SIZE];
	MYSQL_RES *res;

	MACHINE_GETINFO(sql_query, machine_id);
	res = hifs_execute_sql(sql_query);

	if (!res) {
		sqldb.rows = 0;
		return NULL;
	}

	sqldb.last_query = res;
	sqldb.rows = (int)mysql_num_rows(res);

	return res;
}

static bool hifs_insert_sql(const char *sql_string)
{
	MYSQL_RES *res = NULL;
	if (!hifs_execute_query(sql_string, &res))
		return false;
	return true;
}

static long str_to_long(const char *s)
{
	if (!s)
		return 0;
	return strtol(s, NULL, 10);
}

int register_hive_host(void)
{
	char sql_query[MAX_QUERY_SIZE];
	char ip_address[64] = {0};
	char *hive_mach_id = NULL;
	long hive_host_id = 0;
	struct hostent *host_entry;
	struct in_addr addr;
	struct utsname uts;

	char name[100] = {0};
	if (gethostname(name, sizeof(name)) != 0) {
		hifs_err("gethostname failed: %s\n", strerror(errno));
		return 0;
	}
	hifs_debug("Host name is [%s]\n", name);

	host_entry = gethostbyname(name);
	if (!host_entry) {
		hifs_crit("gethostbyname failed: %s\n", hstrerror(h_errno));
		return 0;
	}

	memcpy(&addr.s_addr, host_entry->h_addr_list[0], host_entry->h_length);
	snprintf(ip_address, sizeof(ip_address), "%s", inet_ntoa(addr));
	hifs_debug("IP address is [%s]\n", ip_address);

	if (uname(&uts) != 0) {
		hifs_err("uname failed: %s\n", strerror(errno));
		return 0;
	}

	hive_mach_id = hifs_get_machine_id();
	hive_host_id = hifs_get_host_id();

	hifs_info("Hive machine ID is [%s] and host ID is [%ld]\n", hive_mach_id, hive_host_id);

	MYSQL_RES *res = hifs_get_hive_host_data(hive_mach_id);
	if (!res) {
		hifs_err("Failed to query hive host data\n");
		return 0;
	}

	MYSQL_ROW row;
	unsigned int row_count = 0;
	mysql_data_seek(res, 0);
	while ((row = mysql_fetch_row(res)) != NULL) {
		row_count++;
		sqldb.host.m_id = row[0];
		sqldb.host.name = row[1];
		sqldb.host.host_id = str_to_long(row[2]);
		sqldb.host.ip_address = row[3];
		sqldb.host.os_name = row[4];
		sqldb.host.os_version = row[5];
		sqldb.host.machine_type = row[6];
		sqldb.host.os_release = row[7];
		sqldb.host.machine_id = hive_mach_id;

		hifs_debug("M_ID: %s, Name: %s, Host ID: %ld, IP Address: %s, "
			   "OS Name: %s, OS Version: %s, Machine Type: %s, OS Release: %s, Machine ID: %s\n",
			   safe_str(sqldb.host.m_id), safe_str(sqldb.host.name),
			   sqldb.host.host_id, safe_str(sqldb.host.ip_address),
			   safe_str(sqldb.host.os_name), safe_str(sqldb.host.os_version),
			   safe_str(sqldb.host.machine_type), safe_str(sqldb.host.os_release),
			   safe_str(sqldb.host.machine_id));
	}

	if (row_count > 0)
		return 1;

	hifs_notice("This host does not exist in the hive. Filesystems cannot be mounted without a hive connection.");
	hifs_notice("Would you like to add this host to the hive? [y/n]\n");

	char response = 'n';
	if (scanf(" %c", &response) != 1 || (response != 'y' && response != 'Y')) {
		hifs_notice("Not registering host to Hive.");
		return 0;
	}

	sqldb.host.name = name;
	sqldb.host.machine_id = hive_mach_id;
	sqldb.host.host_id = hive_host_id;
	sqldb.host.ip_address = ip_address;
	sqldb.host.os_name = uts.sysname;
	sqldb.host.os_version = uts.version;
	sqldb.host.os_release = uts.release;
	sqldb.host.machine_type = uts.machine;

	MACHINE_INSERT(sql_query, name, hive_mach_id, hive_host_id, ip_address,
		       uts.sysname, uts.version, uts.release, uts.machine);

	hifs_notice("Registering host to Hive.");
	if (!hifs_insert_sql(sql_query))
		return 0;

	hifs_info("Registered host to hive: name [%s] machine ID [%s] host ID [%ld] "
		  "IP address [%s] OS name [%s] OS version [%s] OS release [%s] machine type [%s]\n",
		  name, hive_mach_id, hive_host_id, ip_address,
		  uts.sysname, uts.version, uts.release, uts.machine);
	return 1;
}

char *hifs_get_quoted_value(const char *in_str)
{
	if (!sqldb.sql_init || !sqldb.conn || !in_str)
		return NULL;

	size_t len = strlen(in_str);
	char *escaped = malloc(len * 2 + 1);
	if (!escaped)
		return NULL;

	unsigned long written = mysql_real_escape_string(sqldb.conn, escaped, in_str, len);
	escaped[written] = '\0';
	return escaped;
}

char *hifs_get_unquoted_value(const char *in_str)
{
	if (!in_str)
		return NULL;
	return strdup(in_str);
}

void hifs_release_query(void)
{
	if (sqldb.last_query) {
		mysql_free_result(sqldb.last_query);
		sqldb.last_query = NULL;
	}
}

bool hifs_insert_data(const char *q_string)
{
	if (!q_string)
		return false;

	if (!hifs_insert_sql(q_string))
		return false;

	hifs_debug("Inserted %llu rows\n", sqldb.last_affected);
	return true;
}

int hifs_get_hive_host_sbs(void)
{
	char sql_query[MAX_QUERY_SIZE];
	MYSQL_RES *res;

	MACHINE_GETSBS(sql_query, safe_str(sqldb.host.m_id));
	res = hifs_execute_sql(sql_query);
	if (!res) {
		sqldb.rows = 0;
		return 0;
	}

	sqldb.rows = (int)mysql_num_rows(res);
	mysql_data_seek(res, 0);

	for (int i = 0; i < sqldb.rows && i < (int)ARRAY_SIZE(sqldb.sb); ++i) {
		MYSQL_ROW row = mysql_fetch_row(res);
		if (!row)
			break;

		sqldb.sb[i].s_id = row[0];
		sqldb.sb[i].mach_id = row[1];
		sqldb.sb[i].f_id = row[2];
		sqldb.sb[i].magic_num = row[3];
		sqldb.sb[i].block_size = row[4] ? atoi(row[4]) : 0;
		sqldb.sb[i].mount_time = row[5];
		sqldb.sb[i].write_time = row[6];
		sqldb.sb[i].mount_count = row[7] ? atoi(row[7]) : 0;
		sqldb.sb[i].max_mount_count = row[8] ? atoi(row[8]) : 0;
		sqldb.sb[i].filesys_state = row[9] ? atoi(row[9]) : 0;
		sqldb.sb[i].max_inodes = row[10] ? atol(row[10]) : 0;
		sqldb.sb[i].max_blocks = row[11] ? atol(row[11]) : 0;
		sqldb.sb[i].free_blocks = row[12] ? atol(row[12]) : 0;
		sqldb.sb[i].free_inodes = row[13] ? atol(row[13]) : 0;
		sqldb.sb[i].blocks_shared = row[14] ? atol(row[14]) : 0;

		hifs_debug("s_id: %s mach_id: %s f_id: %s magic_num: %s block_size: %d "
			   "mount_time: %s write_time: %s mount_count: %d max_mount_count: %d "
			   "filesys_state: %d max_inodes: %ld max_blocks: %ld free_blocks: %ld "
			   "free_inodes: %ld blocks_shared: %ld\n",
			   safe_str(sqldb.sb[i].s_id), safe_str(sqldb.sb[i].mach_id),
			   safe_str(sqldb.sb[i].f_id), safe_str(sqldb.sb[i].magic_num),
			   sqldb.sb[i].block_size, safe_str(sqldb.sb[i].mount_time),
			   safe_str(sqldb.sb[i].write_time), sqldb.sb[i].mount_count,
			   sqldb.sb[i].max_mount_count, sqldb.sb[i].filesys_state,
			   sqldb.sb[i].max_inodes, sqldb.sb[i].max_blocks,
			   sqldb.sb[i].free_blocks, sqldb.sb[i].free_inodes,
			   sqldb.sb[i].blocks_shared);
	}

	return sqldb.rows > 0;
}

int save_binary_data(char *data_block, char *hash)
{
	(void)data_block;
	(void)hash;
	return 0;
}

