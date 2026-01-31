/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */
#pragma once

#include <stdint.h>

#include "hive_guard.h"

#define GUARD_SOCK_STATE_LEN 32
#define GUARD_SOCK_STATUS_LEN 64
#define GUARD_SOCK_MSG_LEN 256
#define GUARD_SOCK_TOKEN_LEN 256
#define GUARD_SOCK_TS_LEN 128
#define GUARD_SOCK_PUBKEY_LEN 4096
#define GUARD_SOCK_UID_LEN 128

struct hive_guard_sock_join_sec {
	uint64_t cluster_id;
	uint64_t node_id;
	uint64_t min_nodes_req;
	char cluster_state[GUARD_SOCK_STATE_LEN];
	char database_state[GUARD_SOCK_STATE_LEN];
	char kv_state[GUARD_SOCK_STATE_LEN];
	char cont1_state[GUARD_SOCK_STATE_LEN];
	char cont2_state[GUARD_SOCK_STATE_LEN];
	char bootstrap_token[GUARD_SOCK_TOKEN_LEN];
	char first_boot_ts[GUARD_SOCK_TS_LEN];
	char config_status[GUARD_SOCK_STATUS_LEN];
	char config_progress[GUARD_SOCK_STATUS_LEN];
	char config_msg[GUARD_SOCK_MSG_LEN];
	char hive_version[GUARD_SOCK_STATUS_LEN];
	char hive_patch_level[GUARD_SOCK_STATUS_LEN];
	char pub_key[GUARD_SOCK_PUBKEY_LEN];
	char cduid[GUARD_SOCK_UID_LEN];
	char machine_uid[GUARD_SOCK_UID_LEN];
	char action[GUARD_SOCK_STATUS_LEN];
	int raft_replay;
};

struct hive_guard_storage_update_cmd {
	uint64_t cluster_id;
	uint64_t node_id;
	struct hive_storage_node node;
	uint32_t hive_version;
	uint32_t hive_patch_level;
};

struct hive_guard_join_context {
	uint64_t cluster_id;
	uint64_t node_id;
	const char *machine_uid;
	const struct hive_storage_node *node_record;
	const char *cluster_state;
	const char *database_state;
	const char *kv_state;
	const char *cont1_state;
	const char *cont2_state;
	uint64_t min_nodes_required;
	uint32_t flags;
	const char *first_boot_ts;
	const char *config_status;
	const char *config_progress;
	const char *config_message;
	const char *hive_version;
	const char *hive_patch_level;
	const char *cduid;
	const char *action;
	uint32_t node_version;
	uint32_t node_patch_level;
	int raft_replay;
};

int hive_guard_distribute_node_tokens(const struct hive_guard_join_context *ctx);
int hive_guard_apply_join_sec(const struct hive_guard_sock_join_sec *req);
int hive_guard_apply_storage_node_update(const struct hive_guard_storage_update_cmd *cmd);
int hive_guard_request_node_cert(const struct hive_storage_node *node,
				 const char *key_path,
				 const char *csr_path,
				 const char *cert_path);
int hive_guard_sock_start(void);
void hive_guard_sock_stop(void);
