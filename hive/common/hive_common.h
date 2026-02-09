/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/*
 *
 * Common General helpers/Globals
 *
 */

#define ENV_LISTEN_ADDR  "WRAP_LISTEN_ADDR"
#define ENV_LISTEN_PORT  "WRAP_LISTEN_PORT"
#define ENV_IDLE_MS      "WRAP_IDLE_TIMEOUT_MS"
#define CLIENT_TCP_IDLE_TIMEOUT_PATH "/sys/class/hivefs/CLIENT_TCP_IDLE_TIMEOUT"
#define ENV_MDB_HOST     "WRAP_MARIADB_HOST"
#define ENV_MDB_PORT     "WRAP_MARIADB_PORT"
#define ENV_MDB_USER     "WRAP_MARIADB_USER"
#define ENV_MDB_PASS     "WRAP_MARIADB_PASS"
#define ENV_MDB_DB       "WRAP_MARIADB_DB"

// Data DIRS
#define HIVE_DATA_DIR         "/hive"
#define HIVE_GUARD_KV_DIR     HIVE_DATA_DIR "/hive_kv"
#define HIVE_METADATA_DIR     HIVE_DATA_DIR "/metadata"
#define HIVE_GUARD_RAFT_DIR   HIVE_DATA_DIR "/hive_guard_raft"
#define HIVE_LOG_DIR          HIVE_DATA_DIR "/logs"
#define HIVE_UI_BACKEND_ROOT  HIVE_DATA_DIR "/backend"
#define HIVE_UI_FRONTEND_ROOT HIVE_DATA_DIR "/frontend"
// SETUP Dirs/Files
#define HIVE_UI_SYSTEMD_DIR   "/etc/systemd/system"
#define HIVE_BOOTSTRAP_SYS_DIR "/etc/hivefs"
#define HIVE_NODE_CONF_PATH   HIVE_BOOTSTRAP_SYS_DIR "/node.json.conf"
#define HIVE_CLUSTER_DIR      HIVE_BOOTSTRAP_SYS_DIR "/cluster"
#define HIVE_CLUSTER_ID       HIVE_CLUSTER_DIR "/cluster.identity"
#define HIVE_CLUSTER_NAME     HIVE_CLUSTER_DIR "/name"
#define HIVE_NODE_DIR         HIVE_BOOTSTRAP_SYS_DIR "/node"
#define HIVE_NODE_CLUST_UUID  HIVE_NODE_DIR   "/cl_node_uuid.identity"
#define HW_NODE_HW_UUID       HIVE_NODE_DIR  "/node_uuid.identity"
#define HIVE_GUARD_SQL_LOG    HIVE_LOG_DIR    "/hive_guard_sql.log"
#define HIVE_GUARD_LOG        HIVE_LOG_DIR    "/hive_guard.log"
#define HIVE_BOOTSTRAP_LOG    HIVE_LOG_DIR    "/hive_bootstrap.log"
#define HIVE_PKI_DIR          HIVE_BOOTSTRAP_SYS_DIR "/pki"
#define HIVE_CA_ROOT          HIVE_PKI_DIR    "/ca"      // CA cert we want to trust, local copy
#define HIVE_CLUSTER_CA       HIVE_CA_ROOT    "/cluster.pem"
#define HIVE_CLUSTER_CERT     HIVE_CA_ROOT    "/cluster.cert"
#define HIVE_NODE_CERT_DIR    HIVE_PKI_DIR    "/node"    // This node's HW & Cluster identity
#define HIVE_NODE_CA          HIVE_NODE_CERT_DIR    "/node.pem"
#define HIVE_NODE_CERT        HIVE_NODE_CERT_DIR    "/node.cert"
#define HIVE_SN_CA_ROOT       HIVE_CA_ROOT    "/storage-nodes"
#define HIVE_RAFT_CERT_DIR    HIVE_PKI_DIR    "/raft"    // if we want a seperate identity for raft operations
#define HIVE_API_CERT_DIR     HIVE_PKI_DIR    "/api"     // if we want a seperate identity for the backend api connections
#define HIVE_CERT_DIR         HIVE_PKI_DIR    "/requests"   // cert request copies

#define HIVE_GUARD_SNAPSHOT_BASE_DIR HIVE_DATA_DIR "/snapshots"
#define HIVE_GUARD_SNAPSHOT_DIR_FMT  "snap-%llu-idx-%llu"
#define HIVE_GUARD_SNAPSHOT_FILE_FMT "meta-snap-%llu-idx-%llu.sql"
#define HIVE_GUARD_SNAPSHOT_META_FILE HIVE_GUARD_SNAPSHOT_BASE_DIR "/hive_guard_snapshot.data"

#ifndef HIVE_GUARD_SOCK_PATH
#define HIVE_GUARD_SOCK_PATH "/run/hive_guard.sock"
#endif
#ifndef HIVE_GUARD_SOCK_MSG_MAX
#define HIVE_GUARD_SOCK_MSG_MAX 4096
#endif
#ifndef HIVE_GUARD_SOCK_BACKLOG
#define HIVE_GUARD_SOCK_BACKLOG 8
#endif

#define HIVE_BOOTSTRAP_BIN    "hive_bootstrap"

#ifndef HIVE_BOOTSTRAP_SOCK_PATH
#define HIVE_BOOTSTRAP_SOCK_PATH "/run/hive_bootstrap.sock"
#endif
#ifndef HIVE_BOOTSTRAP_MSG_MAX
#define HIVE_BOOTSTRAP_MSG_MAX   4096
#endif
#ifndef HIVE_BOOTSTRAP_BACKLOG
#define HIVE_BOOTSTRAP_BACKLOG   4
#endif


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

enum bootstrap_request_type {
	BOOTSTRAP_REQ_UNKNOWN = 0,
	BOOTSTRAP_REQ_CLUSTER,
	BOOTSTRAP_REQ_NODE_JOIN,
};

extern struct hive_bootstrap_config hbc;
extern char g_status_message[64];
extern unsigned int g_status_percent;
extern char g_config_state[16];
extern enum bootstrap_request_type g_pending_request_type;
