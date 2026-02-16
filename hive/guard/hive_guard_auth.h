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
 * user and group management as they relate to tokens and certs.
 *
 * Basically, this is client access, node access, and RBAC.
 *
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <sys/random.h>
#include <errno.h>

#define NODE_CERT_VALID_DAYS 365
#define HIVE_GUARD_TOKEN_EXPIRY_SECS (48 * 60 * 60)

#define HIVE_GUARD_UUID_STR_LEN 37
#define HIVE_GUARD_TOKEN_TS_LEN 32
#define HIVE_GUARD_BOOTSTRAP_TOKEN_LEN 44
#define HIVE_GUARD_TOKEN_TYPE_LEN 32
#define HIVE_GUARD_UID_LEN 128
#define HIVE_USER_ID_LEN 15

struct hive_guard_token_metadata {
	uint64_t cluster_id;
	bool has_cluster_id;
	char bootstrap_token[HIVE_GUARD_BOOTSTRAP_TOKEN_LEN];
	char first_boot_ts[HIVE_GUARD_TOKEN_TS_LEN];
	char tid[HIVE_GUARD_UUID_STR_LEN];
	char token_type[HIVE_GUARD_TOKEN_TYPE_LEN];
	char host_uuid[HIVE_GUARD_UID_LEN];
	char host_mid[HIVE_GUARD_UID_LEN];
	char issued_at[HIVE_GUARD_TOKEN_TS_LEN];
	char expires_at[HIVE_GUARD_TOKEN_TS_LEN];
	char approved_at[HIVE_GUARD_TOKEN_TS_LEN];
	char approved_by[HIVE_USER_ID_LEN];
};

struct hive_storage_node;

int hive_guard_generate_token_metadata(struct hive_guard_token_metadata *meta);
const struct hive_guard_token_metadata *hive_guard_get_last_token_metadata(void);
int hive_guard_generate_node_token(char out[44], struct hive_guard_token_metadata *meta);
int add_node_mtls_token(const struct hive_storage_node *node);
int add_node_join_token(uint32_t node_id, const char *token);
int update_node_for_add(struct hive_storage_node *local);
