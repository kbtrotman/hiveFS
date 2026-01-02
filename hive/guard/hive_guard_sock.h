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

#define HIVE_GUARD_SOCK_PATH "/run/hive_guard.sock"
#define HIVE_GUARD_SOCK_MSG_MAX 4096
#define HIVE_GUARD_SOCK_BACKLOG 8


struct hive_guard_join_context {
	uint64_t cluster_id;
	uint64_t node_id;
	const char *machine_uid;
	const char *cluster_state;
	const char *database_state;
	const char *kv_state;
	const char *cont1_state;
	const char *cont2_state;
	uint64_t min_nodes_required;
	const char *first_boot_ts;
	const char *config_status;
	const char *config_progress;
	const char *config_message;
	const char *hive_version;
	const char *hive_patch_level;
};

int hive_guard_distribute_node_tokens(const struct hive_guard_join_context *ctx);
int hive_guard_sock_start(void);
void hive_guard_sock_stop(void);
