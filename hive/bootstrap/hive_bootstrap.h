/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

#pragma once


#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/uio.h>
#include <sys/stat.h>
 
#include <mysql.h>   // libmariadbclient or libmysqlclient
#include "../../hicomm/hi_command.h"
#include "../common/hive_common.h"

enum hive_bootstrap_stage {
	HIVE_STAGE_INITIAL = 0,
	HIVE_STAGE_DATABASE = 1,
	HIVE_STAGE_KV = 2,
	HIVE_STAGE_CONTAINERS = 3,
	HIVE_STAGE_WEB_READY = 4,
	HIVE_STAGE_COMPLETE = 5,
};

struct hive_bootstrap_config {
	uint64_t cluster_id;
	char cluster_state[15];
	char database_state[15];
	char kv_state[15];
	char cont1_state[15];
	char cont2_state[15];
	uint64_t min_nodes_req;
	char bootstrap_token[100];
	char first_boot_ts[30];

	uint32_t storage_node_id;
	char storage_node_name[100];
	char storage_node_address[64];
	char storage_node_uid[128];
	char storage_node_cduid[64];
	char storage_node_serial[100];
	uint16_t storage_node_guard_port;
	uint16_t storage_node_stripe_port;
	uint64_t storage_node_last_heartbeat;
	uint8_t  storage_node_fenced;
	uint32_t storage_node_hive_version;
	uint32_t storage_node_hive_patch_level;
	uint64_t storage_node_last_maintenance;
	char     storage_node_maintenance_reason[256];
	uint64_t storage_node_maintenance_started_at;
	uint64_t storage_node_maintenance_ended_at;
	uint64_t storage_node_date_added_to_cluster;
	uint64_t storage_node_storage_capacity_bytes;
	uint64_t storage_node_storage_used_bytes;
	uint64_t storage_node_storage_reserved_bytes;
	uint64_t storage_node_storage_overhead_bytes;
	uint32_t storage_node_client_connect_timeout_ms;
	uint32_t storage_node_storage_node_connect_timeout_ms;
	uint32_t stage_of_config;
	uint32_t num_attempts_this_stage;
	bool ready_for_web_conf;
};

extern struct hive_bootstrap_config hbc;



// Prototypes
int hive_bootstrap_main(void);
bool hive_bootstrap_set_stage(enum hive_bootstrap_stage stage, bool reset_attempts);
bool hive_bootstrap_reset_stage_attempts(void);
bool hive_bootstrap_increment_stage_attempts(void);
bool hive_bootstrap_update_string_field(const char *key, const char *value);
bool hive_bootstrap_update_number_field(const char *key, uint64_t value);
