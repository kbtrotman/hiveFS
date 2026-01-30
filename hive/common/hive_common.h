/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

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

#define HIVE_DATA_DIR         "/hive"
#define HIVE_GUARD_KV_DIR     HIVE_DATA_DIR "/hive_kv"
#define HIVE_METADATA_DIR     HIVE_DATA_DIR "/metadata"
#define HIVE_GUARD_RAFT_DIR   HIVE_DATA_DIR "/hive_guard_raft"
#define HIVE_LOG_DIR          HIVE_DATA_DIR "/logs"
#define HIVE_UI_BACKEND_ROOT  "/opt/backend"
#define HIVE_UI_FRONTEND_ROOT "/opt/frontend"
#define HIVE_UI_SYSTEMD_DIR   "/etc/systemd/system"
#define HIVE_BOOTSTRAP_SYS_DIR "/etc/hivefs"
#define HIVE_NODE_CONF_PATH   HIVE_BOOTSTRAP_SYS_DIR "/node.json.conf"
#define HIVE_CLUSTER_DIR      HIVE_BOOTSTRAP_SYS_DIR "/cluster"
#define HIVE_CLUSTER_ID       HIVE_CLUSTER_DIR "/identity"
#define HIVE_CLUSTER_NAME     HIVE_CLUSTER_DIR "/name"
#define HIVE_NODE_DIR         HIVE_BOOTSTRAP_SYS_DIR "/node"
#define HIVE_NODE_ID          HIVE_NODE_DIR "/cl_node_uuid"
#define HW_NODE_UID           HIVE_NODE_ID "/machine_uid"
#define HIVE_GUARD_SQL_LOG    HIVE_LOG_DIR "/hive_guard_sql.log"
#define HIVE_GUARD_LOG        HIVE_LOG_DIR "/hive_guard.log"
#define HIVE_BOOTSTRAP_LOG    HIVE_LOG_DIR "/hive_bootstrap.log"
#define HIVE_PKI_DIR          HIVE_BOOTSTRAP_SYS_DIR "/pki"
#define HIVE_CA_ROOT          HIVE_PKI_DIR "/ca"      // CA cert we want to trust, local copy
#define HIVE_NODE_CERT_DIR    HIVE_PKI_DIR "/node"    // This node's HW & Cluster identity
#define HIVE_RAFT_CERT_DIR    HIVE_PKI_DIR "/raft"    // if we want a seperate identity for raft operations
#define HIVE_API_CERT_DIR     HIVE_PKI_DIR "/api"     // if we want a seperate identity for the backend api connections
#define HIVE_CERT_DIR         HIVE_PKI_DIR "/requests"   // cert request copies

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


#define ENV_LISTEN_ADDR  "WRAP_LISTEN_ADDR"
#define ENV_LISTEN_PORT  "WRAP_LISTEN_PORT"
#define ENV_IDLE_MS      "WRAP_IDLE_TIMEOUT_MS"
#define CLIENT_TCP_IDLE_TIMEOUT_PATH "/sys/class/hivefs/CLIENT_TCP_IDLE_TIMEOUT"
#define ENV_MDB_HOST     "WRAP_MARIADB_HOST"
#define ENV_MDB_PORT     "WRAP_MARIADB_PORT"
#define ENV_MDB_USER     "WRAP_MARIADB_USER"
#define ENV_MDB_PASS     "WRAP_MARIADB_PASS"
#define ENV_MDB_DB       "WRAP_MARIADB_DB"
