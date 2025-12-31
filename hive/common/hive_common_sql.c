#include <arpa/inet.h>
#include <errno.h>
#include <limits.h>
#include <netdb.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "../guard/hive_guard.h"
#include "../guard/hive_guard_sql.h"
#include "hive_common_sql.h"

extern struct SQLDB sqldb;

void hifs_safe_strcpy(char *dst, size_t dst_len, const char *src)
{
	if (!dst || dst_len == 0)
		return;
	if (!src)
		src = "";
	size_t max_copy = dst_len - 1;
	size_t to_copy = strnlen(src, max_copy);
	memcpy(dst, src, to_copy);
	dst[to_copy] = '\0';
}

long hifs_get_host_id(void)
{
	long id = gethostid();

	if (id == -1) {
		char hostname[256];

		if (gethostname(hostname, sizeof(hostname)) == 0) {
			unsigned long hash = 5381;

			for (const unsigned char *p =
				     (const unsigned char *)hostname;
			     *p;
			     ++p)
				hash = ((hash << 5) + hash) + *p;
			id = (long)hash;
		} else {
			id = 0;
		}
	}
	return id;
}

const char *hifs_cached_host_serial(void)
{
	static char serial_buf[32];
	static long cached_host_id = LONG_MIN;
	long host_id = hifs_get_host_id();

	if (host_id != cached_host_id) {
		int len = snprintf(serial_buf, sizeof(serial_buf), "%ld", host_id);

		if (len < 0 || (size_t)len >= sizeof(serial_buf))
			serial_buf[0] = '\0';
		cached_host_id = host_id;
	}
	return serial_buf;
}

char *hifs_get_machine_id(void)
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

uint16_t hifs_local_guard_port(void)
{
	const char *env = getenv(ENV_LISTEN_PORT);
	long port = env && *env ? strtol(env, NULL, 10) : 0;

	if (port <= 0 || port > UINT16_MAX)
		port = atoi(HIFS_GUARD_PORT_STR);
	return (uint16_t)port;
}

void hifs_parse_version(int *version_out, int *patch_out)
{
	int version = 0;
	int patch = 0;
	const char *ver = HIFS_VERSION;

	if (ver && *ver) {
		char *endptr = NULL;
		long v = strtol(ver, &endptr, 10);

		if (v >= 0)
			version = (int)v;
		if (endptr && *endptr == '.') {
			long p = strtol(endptr + 1, NULL, 10);

			if (p >= 0)
				patch = (int)p;
		}
	}

	if (version_out)
		*version_out = version;
	if (patch_out)
		*patch_out = patch;
}

bool hifs_get_local_node_identity(struct hive_storage_node *node)
{
	if (!node)
		return false;

	memset(node, 0, sizeof(*node));

	char hostname[sizeof(node->name)] = {0};

	if (gethostname(hostname, sizeof(hostname)) != 0)
		hifs_safe_strcpy(hostname, sizeof(hostname), "hive_guard");
	hostname[sizeof(hostname) - 1] = '\0';
	hifs_safe_strcpy(node->name, sizeof(node->name), hostname);

	const char *uid = hifs_get_machine_id();

	if (uid)
		hifs_safe_strcpy(node->uid, sizeof(node->uid), uid);

	const char *serial = hifs_cached_host_serial();

	if (serial)
		hifs_safe_strcpy(node->serial, sizeof(node->serial), serial);

	bool have_addr = false;
	struct addrinfo hints = {0};
	struct addrinfo *res = NULL;

	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	if (getaddrinfo(hostname, NULL, &hints, &res) == 0) {
		for (struct addrinfo *it = res; it && !have_addr; it = it->ai_next) {
			if (it->ai_family != AF_INET)
				continue;
			struct sockaddr_in *sin = (struct sockaddr_in *)it->ai_addr;

			if (inet_ntop(AF_INET, &sin->sin_addr,
				      node->address, sizeof(node->address)))
				have_addr = true;
		}
		freeaddrinfo(res);
	}

	if (!have_addr) {
		struct hostent *he = gethostbyname(hostname);

		if (he && he->h_addr_list && he->h_addr_list[0]) {
			struct in_addr addr;

			memcpy(&addr, he->h_addr_list[0], sizeof(addr));
			const char *ip = inet_ntoa(addr);

			if (ip) {
				strncpy(node->address, ip, sizeof(node->address) - 1);
				node->address[sizeof(node->address) - 1] = '\0';
				have_addr = true;
			}
		}
	}

	if (!have_addr)
		hifs_safe_strcpy(node->address, sizeof(node->address), "127.0.0.1");

	node->guard_port = hifs_local_guard_port();
	node->stripe_port = node->guard_port;
	return true;
}

bool hifs_execute_query(const char *sql, MYSQL_RES **out_res)
{
	if (!sqldb.sql_init || !sqldb.conn) {
		hifs_err("MariaDB connection is not initialised\n");
		if (out_res)
			*out_res = NULL;
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

bool hifs_insert_sql(const char *sql_string)
{
	MYSQL_RES *res = NULL;

	return hifs_execute_query(sql_string, &res);
}

bool hifs_persist_storage_node(uint64_t node_id,
			       const char *node_address,
			       uint16_t node_port)
{
	static struct {
		uint64_t node_id;
		char address[64];
		uint16_t port;
		time_t ts;
		bool ok;
	} cache;
	char sql_query[MAX_QUERY_SIZE];

	if (!sqldb.sql_init || !sqldb.conn || node_id == 0)
		return false;

	struct hive_storage_node local = {0};

	if (!hifs_get_local_node_identity(&local))
		return false;

	if (node_address && *node_address)
		hifs_safe_strcpy(local.address, sizeof(local.address), node_address);
	if (node_port)
		local.guard_port = node_port;
	if (!local.stripe_port)
		local.stripe_port = local.guard_port;

	time_t now = time(NULL);
	if (cache.node_id == node_id &&
	    cache.port == local.guard_port &&
	    cache.address[0] != '\0' &&
	    strcmp(cache.address, local.address) == 0 &&
	    cache.ts != 0 &&
	    (now - cache.ts) < HIFS_STORAGE_NODE_PERSIST_MIN_SECS)
		return cache.ok;

	int version = 0;
	int patch = 0;

	hifs_parse_version(&version, &patch);

	int written = snprintf(sql_query, sizeof(sql_query), SQL_STORAGE_NODE_UPSERT,
			       (unsigned long long)node_id,
			       local.name[0] ? local.name : "hive_guard",
			       local.address[0] ? local.address : "127.0.0.1",
			       local.uid[0] ? local.uid : hifs_get_machine_id(),
			       local.serial[0] ? local.serial : hifs_cached_host_serial(),
			       (unsigned)local.guard_port,
			       (unsigned)local.stripe_port,
			       version,
			       patch,
			       0ULL, 0ULL, 0ULL, 0ULL);
	if (written < 0 || (size_t)written >= sizeof(sql_query))
		return false;

	bool ok = hifs_insert_sql(sql_query);
	cache.node_id = node_id;
	hifs_safe_strcpy(cache.address, sizeof(cache.address), local.address);
	cache.port = local.guard_port;
	cache.ts = now;
	cache.ok = ok;
	return ok;
}
