/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

#include <arpa/inet.h>
#include <stdlib.h>
#include <endian.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/utsname.h>
#include <pthread.h>
#include <time.h>
#include <openssl/sha.h>

#include "hive_guard.h"
#include "hive_guard_sql.h"
#include "hive_guard_kv.h"
#include "hive_guard_erasure_code.h"
#include "hive_guard_sn_tcp.h"
#include "hive_guard_raft.h"
#include "../common/hive_common_sql.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

MYSQL *hg_sql_get_db(void)
{
	return sqldb.conn;
}

struct stripe_locations {
	struct stripe_location entries[HIFS_EC_STRIPES];
	size_t count;
	bool ok;
};


static struct stripe_locations hifs_read_block_to_stripe_locations(uint64_t volume_id,
							      uint64_t block_no,
							      const uint8_t hash[HIFS_BLOCK_HASH_SIZE],
							      size_t count);

static void bytes_to_hex(const uint8_t *src, size_t len, char *dst)
{
	static const char hexdigits[] = "0123456789abcdef";
	size_t i;

	for (i = 0; i < len; ++i) {
		dst[i * 2] = hexdigits[(src[i] >> 4) & 0xF];
		dst[i * 2 + 1] = hexdigits[src[i] & 0xF];
	}
	dst[len * 2] = '\0';
}

static int hex_nibble(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static bool hex_to_bytes(const char *hex, size_t hex_len, uint8_t *dst, size_t dst_len)
{
	size_t i;

	if (!hex || !dst)
		return false;
	if (hex_len % 2 != 0)
		return false;
	if (dst_len < hex_len / 2)
		return false;

	for (i = 0; i < hex_len / 2; ++i) {
		int hi = hex_nibble(hex[i * 2]);
		int lo = hex_nibble(hex[i * 2 + 1]);
		if (hi < 0 || lo < 0)
			return false;
		dst[i] = (uint8_t)((hi << 4) | lo);
	}
	if (dst_len > hex_len / 2)
		memset(dst + hex_len / 2, 0, dst_len - hex_len / 2);
	return true;
}

static uint32_t sql_to_u32(const char *s)
{
	return s ? (uint32_t)strtoul(s, NULL, 10) : 0;
}

static uint64_t sql_to_u64(const char *s)
{
	return s ? strtoull(s, NULL, 10) : 0ULL;
}

static const char *safe_str(const char *s)
{
	return s ? s : "";
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

snprintf(sql_query, sizeof(sql_query), SQL_HOST_SELECT_BY_SERIAL,
	 safe_str(machine_id));
res = hifs_execute_sql(sql_query);

	if (!res) {
		sqldb.rows = 0;
		return NULL;
	}

	sqldb.last_query = res;
	sqldb.rows = (int)mysql_num_rows(res);

	return res;
}
static long str_to_long(const char *s)
{
	if (!s)
		return 0;
	return strtol(s, NULL, 10);
}

struct metadata_sql_job {
	char *sql;
	size_t len;
	struct metadata_sql_job *next;
};

static pthread_mutex_t g_metadata_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_metadata_cv = PTHREAD_COND_INITIALIZER;
static struct metadata_sql_job *g_metadata_head;
static struct metadata_sql_job *g_metadata_tail;
static bool g_metadata_stop;
static bool g_metadata_thread_started;
static pthread_t g_metadata_thread;
static size_t g_metadata_depth;

#define HIFS_METADATA_QUEUE_MAX 4096

static void hifs_metadata_job_free(struct metadata_sql_job *job)
{
	if (!job)
		return;
	free(job->sql);
	free(job);
}

static MYSQL *hifs_metadata_open_connection(void)
{
	MYSQL *conn = mysql_init(NULL);

	if (!conn)
		return NULL;
	if (!mysql_real_connect(conn,
				DB_HOST,
				DB_USER,
				DB_PASS,
				DB_NAME,
				DB_PORT,
				DB_SOCKET,
				DB_FLAGS)) {
		hifs_warning("metadata sql: connect failed: %s",
			     mysql_error(conn));
		mysql_close(conn);
		return NULL;
	}
	return conn;
}

static bool hifs_metadata_execute_sql(MYSQL *conn,
				      const char *sql,
				      size_t len)
{
	if (!conn || !sql)
		return false;
	if (mysql_real_query(conn, sql, (unsigned long)len) != 0)
		return false;

	if (mysql_field_count(conn) == 0)
		return true;

	MYSQL_RES *res = mysql_store_result(conn);

	if (!res) {
		if (mysql_field_count(conn) != 0)
			return false;
		return true;
	}

	mysql_free_result(res);
	return true;
}

static void hifs_metadata_worker_backoff(void)
{
	struct timespec ts = {
		.tv_sec = 0,
		.tv_nsec = 200 * 1000 * 1000,
	};

	nanosleep(&ts, NULL);
}

static struct metadata_sql_job *hifs_metadata_job_pop(void)
{
	struct metadata_sql_job *job = g_metadata_head;

	if (!job)
		return NULL;
	g_metadata_head = job->next;
	if (!g_metadata_head)
		g_metadata_tail = NULL;
	if (g_metadata_depth > 0)
		--g_metadata_depth;
	return job;
}

static void *hifs_metadata_worker_main(void *arg)
{
	(void)arg;
	MYSQL *conn = NULL;

	pthread_mutex_lock(&g_metadata_mu);
	for (;;) {
		while (!g_metadata_stop && !g_metadata_head)
			pthread_cond_wait(&g_metadata_cv, &g_metadata_mu);

		if (g_metadata_stop && !g_metadata_head) {
			pthread_mutex_unlock(&g_metadata_mu);
			break;
		}

		struct metadata_sql_job *job = hifs_metadata_job_pop();
		pthread_mutex_unlock(&g_metadata_mu);

		if (!job)
			continue;

		bool done = false;
		while (!done) {
			if (!conn) {
				conn = hifs_metadata_open_connection();
				if (!conn) {
					hifs_metadata_worker_backoff();
					continue;
				}
			}

			if (hifs_metadata_execute_sql(conn,
						      job->sql,
						      job->len)) {
				done = true;
			} else {
				hifs_warning("metadata sql: query failed: %s",
					     mysql_error(conn));
				mysql_close(conn);
				conn = NULL;
				hifs_metadata_worker_backoff();
			}
		}

		hifs_metadata_job_free(job);
		pthread_mutex_lock(&g_metadata_mu);
	}

	if (conn)
		mysql_close(conn);
	return NULL;
}

static bool hifs_metadata_worker_start_locked(void)
{
	if (g_metadata_thread_started)
		return true;
	g_metadata_stop = false;
	int rc = pthread_create(&g_metadata_thread,
				NULL,
				hifs_metadata_worker_main,
				NULL);
	if (rc != 0) {
		hifs_err("metadata sql: pthread_create failed: %s",
			 strerror(rc));
		return false;
	}

	g_metadata_thread_started = true;
	return true;
}

bool hifs_metadata_async_execute(const char *sql_string)
{
	if (!sql_string || !sql_string[0])
		return false;

	struct metadata_sql_job *job = calloc(1, sizeof(*job));

	if (!job)
		return false;

	size_t len = strlen(sql_string);
	job->sql = malloc(len + 1);
	if (!job->sql) {
		free(job);
		return false;
	}

	memcpy(job->sql, sql_string, len + 1);
	job->len = len;

	pthread_mutex_lock(&g_metadata_mu);
	bool started = hifs_metadata_worker_start_locked();
	if (!started) {
		pthread_mutex_unlock(&g_metadata_mu);
		hifs_metadata_job_free(job);
		return false;
	}

	if (g_metadata_depth >= HIFS_METADATA_QUEUE_MAX) {
		hifs_warning("metadata sql: queue full (depth=%zu)",
			     g_metadata_depth);
		pthread_mutex_unlock(&g_metadata_mu);
		hifs_metadata_job_free(job);
		return false;
	}

	if (!g_metadata_head)
		g_metadata_head = job;
	else
		g_metadata_tail->next = job;
	g_metadata_tail = job;
	++g_metadata_depth;
	pthread_cond_signal(&g_metadata_cv);
	pthread_mutex_unlock(&g_metadata_mu);
	return true;
}

void hifs_metadata_async_shutdown(void)
{
	pthread_mutex_lock(&g_metadata_mu);
	if (!g_metadata_thread_started) {
		pthread_mutex_unlock(&g_metadata_mu);
		return;
	}

	g_metadata_stop = true;
	pthread_cond_broadcast(&g_metadata_cv);
	pthread_mutex_unlock(&g_metadata_mu);

	pthread_join(g_metadata_thread, NULL);

	pthread_mutex_lock(&g_metadata_mu);
	g_metadata_thread_started = false;
	g_metadata_stop = false;
	while (g_metadata_head) {
		struct metadata_sql_job *job = hifs_metadata_job_pop();
		hifs_metadata_job_free(job);
	}
	g_metadata_tail = NULL;
	pthread_mutex_unlock(&g_metadata_mu);
}

int get_host_info(void)
{
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
	if (hive_mach_id)
		hifs_safe_strcpy(storage_node_uid, sizeof(storage_node_uid), hive_mach_id);
	snprintf(storage_node_serial, sizeof(storage_node_serial), "%ld", hive_host_id);
	hifs_safe_strcpy(storage_node_name, sizeof(storage_node_name), name);
	hifs_safe_strcpy(storage_node_address, sizeof(storage_node_address), ip_address);

	hifs_info("Hive machine ID is [%s] and host ID is [%ld]\n",
		  hive_mach_id ? hive_mach_id : "unknown", hive_host_id);
	return 1;
}

bool get_hive_host_data(void)
{

	char sql_query[MAX_QUERY_SIZE];
	char ip_address[64] = {0};
	char *hive_mach_id = hifs_get_machine_id();
	long hive_host_id = hifs_get_host_id();
	struct hostent *host_entry;
	struct in_addr addr;
	struct utsname uts;
	char name[100] = {0};
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
		sqldb.host.serial = row[0];
		sqldb.host.name = row[1];
		sqldb.host.host_id = row[2] ? str_to_long(row[2]) : 0;
		sqldb.host.os_name = row[3];
		sqldb.host.os_version = row[4];
		sqldb.host.create_time = row[5];

		hifs_debug("serial: %s, name: %s, host_id: %ld, os: %s %s, created: %s",
			   safe_str(sqldb.host.serial), safe_str(sqldb.host.name),
			   sqldb.host.host_id,
			   safe_str(sqldb.host.os_name),
			   safe_str(sqldb.host.os_version),
			   safe_str(sqldb.host.create_time));
	}

	if (row_count > 0)
		return 1;

	if (gethostname(name, sizeof(name)) != 0) {
		hifs_err("gethostname failed: %s\n", strerror(errno));
		return 0;
	}

	host_entry = gethostbyname(name);
	if (!host_entry) {
		hifs_err("gethostbyname failed: %s\n", hstrerror(h_errno));
		return 0;
	}

	memcpy(&addr.s_addr, host_entry->h_addr_list[0], host_entry->h_length);
	snprintf(ip_address, sizeof(ip_address), "%s", inet_ntoa(addr));

	if (uname(&uts) != 0) {
		hifs_err("uname failed: %s\n", strerror(errno));
		return 0;
	}

	hive_mach_id = hifs_get_machine_id();
	hive_host_id = hifs_get_host_id();

	hifs_notice("This host does not exist in the hive. Filesystems cannot be mounted without a hive connection.");
	hifs_notice("Would you like to add this host to the hive? [y/n]\n");

	char response = 'n';
	if (scanf(" %c", &response) != 1 || (response != 'y' && response != 'Y')) {
		hifs_notice("Not registering host to Hive.");
		return 0;
	}

	sqldb.host.serial = hive_mach_id;
	sqldb.host.name = name;
	sqldb.host.host_id = hive_host_id;
	sqldb.host.os_name = uts.sysname;
	sqldb.host.os_version = uts.release;
	sqldb.host.create_time = NULL;

	char *serial_q = hifs_get_quoted_value(hive_mach_id);
	char *name_q = hifs_get_quoted_value(name);
	char *addr_q = hifs_get_quoted_value(ip_address);
	char *os_name_q = hifs_get_quoted_value(uts.sysname);
	char *os_version_q = hifs_get_quoted_value(uts.release);
	if (!serial_q || !name_q || !addr_q || !os_name_q || !os_version_q) {
		free(serial_q);
		free(name_q);
		free(addr_q);
		free(os_name_q);
		free(os_version_q);
		hifs_err("Out of memory while preparing host registration query");
		return 0;
	}

	unsigned int hicom_port = hifs_local_guard_port();
	uint64_t epoch = (uint64_t)time(NULL);
	unsigned int fenced = 0;

	snprintf(sql_query, sizeof(sql_query), SQL_HOST_UPSERT,
		 safe_str(serial_q), safe_str(name_q), hive_host_id,
		 safe_str(addr_q), safe_str(os_name_q), safe_str(os_version_q),
		 hicom_port, (unsigned long long)epoch, fenced);

	free(serial_q);
	free(name_q);
	free(addr_q);
	free(os_name_q);
	free(os_version_q);

	hifs_notice("Registering host to Hive.");
	if (!hifs_insert_sql(sql_query))
		return 0;

	hifs_info("Registered host to hive: name [%s] machine ID [%s] host ID [%ld] "
		  "IP address [%s] OS [%s %s]",
		  name, hive_mach_id, hive_host_id, ip_address,
		  uts.sysname, uts.release);
	return 1;
}

bool hifs_persist_cluster_record(uint64_t cluster_id,
				 const char *cluster_name,
				 const char *cluster_desc,
				 uint16_t min_nodes)
{
	init_hive_link();
	if (!sqldb.sql_init || !sqldb.conn) {
		fprintf(stderr, "cluster_config: MariaDB connection not available\n");
		return false;
	}

	const char *name_src = (cluster_name && cluster_name[0]) ?
			       cluster_name :
			       "hive_cluster";
	const char *desc_src = (cluster_desc && cluster_desc[0]) ?
			       cluster_desc :
			       "Initial cluster configuration";
	size_t name_len = (cluster_name && cluster_name[0]) ?
			  strnlen(cluster_name, HIFS_CLUSTER_NAME_MAX) :
			  strlen(name_src);
	size_t desc_len = (cluster_desc && cluster_desc[0]) ?
			  strnlen(cluster_desc, HIFS_CLUSTER_DESC_MAX) :
			  strlen(desc_src);

	char name_sql[HIFS_CLUSTER_NAME_MAX * 2 + 1];
	char desc_sql[HIFS_CLUSTER_DESC_MAX * 2 + 1];

	unsigned long esc_len = mysql_real_escape_string(
		sqldb.conn, name_sql, name_src, (unsigned long)name_len);
	name_sql[esc_len] = '\0';
	esc_len = mysql_real_escape_string(
		sqldb.conn, desc_sql, desc_src, (unsigned long)desc_len);
	desc_sql[esc_len] = '\0';

	uint16_t min_required = min_nodes ? min_nodes : 1;
	char sql_query[MAX_QUERY_SIZE];
	int written = snprintf(sql_query, sizeof(sql_query), SQL_CLUSTER_UPSERT,
			       (unsigned long long)cluster_id,
			       name_sql,
			       desc_sql,
			       1U,
			       1U,
			       "pending",
			       "ok",
			       (unsigned int)min_required,
			       1U);
	if (written < 0 || (size_t)written >= sizeof(sql_query)) {
		fprintf(stderr, "cluster_config: SQL buffer too small\n");
		return false;
	}

	if (mysql_real_query(sqldb.conn, sql_query, (unsigned long)written) != 0) {
		fprintf(stderr, "cluster_config: SQL failed: %s\n",
			mysql_error(sqldb.conn));
		return false;
	}

	MYSQL_RES *res = mysql_store_result(sqldb.conn);

	if (res)
		mysql_free_result(res);
	else if (mysql_field_count(sqldb.conn) != 0) {
		fprintf(stderr, "cluster_config: failed to consume SQL result: %s\n",
			mysql_error(sqldb.conn));
		return false;
	}

	return true;
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
			 const char *health)
{
	if (!sqldb.sql_init || !sqldb.conn)
		return false;

	char *name_sql = hifs_get_quoted_value(fs_name ? fs_name : "");
	char *path_sql = hifs_get_quoted_value(fs_path ? fs_path : "");
	char *type_sql = hifs_get_quoted_value(fs_type ? fs_type : "");
	char *health_sql = hifs_get_quoted_value(health ? health : "ok");

	const char *name_use = name_sql ? name_sql : "";
	const char *path_use = path_sql ? path_sql : "";
	const char *type_use = type_sql ? type_sql : "";
	const char *health_use = health_sql ? health_sql : "ok";

	char sql_query[MAX_QUERY_SIZE];
	int written = snprintf(sql_query, sizeof(sql_query),
				 SQL_STORAGE_NODE_FS_STATS_INSERT,
				 (unsigned long long)node_id,
				 (unsigned long long)ts_unix,
				 name_use,
				 path_use,
				 type_use,
				 (unsigned long long)fs_total_bytes,
				 (unsigned long long)fs_used_bytes,
				 (unsigned long long)fs_avail_bytes,
				 fs_used_pct,
				 (unsigned long long)in_total,
				 (unsigned long long)in_used,
				 (unsigned long long)in_avail,
				 in_used_pct,
				 health_use);

	free(name_sql);
	free(path_sql);
	free(type_sql);
	free(health_sql);

	if (written <= 0 || written >= (int)sizeof(sql_query))
		return false;

	return hifs_metadata_async_execute(sql_query);
}


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
			  uint64_t read_ms,
			  uint64_t write_ms,
			  uint64_t io_in_progress,
			  uint64_t io_ms,
			  uint64_t weighted_io_ms,
			  const char *fs_path,
			  const char *health)
{
	if (!sqldb.sql_init || !sqldb.conn)
		return false;

	char *name_sql   = hifs_get_quoted_value(disk_name ? disk_name : "");
	char *path_sql   = hifs_get_quoted_value(disk_path ? disk_path : "");
	char *fsp_sql    = hifs_get_quoted_value(fs_path ? fs_path : "");
	char *health_sql = hifs_get_quoted_value(health ? health : "ok");

	const char *name_use   = name_sql ? name_sql : "";
	const char *path_use   = path_sql ? path_sql : "";
	const char *fsp_use    = fsp_sql ? fsp_sql : "NULL";
	const char *health_use = health_sql ? health_sql : "ok";

	/* If fs_path is NULL/empty, write SQL NULL (not quoted empty string) */
	char fs_path_buf[512];
	if (!fs_path || fs_path[0] == '\0') {
		snprintf(fs_path_buf, sizeof(fs_path_buf), "NULL");
		fsp_use = fs_path_buf;
	}

	char sql_query[MAX_QUERY_SIZE];
	int written = snprintf(sql_query, sizeof(sql_query),
			       SQL_STORAGE_NODE_DISK_STATS_INSERT,
			       (unsigned long long)node_id,
			       (unsigned long long)ts_unix,
			       name_use,
			       path_use,
			       (unsigned long long)disk_size_bytes,
			       (unsigned int)disk_rotational,
			       (unsigned long long)reads_completed,
			       (unsigned long long)writes_completed,
			       (unsigned long long)read_bytes,
			       (unsigned long long)write_bytes,
			       (unsigned long long)read_ms,
			       (unsigned long long)write_ms,
			       (unsigned long long)io_in_progress,
			       (unsigned long long)io_ms,
			       (unsigned long long)weighted_io_ms,
			       fsp_use,
			       health_use);

	free(name_sql);
	free(path_sql);
	free(fsp_sql);
	free(health_sql);

	if (written <= 0 || written >= (int)sizeof(sql_query))
		return false;

	return hifs_metadata_async_execute(sql_query);
}

int hifs_get_hive_host_sbs(void)
{
	char sql_query[MAX_QUERY_SIZE];
	MYSQL_RES *res;

	snprintf(sql_query, sizeof(sql_query), "%s", SQL_VOLUME_SUPER_LIST);
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

		sqldb.sb[i].volume_id = row[0] ? strtoull(row[0], NULL, 10) : 0;
		sqldb.sb[i].s_magic = row[1] ? (uint32_t)strtoul(row[1], NULL, 10) : 0;
		sqldb.sb[i].s_blocksize = row[2] ? (uint32_t)strtoul(row[2], NULL, 10) : 0;
		sqldb.sb[i].s_blocksize_bits = row[3] ? (uint32_t)strtoul(row[3], NULL, 10) : 0;
		sqldb.sb[i].s_blocks_count = row[4] ? strtoull(row[4], NULL, 10) : 0;
		sqldb.sb[i].s_free_blocks = row[5] ? strtoull(row[5], NULL, 10) : 0;
		sqldb.sb[i].s_inodes_count = row[6] ? strtoull(row[6], NULL, 10) : 0;
		sqldb.sb[i].s_free_inodes = row[7] ? strtoull(row[7], NULL, 10) : 0;

		hifs_debug("volume_id=%llu magic=%#x blocksize=%u blocks=%llu free_blocks=%llu "
			   "inodes=%llu free_inodes=%llu",
			   (unsigned long long)sqldb.sb[i].volume_id,
			   sqldb.sb[i].s_magic,
			   sqldb.sb[i].s_blocksize,
			   (unsigned long long)sqldb.sb[i].s_blocks_count,
			   (unsigned long long)sqldb.sb[i].s_free_blocks,
			   (unsigned long long)sqldb.sb[i].s_inodes_count,
			   (unsigned long long)sqldb.sb[i].s_free_inodes);
	}

	return sqldb.rows > 0;
}

bool hifs_volume_super_get(uint64_t volume_id, struct hifs_volume_superblock *out)
{
	char sql_query[MAX_QUERY_SIZE];
	MYSQL_RES *res;
	MYSQL_ROW row;
	unsigned long *lengths;
	bool ok = false;

	if (!sqldb.sql_init || !sqldb.conn || !out)
		return false;

	snprintf(sql_query, sizeof(sql_query), SQL_VOLUME_SUPER_SELECT,
		 (unsigned long long)volume_id);

	res = hifs_execute_sql(sql_query);
	if (!res)
		return false;

	if (mysql_num_rows(res) == 0)
		goto out;

	row = mysql_fetch_row(res);
	lengths = mysql_fetch_lengths(res);
	if (!row || !lengths)
		goto out;

	memset(out, 0, sizeof(*out));
	out->s_magic = row[0] ? (uint32_t)strtoul(row[0], NULL, 10) : 0;
	out->s_blocksize = row[1] ? (uint32_t)strtoul(row[1], NULL, 10) : 0;
	out->s_blocksize_bits = row[2] ? (uint32_t)strtoul(row[2], NULL, 10) : 0;
	out->s_blocks_count = row[3] ? strtoull(row[3], NULL, 10) : 0;
	out->s_free_blocks = row[4] ? strtoull(row[4], NULL, 10) : 0;
	out->s_inodes_count = row[5] ? strtoull(row[5], NULL, 10) : 0;
	out->s_free_inodes = row[6] ? strtoull(row[6], NULL, 10) : 0;
	out->s_maxbytes = row[7] ? strtoull(row[7], NULL, 10) : 0;
	out->s_feature_compat = row[8] ? (uint32_t)strtoul(row[8], NULL, 10) : 0;
	out->s_feature_ro_compat = row[9] ? (uint32_t)strtoul(row[9], NULL, 10) : 0;
	out->s_feature_incompat = row[10] ? (uint32_t)strtoul(row[10], NULL, 10) : 0;
	out->s_rev_level = row[12] ? (uint32_t)strtoul(row[12], NULL, 10) : 0;
	out->s_wtime = row[13] ? (uint32_t)strtoul(row[13], NULL, 10) : 0;
	out->s_flags = row[14] ? (uint32_t)strtoul(row[14], NULL, 10) : 0;

	if (!row[11] || !hex_to_bytes(row[11], lengths[11], out->s_uuid,
				     sizeof(out->s_uuid)))
		goto out;
	if (row[15] && lengths[15] > 0) {
		if (!hex_to_bytes(row[15], lengths[15],
				 (uint8_t *)out->s_volume_name,
				 sizeof(out->s_volume_name)))
			goto out;
	} else {
		memset(out->s_volume_name, 0, sizeof(out->s_volume_name));
	}

	ok = true;

out:
	mysql_free_result(res);
	sqldb.last_query = NULL;
	return ok;
}

bool hifs_volume_super_set(uint64_t volume_id, const struct hifs_volume_superblock *vsb)
{
	char sql_query[MAX_QUERY_SIZE];
	char uuid_hex[sizeof(vsb->s_uuid) * 2 + 1];
	char name_hex[sizeof(vsb->s_volume_name) * 2 + 1];

	if (!sqldb.sql_init || !sqldb.conn || !vsb)
		return false;

	bytes_to_hex(vsb->s_uuid, sizeof(vsb->s_uuid), uuid_hex);
	bytes_to_hex((const uint8_t *)vsb->s_volume_name,
		     sizeof(vsb->s_volume_name), name_hex);

	snprintf(sql_query, sizeof(sql_query), SQL_VOLUME_SUPER_UPSERT,
		 (unsigned long long)volume_id,
		 vsb->s_magic, vsb->s_blocksize, vsb->s_blocksize_bits,
		 (unsigned long long)vsb->s_blocks_count,
		 (unsigned long long)vsb->s_free_blocks,
		 (unsigned long long)vsb->s_inodes_count,
		 (unsigned long long)vsb->s_free_inodes,
		 (unsigned long long)vsb->s_maxbytes,
		 vsb->s_feature_compat, vsb->s_feature_ro_compat, vsb->s_feature_incompat,
		 uuid_hex, vsb->s_rev_level, vsb->s_wtime, vsb->s_flags, name_hex);

	return hifs_insert_sql(sql_query);
}

bool hifs_root_dentry_load(uint64_t volume_id, struct hifs_volume_root_dentry *out)
{
	char sql_query[MAX_QUERY_SIZE];
	MYSQL_RES *res;
	MYSQL_ROW row;
	unsigned long *lengths;
	bool ok = false;

	if (!sqldb.sql_init || !sqldb.conn || !out)
		return false;

	snprintf(sql_query, sizeof(sql_query), SQL_ROOT_DENTRY_SELECT,
		 (unsigned long long)volume_id);

	res = hifs_execute_sql(sql_query);
	if (!res)
		return false;

	if (mysql_num_rows(res) == 0)
		goto out;

	row = mysql_fetch_row(res);
	lengths = mysql_fetch_lengths(res);
	if (!row || !lengths)
		goto out;

	memset(out, 0, sizeof(*out));
	out->rd_inode = row[0] ? strtoull(row[0], NULL, 10) : 0;
	out->rd_mode = row[1] ? (uint32_t)strtoul(row[1], NULL, 10) : 0;
	out->rd_uid = row[2] ? (uint32_t)strtoul(row[2], NULL, 10) : 0;
	out->rd_gid = row[3] ? (uint32_t)strtoul(row[3], NULL, 10) : 0;
	out->rd_flags = row[4] ? (uint32_t)strtoul(row[4], NULL, 10) : 0;
	out->rd_size = row[5] ? strtoull(row[5], NULL, 10) : 0;
	out->rd_blocks = row[6] ? strtoull(row[6], NULL, 10) : 0;
	out->rd_atime = row[7] ? (uint32_t)strtoul(row[7], NULL, 10) : 0;
	out->rd_mtime = row[8] ? (uint32_t)strtoul(row[8], NULL, 10) : 0;
	out->rd_ctime = row[9] ? (uint32_t)strtoul(row[9], NULL, 10) : 0;
	out->rd_links = row[10] ? (uint32_t)strtoul(row[10], NULL, 10) : 0;
	out->rd_name_len = row[11] ? (uint32_t)strtoul(row[11], NULL, 10) : 0;

	if (row[12] && lengths[12] > 0) {
		if (!hex_to_bytes(row[12], lengths[12],
				  (uint8_t *)out->rd_name,
				  sizeof(out->rd_name)))
			goto out;
	} else {
		memset(out->rd_name, 0, sizeof(out->rd_name));
	}

	ok = true;

out:
	mysql_free_result(res);
	sqldb.last_query = NULL;
	return ok;
}

bool hifs_root_dentry_store(uint64_t volume_id, const struct hifs_volume_root_dentry *root)
{
	char sql_query[MAX_QUERY_SIZE];
	char name_hex[HIFS_MAX_NAME_SIZE * 2 + 1];
	uint32_t name_len;

	if (!sqldb.sql_init || !sqldb.conn || !root)
		return false;

	name_len = root->rd_name_len;
	if (name_len > HIFS_MAX_NAME_SIZE)
		name_len = HIFS_MAX_NAME_SIZE;
	bytes_to_hex((const uint8_t *)root->rd_name, name_len, name_hex);

	snprintf(sql_query, sizeof(sql_query), SQL_ROOT_DENTRY_UPSERT,
		 (unsigned long long)volume_id,
		 (unsigned long long)root->rd_inode,
		 root->rd_mode, root->rd_uid, root->rd_gid, root->rd_flags,
		 (unsigned long long)root->rd_size,
		 (unsigned long long)root->rd_blocks,
		 root->rd_atime, root->rd_mtime, root->rd_ctime,
		 root->rd_links, root->rd_name_len, name_hex);

	return hifs_insert_sql(sql_query);
}

bool hifs_volume_dentry_load_by_inode(uint64_t volume_id, uint64_t inode,
				 struct hifs_volume_dentry *out)
{
	char sql_query[MAX_QUERY_SIZE];
	MYSQL_RES *res;
	MYSQL_ROW row;
	unsigned long *lengths;
	bool ok = false;

	if (!sqldb.sql_init || !sqldb.conn || !out)
		return false;

	snprintf(sql_query, sizeof(sql_query), SQL_DENTRY_BY_INODE,
		 (unsigned long long)volume_id, (unsigned long long)inode);

	res = hifs_execute_sql(sql_query);
	if (!res)
		return false;

	if (mysql_num_rows(res) == 0)
		goto out;

	row = mysql_fetch_row(res);
	lengths = mysql_fetch_lengths(res);
	if (!row || !lengths)
		goto out;

	memset(out, 0, sizeof(*out));
	out->de_parent = row[0] ? strtoull(row[0], NULL, 10) : 0;
	out->de_inode = row[1] ? strtoull(row[1], NULL, 10) : 0;
	out->de_epoch = row[2] ? (uint32_t)strtoul(row[2], NULL, 10) : 0;
	out->de_type = row[3] ? (uint32_t)strtoul(row[3], NULL, 10) : 0;
	out->de_name_len = row[4] ? (uint32_t)strtoul(row[4], NULL, 10) : 0;

	if (row[5] && lengths[5] > 0) {
		if (!hex_to_bytes(row[5], lengths[5],
				 (uint8_t *)out->de_name,
				 sizeof(out->de_name)))
			goto out;
	} else {
		memset(out->de_name, 0, sizeof(out->de_name));
	}

	ok = true;

out:
	mysql_free_result(res);
	sqldb.last_query = NULL;
	return ok;
}

bool hifs_volume_dentry_load_by_name(uint64_t volume_id, uint64_t parent,
				 const char *name_hex, uint32_t name_hex_len,
				 struct hifs_volume_dentry *out)
{
	char sql_query[MAX_QUERY_SIZE];
	MYSQL_RES *res;
	MYSQL_ROW row;
	unsigned long *lengths;
	bool ok = false;

	if (!sqldb.sql_init || !sqldb.conn || !out || !name_hex)
		return false;

	snprintf(sql_query, sizeof(sql_query), SQL_DENTRY_BY_NAME,
		 (unsigned long long)volume_id,
		 (unsigned long long)parent,
		 (int)name_hex_len,
		 name_hex);

	res = hifs_execute_sql(sql_query);
	if (!res)
		return false;

	if (mysql_num_rows(res) == 0)
		goto out;

	row = mysql_fetch_row(res);
	lengths = mysql_fetch_lengths(res);
	if (!row || !lengths)
		goto out;

	memset(out, 0, sizeof(*out));
	out->de_parent = row[0] ? strtoull(row[0], NULL, 10) : 0;
	out->de_inode = row[1] ? strtoull(row[1], NULL, 10) : 0;
	out->de_epoch = row[2] ? (uint32_t)strtoul(row[2], NULL, 10) : 0;
	out->de_type = row[3] ? (uint32_t)strtoul(row[3], NULL, 10) : 0;
	out->de_name_len = row[4] ? (uint32_t)strtoul(row[4], NULL, 10) : 0;
	if (row[5] && lengths[5] > 0) {
		if (!hex_to_bytes(row[5], lengths[5],
				 (uint8_t *)out->de_name,
				 sizeof(out->de_name)))
			goto out;
	} else {
		memset(out->de_name, 0, sizeof(out->de_name));
	}

	ok = true;

out:
	mysql_free_result(res);
	sqldb.last_query = NULL;
	return ok;
}

bool hifs_volume_dentry_store(uint64_t volume_id,
				 const struct hifs_volume_dentry *dent)
{
	char sql_query[MAX_QUERY_SIZE];
	char delete_query[MAX_QUERY_SIZE];
	char name_hex[HIFS_MAX_NAME_SIZE * 2 + 1];
	uint32_t name_len;

	if (!sqldb.sql_init || !sqldb.conn || !dent)
		return false;

	name_len = dent->de_name_len;
	if (name_len > HIFS_MAX_NAME_SIZE)
		name_len = HIFS_MAX_NAME_SIZE;
	bytes_to_hex((const uint8_t *)dent->de_name, name_len, name_hex);

	hifs_debug("volume_dentry_store: vol=%llu parent=%llu inode=%llu epoch=%u "
		   "type=%u name_len=%u name_hex=%s",
		   (unsigned long long)volume_id,
		   (unsigned long long)dent->de_parent,
		   (unsigned long long)dent->de_inode,
		   dent->de_epoch,
		   dent->de_type,
		   dent->de_name_len,
		   name_hex);

	snprintf(delete_query, sizeof(delete_query), SQL_DENTRY_DELETE_BY_NAME,
		 (unsigned long long)volume_id,
		 (unsigned long long)dent->de_parent,
		 (int)(name_len * 2),
		 name_hex);
	hifs_insert_sql(delete_query);

	snprintf(sql_query, sizeof(sql_query), SQL_DENTRY_UPSERT,
		 (unsigned long long)volume_id,
		 (unsigned long long)dent->de_parent,
		 (unsigned long long)dent->de_inode,
		 dent->de_epoch,
		 dent->de_type,
		 dent->de_name_len,
		 name_hex);

	bool ok = hifs_insert_sql(sql_query);
	hifs_debug("volume_dentry_store: metadata write %s (affected=%llu last_id=%llu)",
		   ok ? "succeeded" : "failed",
		   (unsigned long long)sqldb.last_affected,
		   (unsigned long long)sqldb.last_insert_id);
	return ok;
}

bool hifs_volume_inode_load(uint64_t volume_id, uint64_t inode,
				 struct hifs_inode_wire *out)
{
	char sql_query[MAX_QUERY_SIZE];
	MYSQL_RES *res = NULL;
	MYSQL_ROW row;
	unsigned long *lengths;
	bool ok = false;
	int idx = 0;

	if (!sqldb.sql_init || !sqldb.conn || !out)
		return false;

	snprintf(sql_query, sizeof(sql_query), SQL_VOLUME_INODE_SELECT,
		 (unsigned long long)volume_id,
		 (unsigned long long)inode);

	res = hifs_execute_sql(sql_query);
	if (!res)
		return false;

	if (mysql_num_rows(res) == 0)
		goto out;

	row = mysql_fetch_row(res);
	lengths = mysql_fetch_lengths(res);
	if (!row || !lengths)
		goto out;

	memset(out, 0, sizeof(*out));

	out->i_msg_flags = htole32(sql_to_u32(row[idx++]));
	out->i_version = row[idx] ? (uint8_t)strtoul(row[idx], NULL, 10) : 0;
	idx++;
	out->i_flags = row[idx] ? (uint8_t)strtoul(row[idx], NULL, 10) : 0;
	idx++;
	out->i_mode = htole32(sql_to_u32(row[idx++]));
	out->i_ino = htole64(sql_to_u64(row[idx++]));
	out->i_uid = htole16((uint16_t)sql_to_u32(row[idx++]));
	out->i_gid = htole16((uint16_t)sql_to_u32(row[idx++]));
	out->i_hrd_lnk = htole16((uint16_t)sql_to_u32(row[idx++]));
	out->i_atime = htole32(sql_to_u32(row[idx++]));
	out->i_mtime = htole32(sql_to_u32(row[idx++]));
	out->i_ctime = htole32(sql_to_u32(row[idx++]));
	out->i_size = htole32(sql_to_u32(row[idx++]));

	if (!row[idx] || !hex_to_bytes(row[idx], lengths[idx],
				       out->i_name, sizeof(out->i_name)))
		goto out;
	idx++;

	for (size_t i = 0; i < HIFS_INODE_TSIZE; ++i)
		out->extents[i].block_start = htole32(sql_to_u32(row[idx++]));
	for (size_t i = 0; i < HIFS_INODE_TSIZE; ++i)
		out->extents[i].block_count = htole32(sql_to_u32(row[idx++]));
	out->i_blocks = htole32(sql_to_u32(row[idx++]));
	out->i_bytes = htole32(sql_to_u32(row[idx++]));
	out->i_links = row[idx] ? (uint8_t)strtoul(row[idx], NULL, 10) : 0;
	idx++;
	out->i_hash_count = htole16((uint16_t)sql_to_u32(row[idx++]));
	out->i_hash_reserved = htole16((uint16_t)sql_to_u32(row[idx++]));

	/* Load fingerprints from RocksDB */
	uint16_t hash_count_host = le16toh(out->i_hash_count);
	for (uint16_t i = 0; i < hash_count_host && i < HIFS_MAX_BLOCK_HASHES; ++i) {
		struct hifs_block_fingerprint_wire fp = {0};
		if (hg_kv_get_vif_entry(volume_id, inode, i, &fp) == 0) {
			out->i_block_fingerprints[i] = fp;
		} else {
			memset(&out->i_block_fingerprints[i], 0,
			       sizeof(out->i_block_fingerprints[i]));
		}
	}

	ok = true;

out:
	if (res) {
		mysql_free_result(res);
		sqldb.last_query = NULL;
	}
	return ok;
}

bool hifs_volume_inode_store(uint64_t volume_id,
			 const struct hifs_inode_wire *inode)
{
	char *sql_query = NULL;
	char name_hex[HIFS_MAX_NAME_SIZE * 2 + 1];
	uint32_t epoch;
	uint64_t ino_host;
	uint32_t msg_flags, mode, atime, mtime, ctime, size;
	uint32_t extent_start[HIFS_INODE_TSIZE];
	uint32_t extent_count[HIFS_INODE_TSIZE];
	uint32_t blocks, bytes;
	uint16_t uid, gid, hrd_lnk, hash_count, hash_reserved;
	uint8_t version, flags, links;

	if (!sqldb.sql_init || !sqldb.conn || !inode)
		return false;

	msg_flags = le32toh(inode->i_msg_flags);
	version = inode->i_version;
	flags = inode->i_flags;
	mode = le32toh(inode->i_mode);
	ino_host = le64toh(inode->i_ino);
	uid = le16toh(inode->i_uid);
	gid = le16toh(inode->i_gid);
	hrd_lnk = le16toh(inode->i_hrd_lnk);
	atime = le32toh(inode->i_atime);
	mtime = le32toh(inode->i_mtime);
	ctime = le32toh(inode->i_ctime);
	size = le32toh(inode->i_size);
	bytes_to_hex((const uint8_t *)inode->i_name, sizeof(inode->i_name), name_hex);
	for (size_t i = 0; i < HIFS_INODE_TSIZE; ++i) {
		extent_start[i] = le32toh(inode->extents[i].block_start);
		extent_count[i] = le32toh(inode->extents[i].block_count);
	}
	blocks = le32toh(inode->i_blocks);
	bytes = le32toh(inode->i_bytes);
	links = inode->i_links;
	hash_count = le16toh(inode->i_hash_count);
	hash_reserved = le16toh(inode->i_hash_reserved);
	epoch = ctime;

	hifs_debug("volume_inode_store: vol=%llu ino=%llu mode=%#x uid=%u gid=%u "
		   "size=%u blocks=%u bytes=%u links=%u hash_count=%u epoch=%u "
		   "name_hex=%s extent_start=[%u,%u,%u,%u] extent_count=[%u,%u,%u,%u]",
		   (unsigned long long)volume_id,
		   (unsigned long long)ino_host,
		   mode,
		   uid,
		   gid,
		   size,
		   blocks,
		   bytes,
		   links,
		   hash_count,
		   epoch,
		   name_hex,
		   extent_start[0], extent_start[1], extent_start[2], extent_start[3],
		   extent_count[0], extent_count[1], extent_count[2], extent_count[3]);

	size_t sql_len = snprintf(NULL, 0, SQL_VOLUME_INODE_UPSERT,
				  (unsigned long long)volume_id,
				  (unsigned long long)ino_host,
				  msg_flags, version, flags, mode,
				  (unsigned long long)ino_host, uid, gid,
				  hrd_lnk, atime, mtime, ctime, size, name_hex,
				  extent_start[0], extent_start[1], extent_start[2], extent_start[3],
				  extent_count[0], extent_count[1], extent_count[2], extent_count[3],
				  blocks, bytes, links, hash_count, hash_reserved, epoch) + 1;
	sql_query = malloc(sql_len);
	if (!sql_query)
		return false;

	snprintf(sql_query, sql_len, SQL_VOLUME_INODE_UPSERT,
		 (unsigned long long)volume_id,
		 (unsigned long long)ino_host,
		 msg_flags, version, flags, mode,
		 (unsigned long long)ino_host, uid, gid,
		 hrd_lnk, atime, mtime, ctime, size, name_hex,
		 extent_start[0], extent_start[1], extent_start[2], extent_start[3],
		 extent_count[0], extent_count[1], extent_count[2], extent_count[3],
		 blocks, bytes, links, hash_count, hash_reserved, epoch);

	bool insert_ok = hifs_insert_sql(sql_query);
	hifs_debug("volume_inode_store: metadata write %s (affected=%llu last_id=%llu)",
		   insert_ok ? "succeeded" : "failed",
		   (unsigned long long)sqldb.last_affected,
		   (unsigned long long)sqldb.last_insert_id);
	free(sql_query);
	if (!insert_ok)
		return false;

	bool fp_ok = hifs_volume_inode_fp_sync(volume_id,
					       ino_host,
					       inode->i_block_fingerprints,
					       hash_count);
	hifs_debug("volume_inode_store: fingerprint sync %s (hash_count=%u)",
		   fp_ok ? "succeeded" : "failed",
		   hash_count);
	if (!fp_ok)
		return false;

	return true;
}

static size_t count_present_chunks(uint8_t **chunks, size_t total)
{
	size_t present = 0;
	if (!chunks)
		return 0;
	for (size_t i = 0; i < total; ++i) {
		if (chunks[i])
			++present;
	}
	return present;
}

static const struct hive_storage_node *find_storage_node(uint32_t node_id)
{
	size_t count = 0;
	const struct hive_storage_node *nodes = hifs_get_storage_nodes(&count);
	if (!nodes)
		return NULL;
	for (size_t i = 0; i < count; ++i) {
		if (nodes[i].id == node_id)
			return &nodes[i];
	}
	return NULL;
}

static bool resolve_node_endpoint(const struct hive_storage_node *node,
					 char *host,
					 size_t host_len,
					 uint16_t *port_out)
{
	if (!node || !host || host_len == 0 || !port_out)
		return false;

	const char *addr = node->address;
	const char *host_start = addr;
	size_t copy_len = 0;
	const char *port_str = NULL;

	if (addr && *addr) {
		if (addr[0] == '[') {
			const char *end = strchr(addr, ']');
			if (end && end > addr + 1) {
				host_start = addr + 1;
				copy_len = (size_t)(end - host_start);
				if (*(end + 1) == ':')
					port_str = end + 2;
			}
		}
		if (!port_str) {
			const char *colon = strrchr(addr, ':');
			if (colon && strchr(colon + 1, ':') == NULL) {
				host_start = addr;
				copy_len = (size_t)(colon - addr);
				port_str = colon + 1;
			}
		}
		if (copy_len == 0)
			copy_len = strlen(host_start);
	} else {
		host_start = "127.0.0.1";
		copy_len = strlen(host_start);
	}

	if (copy_len >= host_len)
		copy_len = host_len - 1;
	memcpy(host, host_start, copy_len);
	host[copy_len] = '\0';
	if (!*host)
		snprintf(host, host_len, "%s", "127.0.0.1");

	uint16_t port = node->stripe_port;
	if (port_str) {
		char *endptr = NULL;
		unsigned long val = strtoul(port_str, &endptr, 10);
		if (endptr && *endptr == '\0' && val > 0 && val <= UINT16_MAX)
			port = (uint16_t)val;
	}
	if (!port)
		port = HIFS_STRIPE_TCP_DEFAULT_PORT;
	*port_out = port;
	return true;
}

static bool fetch_stripe_via_tcp(const struct stripe_location *loc,
				   uint8_t **out_data,
				   size_t *out_len)
{
	if (!loc || !out_data || !out_len)
		return false;
	if (loc->storage_node_id == 0 || loc->estripe_id == 0)
		return false;

	const struct hive_storage_node *node = find_storage_node(loc->storage_node_id);
	if (!node) {
		hifs_warning("stripe fetch: storage node %u missing for estripe %llu",
			     loc->storage_node_id,
			     (unsigned long long)loc->estripe_id);
		return false;
	}

	char host[64];
	uint16_t port = 0;
	if (!resolve_node_endpoint(node, host, sizeof(host), &port))
		return false;

	if (hifs_sn_tcp_fetch(node->id,
			        loc->shard_id,
			        host,
			        port,
			        loc->estripe_id,
			        out_data,
			        out_len) != 0) {
		hifs_warning("stripe fetch: TCP fetch failed for node %u (%s:%u) estripe %llu",
			     node->id,
			     host,
			     (unsigned)port,
			     (unsigned long long)loc->estripe_id);
		return false;
	}

	return true;
}

static bool fetch_stripe_from_node(const struct stripe_location *loc,
				     uint8_t **out_data,
				     size_t *out_len)
{
	if (!loc || !out_data || !out_len || loc->estripe_id == 0)
		return false;

	if (fetch_stripe_via_tcp(loc, out_data, out_len))
		return true;

	if (hg_kv_get_estripe_chunk(loc->estripe_id, out_data, out_len) == 0)
		return true;

	hifs_warning("stripe fetch: unable to locate estripe %llu (node %u shard %u)",
		     (unsigned long long)loc->estripe_id,
		     loc->storage_node_id,
		     loc->shard_id);
	return false;
}

static void fetch_remote_stripes(const struct stripe_locations *locs,
					   uint8_t **encoded_chunks,
					   size_t total_slots,
					   size_t *frag_size,
					   size_t min_required)
{
	if (!locs || !locs->ok || locs->count == 0 || !encoded_chunks || total_slots == 0)
		return;

	size_t have = count_present_chunks(encoded_chunks, total_slots);
	for (size_t i = 0; i < locs->count; ++i) {
		const struct stripe_location *loc = &locs->entries[i];
		if (loc->stripe_index >= total_slots)
			continue;
		if (encoded_chunks[loc->stripe_index])
			continue;

		uint8_t *chunk = NULL;
		size_t chunk_len = 0;
		if (!fetch_stripe_from_node(loc, &chunk, &chunk_len))
			continue;

		if (*frag_size == 0) {
			*frag_size = chunk_len;
		} else if (chunk_len != *frag_size) {
			hifs_warning("Remote stripe %u size mismatch: expected %zu got %zu",
				     (unsigned)loc->stripe_index,
				     *frag_size,
				     chunk_len);
			free(chunk);
			continue;
		}

		encoded_chunks[loc->stripe_index] = chunk;
		++have;
		if (min_required && have >= min_required)
			break;
	}
}

bool hifs_volume_block_load(uint64_t volume_id, uint64_t block_no,
                            uint8_t *buf, uint32_t *len)
{
    bool ok = false;
    unsigned int hash_algo = 0;
    char hash_hex[sizeof(((struct VbEntry *)0)->block_hash) * 2 + 1];
    struct VbEntry vb_entry = {0};
    struct H2SEntry h2s = {0};

    if (!sqldb.sql_init || !sqldb.conn || !buf || !len)
        return false;

    /* 0) Ensure EC is initialized */
    if (hifs_ec_ensure_init() != 0) {
        if (hicomm_erasure_coding_init() != 0) {
            hifs_warning("EC init failed in load");
            return false;
        }
    }


	    if (hg_kv_get_vb_entry(volume_id, block_no, &vb_entry) != 0) {
	        hifs_warning("No block metadata for volume=%llu block=%llu",
	                     (unsigned long long)volume_id,
                     (unsigned long long)block_no);
        return false;
    }

    hash_algo = vb_entry.hash_algo;
    if (hash_algo == HIFS_HASH_ALGO_NONE) {
        hifs_warning("Invalid hash algo for volume=%llu block=%llu",
                     (unsigned long long)volume_id,
                     (unsigned long long)block_no);
        return false;
    }
    bytes_to_hex(vb_entry.block_hash, sizeof(vb_entry.block_hash), hash_hex);

    if (hg_kv_get_h2s(hash_algo, vb_entry.block_hash, &h2s) != 0) {
        hifs_warning("Missing hash->stripe mapping for volume=%llu block=%llu",
                     (unsigned long long)volume_id,
                     (unsigned long long)block_no);
        return false;
    }

    enum { NUM_DATA = HIFS_EC_K, NUM_PARITY = HIFS_EC_M, TOTAL = HIFS_EC_K + HIFS_EC_M };
	    uint64_t stripe_ids[TOTAL];
	    size_t h2s_count = sizeof(h2s.estripe_ids) / sizeof(h2s.estripe_ids[0]);
	    size_t copy = (TOTAL < h2s_count) ? TOTAL : h2s_count;
	    memcpy(stripe_ids, h2s.estripe_ids, copy * sizeof(uint64_t));
	    for (size_t i = copy; i < TOTAL; ++i)
	        stripe_ids[i] = 0;

	    uint8_t *encoded_chunks[TOTAL] = {0};
	    size_t frag_size = 0;
	    struct stripe_locations stripe_locs =
	        hifs_read_block_to_stripe_locations(volume_id, block_no,
	                                            vb_entry.block_hash, TOTAL);
	    fetch_remote_stripes(&stripe_locs, encoded_chunks, TOTAL, &frag_size, NUM_DATA);
	    size_t found = count_present_chunks(encoded_chunks, TOTAL);

	    for (size_t i = 0; i < NUM_DATA && found < NUM_DATA; ++i) {
	        if (stripe_ids[i] == 0 || encoded_chunks[i])
	            continue;
	        uint8_t *chunk = NULL;
	        size_t chunk_len = 0;
        if (hg_kv_get_estripe_chunk(stripe_ids[i], &chunk, &chunk_len) != 0)
            continue;
        if (frag_size == 0) {
            frag_size = chunk_len;
        } else if (chunk_len != frag_size) {
            hifs_warning("Fragment size mismatch: expected %zu, got %zu", frag_size, chunk_len);
            free(chunk);
            continue;
        }
        encoded_chunks[i] = chunk;
        ++found;
    }

	    if (found < NUM_DATA) {
	        for (size_t i = NUM_DATA; i < TOTAL && found < NUM_DATA; ++i) {
	            if (stripe_ids[i] == 0 || encoded_chunks[i])
	                continue;
	            uint8_t *chunk = NULL;
	            size_t chunk_len = 0;
            if (hg_kv_get_estripe_chunk(stripe_ids[i], &chunk, &chunk_len) != 0)
                continue;
            if (frag_size == 0) {
                frag_size = chunk_len;
            } else if (chunk_len != frag_size) {
                hifs_warning("Fragment size mismatch: expected %zu, got %zu", frag_size, chunk_len);
                free(chunk);
                continue;
            }
            encoded_chunks[i] = chunk;
            ++found;
        }
    }

    if (frag_size == 0) {
        hifs_warning("No EC fragments found for hash %s", hash_hex);
        goto fail_chunks;
    }

    /* 5) Decode */
    {
        size_t out_len = (size_t)*len;  /* capacity in, size out */
        int rc;

        size_t have_total = 0;
        for (size_t i = 0; i < TOTAL; ++i) if (encoded_chunks[i]) ++have_total;

        if (have_total == TOTAL) {
            rc = hicomm_erasure_coding_decode(encoded_chunks, frag_size,
                                              NUM_DATA, NUM_PARITY,
                                              buf, &out_len);
        } else if (have_total >= NUM_DATA) {
            rc = hicomm_erasure_coding_rebuild_from_partial(encoded_chunks, frag_size,
                                                            NUM_DATA, NUM_PARITY,
                                                            buf, &out_len);
        } else {
            hifs_warning("Not enough fragments for decode: have %zu, need %u",
                         have_total, (unsigned)NUM_DATA);
            goto fail_chunks;
        }

        if (rc == -2) {
            *len = (uint32_t)out_len; /* tell caller required size */
            hifs_warning("Insufficient buffer: need %zu bytes", out_len);
            goto fail_chunks;
        }
        if (rc != 0) {
            hifs_warning("EC decode failed (rc=%d) for %s", rc, hash_hex);
            goto fail_chunks;
        }

        *len = (uint32_t)out_len;
        ok = true;
    }

    /* 6) Cleanup */
fail_chunks:
    for (size_t i = 0; i < TOTAL; ++i) {
        free(encoded_chunks[i]);
        encoded_chunks[i] = NULL;
    }

    return ok;
}

static void __attribute__((unused))
hash_bytes_to_hex(const uint8_t *src, size_t len, char *dst)
{
	bytes_to_hex(src, len, dst);
}

/*
int save_binary_data(char *data_block, char *hash)
{
	(void)data_block;
	(void)hash;
	return 0;
}
*/

bool hifs_store_block_to_stripe_locations(uint64_t volume_id, uint64_t block_no,
                                          uint8_t hash_algo, const uint8_t hash[HIFS_BLOCK_HASH_SIZE],
                                          const struct stripe_location *locations, size_t count)
{
	char sql_query[MAX_QUERY_SIZE];
	char hash_hex[HIFS_BLOCK_HASH_SIZE * 2 + 1];
	size_t used = 0;

	if (!sqldb.sql_init || !sqldb.conn || !locations || count == 0)
		return false;

	bytes_to_hex(hash, HIFS_BLOCK_HASH_SIZE, hash_hex);

	hifs_debug("store_block_to_stripe_locations: volume=%llu block=%llu hash_algo=%u hash=%s count=%zu",
		   (unsigned long long)volume_id,
		   (unsigned long long)block_no,
		   hash_algo,
		   hash_hex,
		   count);
	for (size_t i = 0; i < count; ++i) {
		const struct stripe_location *loc = &locations[i];
		hifs_debug("store_block_to_stripe_locations: stripe[%zu]=idx=%u node=%u shard=%u estripe=%llu offset=%llu",
			   i,
			   loc->stripe_index,
			   loc->storage_node_id,
			   loc->shard_id,
			   (unsigned long long)loc->estripe_id,
			   (unsigned long long)loc->block_offset);
	}

	used += snprintf(sql_query + used, sizeof(sql_query) - used,
			 "INSERT INTO block_stripe_locations "
			 "(volume_id, block_no, hash_algo, block_hash, stripe_index, "
			 "storage_node_id, shard_id, estripe_id, block_offset, ref_count) VALUES ");

	for (size_t i = 0; i < count; ++i) {
		const struct stripe_location *loc = &locations[i];
		used += snprintf(sql_query + used, sizeof(sql_query) - used,
				 "(%llu,%llu,%u,UNHEX('%s'),%u,%u,%u,%llu,%llu,1)%s",
				 (unsigned long long)volume_id,
				 (unsigned long long)block_no,
				 (unsigned)hash_algo,
				 hash_hex,
				 (unsigned)loc->stripe_index,
				 loc->storage_node_id,
				 loc->shard_id,
				 (unsigned long long)loc->estripe_id,
				 (unsigned long long)loc->block_offset,
				 (i + 1 == count) ? "" : ",");
		if (used >= sizeof(sql_query))
			return false;
	}

	used += snprintf(sql_query + used, sizeof(sql_query) - used,
			 " ON DUPLICATE KEY UPDATE "
			 "storage_node_id=VALUES(storage_node_id), "
			 "shard_id=VALUES(shard_id), "
			 "estripe_id=VALUES(estripe_id), "
			 "block_offset=VALUES(block_offset), "
			 "ref_count=ref_count+1");

	bool ok = hifs_insert_sql(sql_query);
	hifs_debug("store_block_to_stripe_locations: metadata write %s (affected=%llu last_id=%llu)",
		   ok ? "succeeded" : "failed",
		   (unsigned long long)sqldb.last_affected,
		   (unsigned long long)sqldb.last_insert_id);
	return ok;
}

int hg_sql_snapshot_take(MYSQL *db, uint64_t snap_id, uint64_t snap_index)
{
	if (!db) {
		hifs_warning("hg_sql_snapshot_take: missing database handle");
		return -EINVAL;
	}

	if (mysql_query(db, SQL_RAFT_SNAPSHOT_TABLE_DDL) != 0) {
		hifs_warning("hg_sql_snapshot_take: unable to ensure snapshot table: %s",
			     mysql_error(db));
		return -EIO;
	}

	if (mysql_query(db, SQL_RAFT_SNAPSHOT_ISOLATION_REPEATABLE_READ) != 0) {
		hifs_warning("hg_sql_snapshot_take: failed to set isolation level: %s",
			     mysql_error(db));
		return -EIO;
	}

	if (mysql_query(db, SQL_RAFT_SNAPSHOT_START_CONSISTENT) != 0) {
		hifs_warning("hg_sql_snapshot_take: failed to start snapshot transaction: %s",
			     mysql_error(db));
		return -EIO;
	}

	char insert_sql[256];
	(void)snprintf(insert_sql,
		       sizeof(insert_sql),
		       SQL_RAFT_SNAPSHOT_INSERT_OR_UPDATE,
		       (unsigned long long)snap_id,
		       (unsigned long long)snap_index);

	if (mysql_query(db, insert_sql) != 0) {
		hifs_warning("hg_sql_snapshot_take: failed to persist snapshot marker: %s",
			     mysql_error(db));
		mysql_rollback(db);
		return -EIO;
	}

	if (mysql_commit(db) != 0) {
		hifs_warning("hg_sql_snapshot_take: commit failed: %s", mysql_error(db));
		mysql_rollback(db);
		return -EIO;
	}

	hifs_debug("hg_sql_snapshot_take: snap_id=%llu index=%llu",
		   (unsigned long long)snap_id,
		   (unsigned long long)snap_index);
	return 0;
}

static struct stripe_locations hifs_read_block_to_stripe_locations(uint64_t volume_id, uint64_t block_no,
						      const uint8_t hash[HIFS_BLOCK_HASH_SIZE],
						      size_t count)
{
	struct stripe_locations out = {0};
	char sql_query[MAX_QUERY_SIZE];
	char hash_hex[HIFS_BLOCK_HASH_SIZE * 2 + 1];
	size_t limit = count;

	if (!sqldb.sql_init || !sqldb.conn || !hash || count == 0)
		return out;
	if (limit > ARRAY_SIZE(out.entries))
		limit = ARRAY_SIZE(out.entries);

	bytes_to_hex(hash, HIFS_BLOCK_HASH_SIZE, hash_hex);
	int written = snprintf(sql_query, sizeof(sql_query),
			       "SELECT stripe_index, storage_node_id, shard_id, estripe_id, block_offset "
			       "FROM block_stripe_locations "
			       "WHERE volume_id=%llu AND block_no=%llu AND block_hash=UNHEX('%s') "
			       "ORDER BY stripe_index LIMIT %zu",
			       (unsigned long long)volume_id,
			       (unsigned long long)block_no,
			       hash_hex,
			       limit);
	if (written < 0 || (size_t)written >= sizeof(sql_query))
		return out;

	MYSQL_RES *res = hifs_execute_sql(sql_query);
	if (!res)
		return out;

	MYSQL_ROW row;
	size_t idx = 0;
	while (idx < limit && (row = mysql_fetch_row(res)) != NULL) {
		struct stripe_location *loc = &out.entries[idx];
		loc->stripe_index = (uint8_t)sql_to_u32(row[0]);
		loc->storage_node_id = sql_to_u32(row[1]);
		loc->shard_id = sql_to_u32(row[2]);
		loc->estripe_id = sql_to_u64(row[3]);
		loc->block_offset = sql_to_u64(row[4]);
		++idx;
	}

	mysql_free_result(res);
	sqldb.last_query = NULL;
	out.count = idx;
	out.ok = (idx > 0);
	if (idx == 0) {
		hifs_warning("stripe location rows missing for volume=%llu block=%llu hash=%s",
			     (unsigned long long)volume_id,
			     (unsigned long long)block_no,
			     hash_hex);
	} else if (idx < limit) {
		hifs_notice("stripe location gap for volume=%llu block=%llu hash=%s: have %zu/%zu rows",
			    (unsigned long long)volume_id,
			    (unsigned long long)block_no,
			    hash_hex,
			    idx,
			    limit);
	}
	return out;
}

static int hg_run_cmd(char *const argv[])
{
    pid_t pid = fork();
    if (pid < 0) return -errno;
    if (pid == 0) {
        execvp(argv[0], argv);
        _exit(127);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return -errno;
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) return 0;
    return -EIO;
}

/*
 * Create a consistent logical snapshot artifact using mysqldump.
 *
 * Requires a defaults file (recommended) so you do NOT put passwords on argv.
 * When going to prod, this should be placed in my.cnf and secured (0600).
 * Example defaults file (0600):
 *   [client]
 *   user=backupuser
 *   password=...
 *   host=127.0.0.1
 *   port=3306
 */
static const char *hg_snapshot_base_dir(const char *snapshot_dir)
{
    if (snapshot_dir && *snapshot_dir)
        return snapshot_dir;
    return HIVE_GUARD_SNAPSHOT_BASE_DIR;
}

static int hg_snapshot_format_dir(const char *base_dir,
                                  uint64_t snap_id,
                                  uint64_t snap_index,
                                  char *dir_buf,
                                  size_t dir_sz)
{
    int written = snprintf(dir_buf,
                           dir_sz,
                           "%s/" HIVE_GUARD_SNAPSHOT_DIR_FMT,
                           base_dir,
                           (unsigned long long)snap_id,
                           (unsigned long long)snap_index);
    if (written < 0 || (size_t)written >= dir_sz)
        return -ENAMETOOLONG;
    return 0;
}

static int hg_snapshot_format_file(const char *dir_path,
                                   uint64_t snap_id,
                                   uint64_t snap_index,
                                   char *out_path,
                                   size_t out_path_sz)
{
    int written = snprintf(out_path,
                           out_path_sz,
                           "%s/" HIVE_GUARD_SNAPSHOT_FILE_FMT,
                           dir_path,
                           (unsigned long long)snap_id,
                           (unsigned long long)snap_index);
    if (written < 0 || (size_t)written >= out_path_sz)
        return -ENAMETOOLONG;
    return 0;
}

static int hg_snapshot_ensure_dir(const char *path, mode_t mode, bool require_new)
{
    if (mkdir(path, mode) == 0)
        return 0;
    if (!require_new && errno == EEXIST)
        return 0;
    if (errno == EEXIST && require_new)
        return -EEXIST;
    return -errno;
}

int hg_sql_snapshot_create_artifact(const char *snapshot_dir,
                                   const char *mysqldump_path,
                                   const char *mysql_defaults_file,
                                   const char *db_name,
                                   uint64_t snap_id,
                                   uint64_t snap_index,
                                   char *out_path,
                                   size_t out_path_sz)
{
    if (!mysqldump_path || !mysql_defaults_file || !db_name || !out_path)
        return -EINVAL;

    const char *base_dir = hg_snapshot_base_dir(snapshot_dir);
    int rc = hg_snapshot_ensure_dir(base_dir, 0755, false);
    if (rc != 0) {
        hifs_warning("hg_sql_snapshot_create_artifact: base_dir=%s ensure failed rc=%d",
                     base_dir,
                     rc);
        return rc;
    }

    char snap_dir[PATH_MAX];
    rc = hg_snapshot_format_dir(base_dir, snap_id, snap_index, snap_dir, sizeof(snap_dir));
    if (rc != 0)
        return rc;

    rc = hg_snapshot_ensure_dir(snap_dir, 0700, true);
    if (rc != 0) {
        hifs_warning("hg_sql_snapshot_create_artifact: snap_dir=%s ensure failed rc=%d",
                     snap_dir,
                     rc);
        return rc;
    }

    char dump_path[PATH_MAX];
    rc = hg_snapshot_format_file(snap_dir, snap_id, snap_index, dump_path, sizeof(dump_path));
    if (rc != 0) {
        rmdir(snap_dir);
        return rc;
    }

    char defaults_arg[PATH_MAX + 32];
    int written = snprintf(defaults_arg, sizeof(defaults_arg),
                           "--defaults-extra-file=%s", mysql_defaults_file);
    if (written < 0 || (size_t)written >= sizeof(defaults_arg)) {
        rmdir(snap_dir);
        return -ENAMETOOLONG;
    }

    char result_arg[PATH_MAX + 32];
    written = snprintf(result_arg, sizeof(result_arg),
                       "--result-file=%s", dump_path);
    if (written < 0 || (size_t)written >= sizeof(result_arg)) {
        rmdir(snap_dir);
        return -ENAMETOOLONG;
    }

    char *argv[] = {
        (char *)mysqldump_path,
        defaults_arg,
        "--single-transaction",
        "--quick",
        "--routines",
        "--triggers",
        "--set-gtid-purged=OFF",
        result_arg,
        (char *)db_name,
        NULL
    };

    rc = hg_run_cmd(argv);
    if (rc != 0) {
        unlink(dump_path);
        rmdir(snap_dir);
        hifs_warning("hg_sql_snapshot_create_artifact: mysqldump failed rc=%d", rc);
        return rc;
    }

    hifs_notice("hg_sql_snapshot_create_artifact: snap_id=%llu index=%llu dir=%s file=%s",
                (unsigned long long)snap_id,
                (unsigned long long)snap_index,
                snap_dir,
                dump_path);

    strncpy(out_path, dump_path, out_path_sz - 1);
    out_path[out_path_sz - 1] = '\0';
    return 0;
}

int hg_sql_snapshot_restore_artifact(const char *snapshot_dir,
                                   const char *mysqldump_path,
                                   const char *mysql_defaults_file,
                                   const char *db_name,
                                   uint64_t snap_id,
                                   uint64_t snap_index,
                                   char *out_path,
                                   size_t out_path_sz)
{
    if (!mysqldump_path || !mysql_defaults_file || !db_name || !out_path)
        return -EINVAL;

    const char *base_dir = hg_snapshot_base_dir(snapshot_dir);
    int rc = hg_snapshot_ensure_dir(base_dir, 0755, false);
    if (rc != 0) {
        hifs_warning("hg_sql_snapshot_restore_artifact: base_dir=%s ensure failed rc=%d",
                     base_dir,
                     rc);
        return rc;
    }

    char snap_dir[PATH_MAX];
    rc = hg_snapshot_format_dir(base_dir, snap_id, snap_index, snap_dir, sizeof(snap_dir));
    if (rc != 0)
        return rc;

    rc = hg_snapshot_ensure_dir(snap_dir, 0700, false);
    if (rc != 0) {
        hifs_warning("hg_sql_snapshot_restore_artifact: snap_dir=%s ensure failed rc=%d",
                     snap_dir,
                     rc);
        return rc;
    }

    char dump_path[PATH_MAX];
    rc = hg_snapshot_format_file(snap_dir, snap_id, snap_index, dump_path, sizeof(dump_path));
    if (rc != 0)
        return rc;

    struct stat st;
    if (stat(dump_path, &st) != 0 || !S_ISREG(st.st_mode)) {
        rc = (errno != 0) ? -errno : -ENOENT;
        hifs_warning("hg_sql_snapshot_restore_artifact: missing file %s rc=%d",
                     dump_path,
                     rc);
        return rc;
    }

    char defaults_arg[PATH_MAX + 32];
    int written = snprintf(defaults_arg, sizeof(defaults_arg),
                           "--defaults-extra-file=%s", mysql_defaults_file);
    if (written < 0 || (size_t)written >= sizeof(defaults_arg))
        return -ENAMETOOLONG;

    char db_arg[NAME_MAX + 32];
    written = snprintf(db_arg, sizeof(db_arg),
                       "--database=%s", db_name);
    if (written < 0 || (size_t)written >= sizeof(db_arg))
        return -ENAMETOOLONG;

    char source_cmd[PATH_MAX + 16];
    written = snprintf(source_cmd, sizeof(source_cmd),
                       "SOURCE %s", dump_path);
    if (written < 0 || (size_t)written >= sizeof(source_cmd))
        return -ENAMETOOLONG;

    char *argv[] = {
        (char *)mysqldump_path,
        defaults_arg,
        db_arg,
        "-e",
        source_cmd,
        NULL
    };

    rc = hg_run_cmd(argv);
    if (rc != 0) {
        hifs_warning("hg_sql_snapshot_restore_artifact: mysql restore failed rc=%d", rc);
        return rc;
    }

    hifs_notice("hg_sql_snapshot_restore_artifact: restored snap_id=%llu index=%llu path=%s",
                (unsigned long long)snap_id,
                (unsigned long long)snap_index,
                dump_path);

    strncpy(out_path, dump_path, out_path_sz - 1);
    out_path[out_path_sz - 1] = '\0';
    return 0;
}

static void hg_sql_copy_field(char *dst, size_t dst_len, const char *src)
{
	if (!dst || dst_len == 0) {
		return;
	}
	if (!src) {
		dst[0] = '\0';
		return;
	}
	strncpy(dst, src, dst_len - 1);
	dst[dst_len - 1] = '\0';
}

static void hg_sql_normalize_timestamp(const char *src, char *dst, size_t dst_len)
{
	if (!dst || dst_len == 0) {
		return;
	}
	if (!src || !*src) {
		dst[0] = '\0';
		return;
	}

	size_t len = strnlen(src, HIVE_GUARD_TOKEN_TS_LEN);
	size_t out = 0;
	for (size_t i = 0; i < len && out + 1 < dst_len; ++i) {
		char c = src[i];
		if (c == 'T')
			c = ' ';
		else if (c == 'Z')
			break;
		dst[out++] = c;
	}
	dst[out] = '\0';
}

static void hg_sql_escape_field(MYSQL *db,
				char *dst,
				size_t dst_len,
				const char *src,
				size_t max_src_len)
{
	if (!dst || dst_len == 0) {
		return;
	}
	if (!db) {
		dst[0] = '\0';
		return;
	}
	if (!src)
		src = "";

	size_t len = strnlen(src, max_src_len);
	if (dst_len <= 1) {
		dst[0] = '\0';
		return;
	}

	size_t max_copy = (dst_len - 1) / 2;
	if (len > max_copy)
		len = max_copy;

	mysql_real_escape_string(db, dst, src, (unsigned long)len);
}

static uint32_t hg_sql_token_type_code(const char *type)
{
	if (!type || !type[0])
		return 0;

	uint32_t hash = 2166136261u;
	size_t len = strnlen(type, HIVE_GUARD_TOKEN_TYPE_LEN);
	for (size_t i = 0; i < len; ++i) {
		hash ^= (uint32_t)(unsigned char)type[i];
		hash *= 16777619u;
	}
	return hash;
}

static uint64_t
hg_sql_token_metadata_make_id(const struct hive_guard_token_metadata *meta)
{
	if (!meta)
		return 0;

	const struct {
		const char *value;
		size_t max_len;
	} fields[] = {
		{ meta->tid, sizeof(meta->tid) },
		{ meta->bootstrap_token, sizeof(meta->bootstrap_token) },
		{ meta->host_mid, sizeof(meta->host_mid) },
		{ meta->host_uuid, sizeof(meta->host_uuid) },
	};

	for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i) {
		const char *val = fields[i].value;
		if (!val || val[0] == '\0')
			continue;

		uint64_t hash = 1469598103934665603ULL;
		size_t len = strnlen(val, fields[i].max_len);
		for (size_t j = 0; j < len; ++j) {
			hash ^= (uint64_t)(unsigned char)val[j];
			hash *= 1099511628211ULL;
		}
		if (hash != 0)
			return hash;
	}
	return 0;
}

int hg_sql_update_token_entry(const struct hive_guard_token_metadata *meta)
{
	if (!meta || meta->bootstrap_token[0] == '\0')
		return -EINVAL;

	MYSQL *db = hg_sql_get_db();
	if (!db)
		return -EIO;

	const char *machine_uid = meta->host_mid[0] ? meta->host_mid : meta->host_uuid;

	char issued_norm[HIVE_GUARD_TOKEN_TS_LEN];
	char expires_norm[HIVE_GUARD_TOKEN_TS_LEN];
	char approved_norm[HIVE_GUARD_TOKEN_TS_LEN];
	hg_sql_normalize_timestamp(meta->issued_at, issued_norm, sizeof(issued_norm));
	hg_sql_normalize_timestamp(meta->expires_at, expires_norm, sizeof(expires_norm));
	hg_sql_normalize_timestamp(meta->approved_at, approved_norm, sizeof(approved_norm));

	char token_sql[HIVE_GUARD_BOOTSTRAP_TOKEN_LEN * 2 + 1];
	char type_sql[HIVE_GUARD_TOKEN_TYPE_LEN * 2 + 1];
	char machine_sql[HIVE_GUARD_UID_LEN * 2 + 1];
	char issued_sql[HIVE_GUARD_TOKEN_TS_LEN * 2 + 1];
	char expires_sql[HIVE_GUARD_TOKEN_TS_LEN * 2 + 1];
	char approved_sql[HIVE_GUARD_TOKEN_TS_LEN * 2 + 1];
	char approved_by_sql[HIVE_USER_ID_LEN * 2 + 1];

	hg_sql_escape_field(db, token_sql, sizeof(token_sql),
			    meta->bootstrap_token, sizeof(meta->bootstrap_token));
	hg_sql_escape_field(db, type_sql, sizeof(type_sql),
			    meta->token_type, sizeof(meta->token_type));
	hg_sql_escape_field(db, machine_sql, sizeof(machine_sql),
			    machine_uid, HIVE_GUARD_UID_LEN);
	hg_sql_escape_field(db, issued_sql, sizeof(issued_sql),
			    issued_norm, sizeof(issued_norm));
	hg_sql_escape_field(db, expires_sql, sizeof(expires_sql),
			    expires_norm, sizeof(expires_norm));
	hg_sql_escape_field(db, approved_sql, sizeof(approved_sql),
			    approved_norm, sizeof(approved_norm));
	hg_sql_escape_field(db, approved_by_sql, sizeof(approved_by_sql),
			    meta->approved_by, sizeof(meta->approved_by));

	char sql[1024];
	int written = snprintf(sql, sizeof(sql),
			       "INSERT INTO host_tokens "
			       "(token, t_type, machine_uid, issued_at, expires_at, approved_at, approved_by) "
			       "VALUES ('%s','%s','%s','%s','%s','%s','%s') "
			       "ON DUPLICATE KEY UPDATE "
			       "t_type=VALUES(t_type), machine_uid=VALUES(machine_uid), "
			       "issued_at=VALUES(issued_at), expires_at=VALUES(expires_at), "
			       "approved_at=VALUES(approved_at), approved_by=VALUES(approved_by), "
			       "revoked=0, expired=0, used=0",
			       token_sql,
			       type_sql,
			       machine_sql,
			       issued_sql,
			       expires_sql,
			       approved_sql,
			       approved_by_sql);
	if (written < 0 || (size_t)written >= sizeof(sql))
		return -ENOSPC;

	if (mysql_query(db, sql) != 0) {
		hifs_warning("hg_sql_update_token_entry: token upsert failed: %s",
			     mysql_error(db));
		return -EIO;
	}

	return 0;
}

int hg_sql_load_tokens(struct RaftTokenCommand **out_tokens, size_t *out_count)
{
	if (!out_tokens || !out_count)
		return -EINVAL;

	*out_tokens = NULL;
	*out_count = 0;

	const char *sql_query =
		"SELECT token, t_type, machine_uid, "
		"DATE_FORMAT(issued_at, '%Y-%m-%dT%H:%i:%sZ'), "
		"DATE_FORMAT(expires_at, '%Y-%m-%dT%H:%i:%sZ'), "
		"DATE_FORMAT(approved_at, '%Y-%m-%dT%H:%i:%sZ'), "
		"approved_by, "
		"COALESCE(UNIX_TIMESTAMP(expires_at), 0) "
		"FROM host_tokens "
		"WHERE revoked=0 AND expired=0";

	MYSQL_RES *res = hifs_execute_sql(sql_query);
	if (!res)
		return -EIO;

	my_ulonglong row_count = mysql_num_rows(res);
	if (row_count == 0) {
		mysql_free_result(res);
		return 0;
	}

	if (row_count > SIZE_MAX)
		row_count = SIZE_MAX;

	struct RaftTokenCommand *tokens =
		calloc((size_t)row_count, sizeof(*tokens));
	if (!tokens) {
		mysql_free_result(res);
		return -ENOMEM;
	}

	MYSQL_ROW row;
	size_t idx = 0;
	while ((row = mysql_fetch_row(res)) != NULL && idx < (size_t)row_count) {
		struct RaftTokenCommand *entry = &tokens[idx];
		memset(entry, 0, sizeof(*entry));

		struct hive_guard_token_metadata *meta = &entry->meta;
		meta->cluster_id = 0;
		meta->has_cluster_id = false;

		hg_sql_copy_field(meta->bootstrap_token,
				  sizeof(meta->bootstrap_token),
				  row[0]);
		hg_sql_copy_field(meta->token_type,
				  sizeof(meta->token_type),
				  row[1]);
		hg_sql_copy_field(meta->host_mid,
				  sizeof(meta->host_mid),
				  row[2]);
		hg_sql_copy_field(entry->machine_uid,
				  sizeof(entry->machine_uid),
				  row[2]);
		hg_sql_copy_field(meta->issued_at,
				  sizeof(meta->issued_at),
				  row[3]);
		hg_sql_copy_field(meta->expires_at,
				  sizeof(meta->expires_at),
				  row[4]);
		hg_sql_copy_field(meta->approved_at,
				  sizeof(meta->approved_at),
				  row[5]);
		hg_sql_copy_field(meta->approved_by,
				  sizeof(meta->approved_by),
				  row[6]);
		hg_sql_copy_field(entry->token,
				  sizeof(entry->token),
				  row[0]);

		entry->token_type = hg_sql_token_type_code(meta->token_type);
		if (row[7] && row[7][0]) {
			uint64_t secs = strtoull(row[7], NULL, 10);
			entry->expires_ns = secs * 1000000000ULL;
		} else {
			entry->expires_ns = 0;
		}

		entry->token_id = hg_sql_token_metadata_make_id(meta);
		++idx;
	}
	mysql_free_result(res);

	if (idx == 0) {
		free(tokens);
		return 0;
	}

	*out_tokens = tokens;
	*out_count = idx;
	return 0;
}
