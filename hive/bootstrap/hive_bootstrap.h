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


#define ENV_LISTEN_ADDR  "WRAP_LISTEN_ADDR"
#define ENV_LISTEN_PORT  "WRAP_LISTEN_PORT"
#define ENV_IDLE_MS      "WRAP_IDLE_TIMEOUT_MS"
#define CLIENT_TCP_IDLE_TIMEOUT_PATH "/sys/class/hivefs/CLIENT_TCP_IDLE_TIMEOUT"
#define ENV_MDB_HOST     "WRAP_MARIADB_HOST"
#define ENV_MDB_PORT     "WRAP_MARIADB_PORT"
#define ENV_MDB_USER     "WRAP_MARIADB_USER"
#define ENV_MDB_PASS     "WRAP_MARIADB_PASS"
#define ENV_MDB_DB       "WRAP_MARIADB_DB"


struct hive_bootstrap_config {
	uint64_t cluster_id;
	char cluster_state[15];
	char database_state[15];
	char cont1_state[15];
	char cont2_state[15];
	uint64_t min_nodes_req;
	char bootstrap_token[100];
	char first_boot_ts[30];

	uint32_t storage_node_id;
	char storage_node_name[50];
	char storage_node_address[64];
	char storage_node_uid[128];
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
};

extern struct hive_bootstrap_config hbc;

#define cluster_id      hbc.cluster_id
#define cluster_state   hbc.cluster_state
#define database_state  hbc.database_state
#define cont1_state     hbc.cont1_state
#define cont2_state     hbc.cont2_state
#define min_nodes_req   hbc.min_nodes_req
#define bootstrap_token hbc.bootstrap_token
#define first_boot_ts   hbc.first_boot_ts

// Prototypes
int hive_bootstrap_main(void);
