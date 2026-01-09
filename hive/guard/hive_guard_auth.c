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
 * Basically, this is client access,l node access, and RBAC.
 * 
 */
#include "hive_guard_auth.h"
#include "hive_guard_sql.h"
#include "hive_guard_sock.h"
#include "../bootstrap/hive_bootstrap_sock.h"
#include "../../hifs_shared_defs.h"

#include <openssl/asn1.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define NODE_CERT_KEY_BITS   2048
#define NODE_CERT_VALID_DAYS 365

static bool bio_to_string(BIO *bio, char **out)
{
	if (!bio || !out)
		return false;

	char *data = NULL;
	long len = BIO_get_mem_data(bio, &data);

	if (len <= 0 || !data)
		return false;
	char *buf = malloc((size_t)len + 1);

	if (!buf)
		return false;
	memcpy(buf, data, (size_t)len);
	buf[len] = '\0';
	*out = buf;
	return true;
}

static bool generate_node_certificate(const struct hive_storage_node *node,
				      char **priv_pem_out,
				      char **pub_pem_out)
{
	if (!node || !priv_pem_out || !pub_pem_out)
		return false;
	*priv_pem_out = NULL;
	*pub_pem_out = NULL;

	bool ok = false;
	EVP_PKEY_CTX *key_ctx = NULL;
	EVP_PKEY *pkey = NULL;
	X509 *x509 = X509_new();
	BIO *priv_bio = NULL;
	BIO *cert_bio = NULL;

	if (!x509)
		goto cleanup;
	key_ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL);
	if (!key_ctx)
		goto cleanup;
	if (EVP_PKEY_keygen_init(key_ctx) <= 0)
		goto cleanup;
	if (EVP_PKEY_CTX_set_rsa_keygen_bits(key_ctx, NODE_CERT_KEY_BITS) <= 0)
		goto cleanup;
	if (EVP_PKEY_keygen(key_ctx, &pkey) <= 0)
		goto cleanup;

	X509_set_version(x509, 2);
	uint32_t serial = (uint32_t)(time(NULL) ^ (uint32_t)getpid());

	if (!serial)
		serial = 1;
	if (!ASN1_INTEGER_set(X509_get_serialNumber(x509), (long)serial))
		goto cleanup;
	if (!X509_gmtime_adj(X509_get_notBefore(x509), 0) ||
	    !X509_gmtime_adj(X509_get_notAfter(x509),
			     (long)NODE_CERT_VALID_DAYS * 24L * 3600L))
		goto cleanup;
	if (X509_set_pubkey(x509, pkey) != 1)
		goto cleanup;

	X509_NAME *name = X509_get_subject_name(x509);
	const char *cn = (node->name[0] != '\0') ? node->name : node->uid;

	if (!cn || !*cn)
		cn = "hivefs-node";
	if (X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
				       (const unsigned char *)cn,
				       -1, -1, 0) != 1)
		goto cleanup;
	if (node->uid[0])
		X509_NAME_add_entry_by_txt(name, "UID", MBSTRING_ASC,
					   (const unsigned char *)node->uid,
					   -1, -1, 0);
	if (node->serial[0])
		X509_NAME_add_entry_by_txt(name, "serialNumber",
					   MBSTRING_ASC,
					   (const unsigned char *)node->serial,
					   -1, -1, 0);
	if (node->address[0])
		X509_NAME_add_entry_by_txt(name, "L", MBSTRING_ASC,
					   (const unsigned char *)node->address,
					   -1, -1, 0);
	if (X509_set_issuer_name(x509, name) != 1)
		goto cleanup;
	if (X509_sign(x509, pkey, EVP_sha256()) <= 0)
		goto cleanup;

	priv_bio = BIO_new(BIO_s_mem());
	cert_bio = BIO_new(BIO_s_mem());
	if (!priv_bio || !cert_bio)
		goto cleanup;
	if (!PEM_write_bio_PrivateKey(priv_bio, pkey, NULL, NULL, 0,
				      NULL, NULL))
		goto cleanup;
	if (!PEM_write_bio_X509(cert_bio, x509))
		goto cleanup;
	if (!bio_to_string(priv_bio, priv_pem_out) ||
	    !bio_to_string(cert_bio, pub_pem_out))
		goto cleanup;

	ok = true;

cleanup:
	BIO_free(cert_bio);
	BIO_free(priv_bio);
	X509_free(x509);
	EVP_PKEY_free(pkey);
	EVP_PKEY_CTX_free(key_ctx);
	if (!ok) {
		free(*priv_pem_out);
		free(*pub_pem_out);
		*priv_pem_out = NULL;
		*pub_pem_out = NULL;
	}
	return ok;
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

	char *priv_pem = NULL;
	char *pub_pem = NULL;
	char *priv_sql = NULL;
	char *pub_sql = NULL;
	char *sql_query = NULL;
	int rc = -1;

	if (!generate_node_certificate(node, &priv_pem, &pub_pem)) {
		fprintf(stderr,
			"bootstrap: failed to generate node mTLS certificate\n");
		goto cleanup;
	}

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

	size_t priv_len = strlen(priv_pem);
	size_t pub_len = strlen(pub_pem);

	priv_sql = malloc(priv_len * 2 + 1);
	pub_sql = malloc(pub_len * 2 + 1);
	if (!priv_sql || !pub_sql)
		goto cleanup;

	unsigned long priv_sql_len = mysql_real_escape_string(
		sqldb.conn, priv_sql, priv_pem, (unsigned long)priv_len);
	priv_sql[priv_sql_len] = '\0';
	unsigned long pub_sql_len = mysql_real_escape_string(
		sqldb.conn, pub_sql, pub_pem, (unsigned long)pub_len);
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
	if (priv_pem) {
		memset(priv_pem, 0, strlen(priv_pem));
		free(priv_pem);
	}
	free(pub_pem);
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
