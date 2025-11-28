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
#include <sys/utsname.h>
#include <openssl/sha.h>

#include "hive_guard.h"
#include "hive_guard_sql.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

struct SQLDB sqldb;

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
	char *os_name_q = hifs_get_quoted_value(uts.sysname);
	char *os_version_q = hifs_get_quoted_value(uts.release);
	if (!serial_q || !name_q || !os_name_q || !os_version_q) {
		free(serial_q);
		free(name_q);
		free(os_name_q);
		free(os_version_q);
		hifs_err("Out of memory while preparing host registration query");
		return 0;
	}

	snprintf(sql_query, sizeof(sql_query), SQL_HOST_UPSERT,
		 safe_str(serial_q), safe_str(name_q), hive_host_id,
		 safe_str(os_name_q), safe_str(os_version_q));

	free(serial_q);
	free(name_q);
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
	char name_hex[HIFS_MAX_NAME_SIZE * 2 + 1];
	uint32_t name_len;

	if (!sqldb.sql_init || !sqldb.conn || !dent)
		return false;

	name_len = dent->de_name_len;
	if (name_len > HIFS_MAX_NAME_SIZE)
		name_len = HIFS_MAX_NAME_SIZE;
	bytes_to_hex((const uint8_t *)dent->de_name, name_len, name_hex);

	snprintf(sql_query, sizeof(sql_query), SQL_DENTRY_UPSERT,
		 (unsigned long long)volume_id,
		 (unsigned long long)dent->de_parent,
		 (unsigned long long)dent->de_inode,
		 dent->de_epoch,
		 dent->de_type,
		 dent->de_name_len,
		 name_hex);

	return hifs_insert_sql(sql_query);
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

	out->i_addrb[0] = htole32(sql_to_u32(row[idx++]));
	out->i_addrb[1] = htole32(sql_to_u32(row[idx++]));
	out->i_addrb[2] = htole32(sql_to_u32(row[idx++]));
	out->i_addrb[3] = htole32(sql_to_u32(row[idx++]));
	out->i_addre[0] = htole32(sql_to_u32(row[idx++]));
	out->i_addre[1] = htole32(sql_to_u32(row[idx++]));
	out->i_addre[2] = htole32(sql_to_u32(row[idx++]));
	out->i_addre[3] = htole32(sql_to_u32(row[idx++]));
	out->i_blocks = htole32(sql_to_u32(row[idx++]));
	out->i_bytes = htole32(sql_to_u32(row[idx++]));
	out->i_links = row[idx] ? (uint8_t)strtoul(row[idx], NULL, 10) : 0;
	idx++;
	out->i_hash_count = htole16((uint16_t)sql_to_u32(row[idx++]));
	out->i_hash_reserved = htole16((uint16_t)sql_to_u32(row[idx++]));

	/* Load fingerprints */
	{
		MYSQL_RES *fp_res = NULL;
		MYSQL_ROW fp_row;
		unsigned long *fp_lens;

		snprintf(sql_query, sizeof(sql_query), SQL_VOLUME_INODE_FP_SELECT,
			 (unsigned long long)volume_id,
			 (unsigned long long)inode);
		fp_res = hifs_execute_sql(sql_query);
		if (fp_res) {
			while ((fp_row = mysql_fetch_row(fp_res)) != NULL) {
				fp_lens = mysql_fetch_lengths(fp_res);
				if (!fp_lens)
					break;
				unsigned int fp_idx = sql_to_u32(fp_row[0]);
				if (fp_idx >= HIFS_MAX_BLOCK_HASHES)
					continue;
				out->i_block_fingerprints[fp_idx].block_no =
					htole32(sql_to_u32(fp_row[1]));
				out->i_block_fingerprints[fp_idx].hash_algo =
					fp_row[2] ? (uint8_t)strtoul(fp_row[2], NULL, 10) : 0;
				if (!fp_row[3] ||
				    !hex_to_bytes(fp_row[3], fp_lens[3],
						  out->i_block_fingerprints[fp_idx].hash,
						  sizeof(out->i_block_fingerprints[fp_idx].hash)))
					continue;
			}
			mysql_free_result(fp_res);
			sqldb.last_query = NULL;
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
	uint32_t addrb[HIFS_INODE_TSIZE];
	uint32_t addre[HIFS_INODE_TSIZE];
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
		addrb[i] = le32toh(inode->i_addrb[i]);
		addre[i] = le32toh(inode->i_addre[i]);
	}
	blocks = le32toh(inode->i_blocks);
	bytes = le32toh(inode->i_bytes);
	links = inode->i_links;
	hash_count = le16toh(inode->i_hash_count);
	hash_reserved = le16toh(inode->i_hash_reserved);
	epoch = ctime;

	size_t sql_len = snprintf(NULL, 0, SQL_VOLUME_INODE_UPSERT,
				  (unsigned long long)volume_id,
				  (unsigned long long)ino_host,
				  msg_flags, version, flags, mode,
				  (unsigned long long)ino_host, uid, gid,
				  hrd_lnk, atime, mtime, ctime, size, name_hex,
				  addrb[0], addrb[1], addrb[2], addrb[3],
				  addre[0], addre[1], addre[2], addre[3],
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
		 addrb[0], addrb[1], addrb[2], addrb[3],
		 addre[0], addre[1], addre[2], addre[3],
		 blocks, bytes, links, hash_count, hash_reserved, epoch);

	if (!hifs_insert_sql(sql_query)) {
		free(sql_query);
		return false;
	}
	free(sql_query);

	/* Refresh fingerprints */
	char sql_tmp[MAX_QUERY_SIZE];
	snprintf(sql_tmp, sizeof(sql_tmp), SQL_VOLUME_INODE_FP_DELETE,
		 (unsigned long long)volume_id,
		 (unsigned long long)ino_host);
	if (!hifs_insert_sql(sql_tmp))
		return false;

	for (uint16_t i = 0; i < hash_count && i < HIFS_MAX_BLOCK_HASHES; ++i) {
		const struct hifs_block_fingerprint_wire *fp =
			&inode->i_block_fingerprints[i];
		uint32_t block_no = le32toh(fp->block_no);
		if (block_no == 0 && fp->hash_algo == 0)
			continue;
		char hash_hex[HIFS_BLOCK_HASH_SIZE * 2 + 1];
		bytes_to_hex(fp->hash, HIFS_BLOCK_HASH_SIZE, hash_hex);
		snprintf(sql_tmp, sizeof(sql_tmp), SQL_VOLUME_INODE_FP_REPLACE,
			 (unsigned long long)volume_id,
			 (unsigned long long)ino_host,
			 i, block_no, fp->hash_algo, hash_hex);
		if (!hifs_insert_sql(sql_tmp))
			return false;
	}

	return true;
}

bool hifs_volume_block_load(uint64_t volume_id, uint64_t block_no,
                            uint8_t *buf, uint32_t *len)
{
    char sql_query[MAX_QUERY_SIZE];
    MYSQL_RES *res = NULL;
    MYSQL_ROW row;
    unsigned long *lengths;
    bool ok = false;

    unsigned int hash_algo = 0;
    char hash_hex[SHA256_DIGEST_LENGTH * 2 + 1];

    if (!sqldb.sql_init || !sqldb.conn || !buf || !len)
        return false;

    /* 0) Ensure EC is initialized */
    if (hifs_ec_ensure_init() != 0) {
        if (hicomm_erasure_coding_init() != 0) {
            hifs_warning("EC init failed in load");
            return false;
        }
    }

    /* 1) volume_id+block_no -> (hash_algo, block_hash) */
    snprintf(sql_query, sizeof(sql_query), SQL_VOLUME_BLOCK_MAP_SELECT,
             (unsigned long long)volume_id,
             (unsigned long long)block_no);

    res = hifs_execute_sql(sql_query);
    if (!res) return false;
    if (mysql_num_rows(res) == 0) goto out;

    row = mysql_fetch_row(res);
    lengths = mysql_fetch_lengths(res);
    if (!row || !lengths) goto out;

    hash_algo = row[0] ? (unsigned int)strtoul(row[0], NULL, 10) : HIFS_HASH_ALGO_NONE;
    if (!row[1] || lengths[1] == 0) goto out;

    {
        unsigned long hex_len = lengths[1];
        if (hex_len >= sizeof(hash_hex)) hex_len = sizeof(hash_hex) - 1;
        memcpy(hash_hex, row[1], hex_len);
        hash_hex[hex_len] = '\0';
    }

    mysql_free_result(res);
    sqldb.last_query = NULL;
    res = NULL;

    /* 2) block_hash -> stripe IDs (data then parity) */
    snprintf(sql_query, sizeof(sql_query), SQL_HASH_TO_ESTRIPES_SELECT,
             hash_algo, hash_hex);

    res = hifs_execute_sql(sql_query);
    if (!res) return false;
    if (mysql_num_rows(res) == 0) goto out;

    row = mysql_fetch_row(res);
    lengths = mysql_fetch_lengths(res);
    if (!row || !lengths) goto out;

    enum { NUM_DATA = HIFS_EC_K, NUM_PARITY = HIFS_EC_M, TOTAL = HIFS_EC_K + HIFS_EC_M };
    uint64_t stripe_ids[TOTAL] = {0};

    for (size_t i = 0; i < TOTAL; ++i) {
        stripe_ids[i] = row[i] ? (uint64_t)strtoull(row[i], NULL, 10) : 0ULL;
    }

    mysql_free_result(res);
    sqldb.last_query = NULL;
    res = NULL;

    /* 3) Fetch fragments until we have at least k (prefer data first) */
    uint8_t *encoded_chunks[TOTAL] = {0};
    size_t   frag_size = 0;
    size_t   found = 0;

    for (size_t i = 0; i < TOTAL && found < NUM_DATA; ++i) {
        if (stripe_ids[i] == 0) continue; /* missing id: try others */

        snprintf(sql_query, sizeof(sql_query), SQL_ECBLOCK_SELECT,
                 (unsigned long long)stripe_ids[i]);

        res = hifs_execute_sql(sql_query);
        if (!res) goto fail_chunks;

        if (mysql_num_rows(res) == 0) {
            mysql_free_result(res);
            sqldb.last_query = NULL;
            res = NULL;
            continue; /* missing row; try others */
        }

        row = mysql_fetch_row(res);
        lengths = mysql_fetch_lengths(res);
        if (!row || !lengths) {
            mysql_free_result(res);
            sqldb.last_query = NULL;
            res = NULL;
            goto fail_chunks;
        }

        unsigned long hex_len = lengths[0];
        if (!row[0] || hex_len == 0) {
            mysql_free_result(res);
            sqldb.last_query = NULL;
            res = NULL;
            continue;
        }

        size_t this_frag = hex_len / 2; /* HEX -> bytes */
        if (frag_size == 0) {
            frag_size = this_frag;
        } else if (this_frag != frag_size) {
            hifs_warning("Fragment size mismatch: expected %zu, got %zu", frag_size, this_frag);
            mysql_free_result(res);
            sqldb.last_query = NULL;
            res = NULL;
            goto fail_chunks;
        }

        encoded_chunks[i] = (uint8_t *)malloc(frag_size);
        if (!encoded_chunks[i]) {
            hifs_warning("OOM for fragment %zu", i);
            mysql_free_result(res);
            sqldb.last_query = NULL;
            res = NULL;
            goto fail_chunks;
        }

        if (!hex_to_bytes(row[0], hex_len, encoded_chunks[i], frag_size)) {
            hifs_warning("hex_to_bytes failed for stripe %llu",
                         (unsigned long long)stripe_ids[i]);
            mysql_free_result(res);
            sqldb.last_query = NULL;
            res = NULL;
            goto fail_chunks;
        }

        ++found;

        mysql_free_result(res);
        sqldb.last_query = NULL;
        res = NULL;
    }

    if (frag_size == 0) {
        hifs_warning("No EC fragments found for hash %s", hash_hex);
        goto fail_chunks;
    }

    /* 4) If we didn’t get k yet, try the remaining IDs (parity) */
    for (size_t i = NUM_DATA; i < TOTAL && found < NUM_DATA; ++i) {
        if (encoded_chunks[i]) continue;           /* already fetched */
        if (stripe_ids[i] == 0) continue;

        snprintf(sql_query, sizeof(sql_query), SQL_ECBLOCK_SELECT,
                 (unsigned long long)stripe_ids[i]);

        res = hifs_execute_sql(sql_query);
        if (!res) goto fail_chunks;

        if (mysql_num_rows(res) == 0) { /* still missing */
            mysql_free_result(res);
            sqldb.last_query = NULL;
            res = NULL;
            continue;
        }

        row = mysql_fetch_row(res);
        lengths = mysql_fetch_lengths(res);
        if (!row || !lengths) {
            mysql_free_result(res);
            sqldb.last_query = NULL;
            res = NULL;
            goto fail_chunks;
        }

        unsigned long hex_len = lengths[0];
        if (!row[0] || hex_len == 0) {
            mysql_free_result(res);
            sqldb.last_query = NULL;
            res = NULL;
            continue;
        }

        size_t this_frag = hex_len / 2;
        if (this_frag != frag_size) {
            hifs_warning("Fragment size mismatch: expected %zu, got %zu", frag_size, this_frag);
            mysql_free_result(res);
            sqldb.last_query = NULL;
            res = NULL;
            goto fail_chunks;
        }

        encoded_chunks[i] = (uint8_t *)malloc(frag_size);
        if (!encoded_chunks[i]) {
            hifs_warning("OOM for fragment %zu", i);
            mysql_free_result(res);
            sqldb.last_query = NULL;
            res = NULL;
            goto fail_chunks;
        }

        if (!hex_to_bytes(row[0], hex_len, encoded_chunks[i], frag_size)) {
            hifs_warning("hex_to_bytes failed for stripe %llu",
                         (unsigned long long)stripe_ids[i]);
            mysql_free_result(res);
            sqldb.last_query = NULL;
            res = NULL;
            goto fail_chunks;
        }

        ++found;

        mysql_free_result(res);
        sqldb.last_query = NULL;
        res = NULL;
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

out:
    if (res) {
        mysql_free_result(res);
        sqldb.last_query = NULL;
    }
    return ok;
}

static void __attribute__((unused))
hash_bytes_to_hex(const uint8_t *src, size_t len, char *dst)
{
	bytes_to_hex(src, len, dst);
}

int save_binary_data(char *data_block, char *hash)
{
	(void)data_block;
	(void)hash;
	return 0;
}
