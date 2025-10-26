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
#include <endian.h>
#include <netdb.h>
#include <sys/utsname.h>

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

bool hifs_volume_super_get(uint64_t volume_id, struct hifs_volume_superblock *out)
{
	char sql_query[MAX_QUERY_SIZE];
	MYSQL_RES *res;
	MYSQL_ROW row;
	unsigned long *lengths;
	bool ok = false;

	if (!sqldb.sql_init || !sqldb.conn || !out)
		return false;

	snprintf(sql_query, sizeof(sql_query),
		 "SELECT s_magic, s_blocksize, s_blocksize_bits, s_blocks_count, "
		 "s_free_blocks, s_inodes_count, s_free_inodes, s_maxbytes, "
		 "s_feature_compat, s_feature_ro_compat, s_feature_incompat, "
		 "HEX(s_uuid), s_rev_level, s_wtime, s_flags, HEX(s_volume_name) "
		 "FROM volume_superblocks WHERE volume_id=%llu",
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

	snprintf(sql_query, sizeof(sql_query),
		 "INSERT INTO volume_superblocks "
		 "(volume_id, s_magic, s_blocksize, s_blocksize_bits, s_blocks_count, "
		 "s_free_blocks, s_inodes_count, s_free_inodes, s_maxbytes, "
		 "s_feature_compat, s_feature_ro_compat, s_feature_incompat, s_uuid, "
		 "s_rev_level, s_wtime, s_flags, s_volume_name) "
		 "VALUES (%llu, %u, %u, %u, %llu, %llu, %llu, %llu, %llu, %u, %u, %u, "
		 "UNHEX('%s'), %u, %u, %u, UNHEX('%s')) "
		 "ON DUPLICATE KEY UPDATE "
		 "s_magic=VALUES(s_magic), s_blocksize=VALUES(s_blocksize), "
		 "s_blocksize_bits=VALUES(s_blocksize_bits), s_blocks_count=VALUES(s_blocks_count), "
		 "s_free_blocks=VALUES(s_free_blocks), s_inodes_count=VALUES(s_inodes_count), "
		 "s_free_inodes=VALUES(s_free_inodes), s_maxbytes=VALUES(s_maxbytes), "
		 "s_feature_compat=VALUES(s_feature_compat), "
		 "s_feature_ro_compat=VALUES(s_feature_ro_compat), "
		 "s_feature_incompat=VALUES(s_feature_incompat), s_uuid=VALUES(s_uuid), "
		 "s_rev_level=VALUES(s_rev_level), s_wtime=VALUES(s_wtime), "
		 "s_flags=VALUES(s_flags), s_volume_name=VALUES(s_volume_name)",
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
	const char *machine = safe_str(sqldb.host.machine_id);
	char *escaped = NULL;
	bool ok = false;

	if (!sqldb.sql_init || !sqldb.conn || !out || !machine[0])
		return false;

	escaped = hifs_get_quoted_value(machine);
	if (!escaped)
		return false;

	snprintf(sql_query, sizeof(sql_query),
		 "SELECT rd_inode, rd_mode, rd_uid, rd_gid, rd_flags, "
		 "rd_size, rd_blocks, rd_atime, rd_mtime, rd_ctime, rd_links, "
		 "rd_name_len, HEX(rd_name) "
		 "FROM root_dentries WHERE machine_uid='%s' AND volume_id=%llu",
		 escaped, (unsigned long long)volume_id);
	free(escaped);

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
	const char *machine = safe_str(sqldb.host.machine_id);
	char *escaped = NULL;
	uint32_t name_len;

	if (!sqldb.sql_init || !sqldb.conn || !root || !machine[0])
		return false;

	escaped = hifs_get_quoted_value(machine);
	if (!escaped)
		return false;

	name_len = root->rd_name_len;
	if (name_len > HIFS_MAX_NAME_SIZE)
		name_len = HIFS_MAX_NAME_SIZE;
	bytes_to_hex((const uint8_t *)root->rd_name, name_len, name_hex);

	snprintf(sql_query, sizeof(sql_query),
		 "INSERT INTO root_dentries "
		 "(machine_uid, volume_id, rd_inode, rd_mode, rd_uid, rd_gid, rd_flags, "
		 "rd_size, rd_blocks, rd_atime, rd_mtime, rd_ctime, rd_links, rd_name_len, rd_name) "
		 "VALUES ('%s', %llu, %llu, %u, %u, %u, %u, %llu, %llu, %u, %u, %u, %u, %u, UNHEX('%s')) "
		 "ON DUPLICATE KEY UPDATE "
		 "rd_inode=VALUES(rd_inode), rd_mode=VALUES(rd_mode), rd_uid=VALUES(rd_uid), "
		 "rd_gid=VALUES(rd_gid), rd_flags=VALUES(rd_flags), rd_size=VALUES(rd_size), "
		 "rd_blocks=VALUES(rd_blocks), rd_atime=VALUES(rd_atime), rd_mtime=VALUES(rd_mtime), "
		 "rd_ctime=VALUES(rd_ctime), rd_links=VALUES(rd_links), rd_name_len=VALUES(rd_name_len), "
		 "rd_name=VALUES(rd_name)",
		 escaped,
		 (unsigned long long)volume_id,
		 (unsigned long long)root->rd_inode,
		 root->rd_mode, root->rd_uid, root->rd_gid, root->rd_flags,
		 (unsigned long long)root->rd_size,
		 (unsigned long long)root->rd_blocks,
		 root->rd_atime, root->rd_mtime, root->rd_ctime,
		 root->rd_links, root->rd_name_len, name_hex);

	free(escaped);
	return hifs_insert_sql(sql_query);
}

bool hifs_volume_dentry_load(uint64_t volume_id, uint64_t inode,
				 struct hifs_volume_dentry *out)
{
	char sql_query[MAX_QUERY_SIZE];
	MYSQL_RES *res;
	MYSQL_ROW row;
	unsigned long *lengths;
	bool ok = false;

	if (!sqldb.sql_init || !sqldb.conn || !out)
		return false;

	snprintf(sql_query, sizeof(sql_query),
		 "SELECT de_parent, de_inode, de_epoch, de_type, de_name_len, HEX(de_name) "
		 "FROM volume_dentries WHERE volume_id=%llu AND de_inode=%llu",
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

	snprintf(sql_query, sizeof(sql_query),
		 "INSERT INTO volume_dentries "
		 "(volume_id, de_inode, de_parent, de_epoch, de_type, de_name_len, de_name) "
		 "VALUES (%llu, %llu, %llu, %u, %u, %u, UNHEX('%s')) "
		 "ON DUPLICATE KEY UPDATE "
		 "de_parent=VALUES(de_parent), de_epoch=VALUES(de_epoch), "
		 "de_type=VALUES(de_type), de_name_len=VALUES(de_name_len), "
		 "de_name=VALUES(de_name)",
		 (unsigned long long)volume_id,
		 (unsigned long long)dent->de_inode,
		 (unsigned long long)dent->de_parent,
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
	MYSQL_RES *res;
	MYSQL_ROW row;
	unsigned long *lengths;
	bool ok = false;

	if (!sqldb.sql_init || !sqldb.conn || !out)
		return false;

	snprintf(sql_query, sizeof(sql_query),
		 "SELECT HEX(inode_blob) FROM volume_inodes WHERE volume_id=%llu AND inode=%llu",
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

	if (!row[0] || !hex_to_bytes(row[0], lengths[0],
				      (uint8_t *)out, sizeof(*out)))
		goto out;

	ok = true;

out:
	mysql_free_result(res);
	sqldb.last_query = NULL;
	return ok;
}

bool hifs_volume_inode_store(uint64_t volume_id,
				 const struct hifs_inode_wire *inode)
{
	char sql_query[MAX_QUERY_SIZE];
	char blob_hex[sizeof(*inode) * 2 + 1];
	uint32_t epoch;
	uint64_t ino_host;

	if (!sqldb.sql_init || !sqldb.conn || !inode)
		return false;

	bytes_to_hex((const uint8_t *)inode, sizeof(*inode), blob_hex);
	ino_host = le64toh(inode->i_ino);
	epoch = le32toh(inode->i_ctime);

	snprintf(sql_query, sizeof(sql_query),
		 "INSERT INTO volume_inodes (volume_id, inode, inode_blob, epoch) "
		 "VALUES (%llu, %llu, UNHEX('%s'), %u) "
		 "ON DUPLICATE KEY UPDATE inode_blob=VALUES(inode_blob), epoch=VALUES(epoch)",
		 (unsigned long long)volume_id,
		 (unsigned long long)ino_host,
		 blob_hex,
		 epoch);

	return hifs_insert_sql(sql_query);
}

int save_binary_data(char *data_block, char *hash)
{
	(void)data_block;
	(void)hash;
	return 0;
}
