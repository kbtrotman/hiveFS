/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

/**
 * HiveFS - hive_guard_auth
 *
 * The keypair creator (access) and certificate (authenticator)
 * management routines for hiveFS. This also will need to include
 * user and group managmenet as they relate to tokens and certs.
 * 
 * Basically, this is client access, node access, and RBAC.
 * 
 */
#include "hive_guard_auth.h"
#include "hive_guard_sql.h"
#include "hive_guard_sock.h"
#include "../bootstrap/hive_bootstrap_sock.h"
#include "../../hifs_shared_defs.h"

#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define NODE_CERT_VALID_DAYS 365

static void ensure_pki_directories(void)
{
	const char *dirs[] = {
		HIVE_PKI_DIR,
		HIVE_NODE_CERT_DIR,
		HIVE_CERT_DIR,
		NULL
	};

	for (size_t i = 0; dirs[i]; ++i) {
		if (mkdir(dirs[i], 0700) == 0 || errno == EEXIST)
			continue;
		fprintf(stderr,
			"bootstrap: failed to create %s: %s\n",
			dirs[i],
			strerror(errno));
	}
}

char *generate_random_token(size_t length)
{
    char *buf = malloc(length + 1);
    ssize_t rc = getrandom(buf, length, 0);
    if (rc < 0) {
        free(buf);
        return NULL;
    }
    if ((size_t)rc != length) {
        free(buf);
        return NULL;
    }
    buf[length] = '\0';
    
    return buf;
}

static bool build_cert_path(char *dst, size_t dst_len,
			    const char *dir, const char *uid,
			    const char *suffix)
{
	if (!dst || dst_len == 0 || !dir || !uid || !suffix)
		return false;
	int written = snprintf(dst, dst_len, "%s/%s%s", dir, uid, suffix);

	return written >= 0 && (size_t)written < dst_len;
}

static void touch_empty_file(const char *path)
{
	if (!path || !*path)
		return;
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);

	if (fd >= 0)
		close(fd);
	else
		fprintf(stderr,
			"bootstrap: failed to create %s: %s\n",
			path,
			strerror(errno));
}

int add_node_join_token(uint32_t node_id, const char *token)
{
	(void)node_id;

	if (!token || !*token)
		return 0;
	if (!sqldb.conn || !hbc.storage_node_uid[0])
		return -1;

	char sql_query[1024];
	char token_sql[HIVE_BOOTSTRAP_MSG_MAX * 2 + 1];
	char uid_sql[sizeof(hbc.storage_node_uid) * 2 + 1];
	unsigned long token_len = (unsigned long)strlen(token);
	unsigned long uid_len =
		(unsigned long)strlen(hbc.storage_node_uid);

	if (token_len >= (sizeof(token_sql) - 1) / 2)
		return -1;

	mysql_real_escape_string(sqldb.conn,
				 token_sql,
				 token,
				 token_len);
	mysql_real_escape_string(sqldb.conn,
				 uid_sql,
				 hbc.storage_node_uid,
				 uid_len);

	int written = snprintf(sql_query, sizeof(sql_query),
			       SQL_HOST_TOKEN_INSERT_NODE_JOIN,
			       token_sql,
			       uid_sql);

	if (written < 0 || (size_t)written >= sizeof(sql_query))
		return -1;

	if (mysql_real_query(sqldb.conn, sql_query, (unsigned long)written) != 0) {
		fprintf(stderr, "bootstrap: failed to add node add token: %s\n",
			mysql_error(sqldb.conn));
		return -1;
	}

	return 0;
}

int add_node_mtls_token(const struct hive_storage_node *node)
{
	if (!node || !sqldb.conn || !node->uid[0])
		return -1;

	char key_path[PATH_MAX];
	char cert_path[PATH_MAX];
	char csr_path[PATH_MAX];
	char *priv_sql = NULL;
	char *pub_sql = NULL;
	char *sql_query = NULL;
	int rc = -1;

	ensure_pki_directories();
	if (!build_cert_path(key_path, sizeof(key_path),
			     HIVE_NODE_CERT_DIR, node->uid, ".key"))
		goto cleanup;
	if (!build_cert_path(cert_path, sizeof(cert_path),
			     HIVE_NODE_CERT_DIR, node->uid, ".crt"))
		goto cleanup;
	if (!build_cert_path(csr_path, sizeof(csr_path),
			     HIVE_CERT_DIR, node->uid, ".csr"))
		goto cleanup;

	if (hive_guard_request_node_cert(node,
					 key_path,
					 csr_path,
					 cert_path) != 0) {
		int saved_err = errno;

		fprintf(stderr,
			"bootstrap: certmonger request failed for %s: %s\n",
			node->uid,
			strerror(saved_err));
		errno = saved_err;
	}

	touch_empty_file(key_path);
	touch_empty_file(cert_path);
	touch_empty_file(csr_path);

	const char *serial_src = node->serial[0] ? node->serial : node->uid;
	const char *name_src = node->name[0] ? node->name : node->uid;

	char serial_sql[sizeof(node->serial) * 2 + 1];
	char name_sql[sizeof(node->name) * 2 + 1];
	char uid_sql[sizeof(node->uid) * 2 + 1];

	unsigned long serial_len = (unsigned long)strlen(serial_src);
	unsigned long name_len = (unsigned long)strlen(name_src);
	unsigned long uid_len = (unsigned long)strlen(node->uid);

	unsigned long serial_sql_len = mysql_real_escape_string(
		sqldb.conn, serial_sql, serial_src, serial_len);
	serial_sql[serial_sql_len] = '\0';
	unsigned long name_sql_len = mysql_real_escape_string(
		sqldb.conn, name_sql, name_src, name_len);
	name_sql[name_sql_len] = '\0';
	unsigned long uid_sql_len = mysql_real_escape_string(
		sqldb.conn, uid_sql, node->uid, uid_len);
	uid_sql[uid_sql_len] = '\0';

	size_t priv_len = strlen(key_path);
	size_t pub_len = strlen(cert_path);

	priv_sql = malloc(priv_len * 2 + 1);
	pub_sql = malloc(pub_len * 2 + 1);
	if (!priv_sql || !pub_sql)
		goto cleanup;

	unsigned long priv_sql_len = mysql_real_escape_string(
		sqldb.conn, priv_sql, key_path, (unsigned long)priv_len);
	priv_sql[priv_sql_len] = '\0';
	unsigned long pub_sql_len = mysql_real_escape_string(
		sqldb.conn, pub_sql, cert_path, (unsigned long)pub_len);
	pub_sql[pub_sql_len] = '\0';

	size_t query_len = sizeof(SQL_HOST_AUTH_UPSERT_CERT) +
			   serial_sql_len + uid_sql_len + name_sql_len +
			   priv_sql_len + pub_sql_len + 64;
	sql_query = malloc(query_len);
	if (!sql_query)
		goto cleanup;

	int written = snprintf(sql_query, query_len,
			       SQL_HOST_AUTH_UPSERT_CERT,
			       serial_sql,
			       uid_sql,
			       name_sql,
			       priv_sql,
			       pub_sql,
			       NODE_CERT_VALID_DAYS);
	if (written < 0 || (size_t)written >= query_len)
		goto cleanup;

	if (mysql_real_query(sqldb.conn, sql_query,
			     (unsigned long)written) != 0) {
		fprintf(stderr,
			"bootstrap: failed to persist node certificate: %s\n",
			mysql_error(sqldb.conn));
		goto cleanup;
	}

	rc = 0;

cleanup:
	free(sql_query);
	free(priv_sql);
	free(pub_sql);
	return rc;
}

int update_node_for_add(struct hive_storage_node *local)
{
	if (!local)
		return -1;
	const uint64_t requested_node_id = hbc.storage_node_id;
	bool is_join = (g_pending_request_type == BOOTSTRAP_REQ_NODE_JOIN);
	if (hifs_update_node_for_add(&hbc, local, is_join, requested_node_id) != 0)
		return -1;

	add_node_join_token(hbc.storage_node_id, hbc.bootstrap_token);
	add_node_mtls_token(local);

	unsigned int status_pct = g_status_percent;
	if (status_pct > 100)
		status_pct = 100;
	char progress_pct[8];
	snprintf(progress_pct, sizeof(progress_pct), "%u%%", status_pct);
	char patch_level_str[8];
	snprintf(patch_level_str, sizeof(patch_level_str), "%04u",
		 (unsigned)hbc.storage_node_hive_patch_level);
	struct hive_guard_join_context join_ctx = {
		.cluster_id = hbc.cluster_id,
		.node_id = hbc.storage_node_id,
		.machine_uid = hbc.storage_node_uid,
		.node_record = local,
		.cluster_state = hbc.cluster_state,
		.database_state = hbc.database_state,
		.kv_state = hbc.kv_state,
		.cont1_state = hbc.cont1_state,
		.cont2_state = hbc.cont2_state,
		.min_nodes_required = hbc.min_nodes_req,
		.first_boot_ts = hbc.first_boot_ts,
		.config_status = g_config_state,
		.config_progress = progress_pct,
		.config_message = g_status_message,
		.hive_version = HIFS_VERSION,
		.hive_patch_level = patch_level_str,
		.node_version = hbc.storage_node_hive_version,
		.node_patch_level = hbc.storage_node_hive_patch_level,
	};
	if (hbc.storage_node_id == 1) {
		fprintf(stdout,
			"bootstrap: first node (id=%u) added to cluster (id=%llu)\n",
			hbc.storage_node_id,
			(unsigned long long)hbc.cluster_id);
		fflush(stdout);
	} else {
		fprintf(stdout,
			"bootstrap: node (id=%u) added to cluster (id=%llu)\n",
			hbc.storage_node_id,
			(unsigned long long)hbc.cluster_id);
		fflush(stdout);

		if (hive_guard_distribute_node_tokens(&join_ctx) != 0)
			fprintf(stderr,
				"bootstrap: failed to distribute node tokens to existing nodes\n");
	}
	return 0;
}
