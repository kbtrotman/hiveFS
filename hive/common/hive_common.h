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
#define HIVE_PKI_DIR          HIVE_BOOTSTRAP_SYS_DIR "/pki"


#define HIVE_GUARD_SNAPSHOT_BASE_DIR HIVE_DATA_DIR "/meta/snapshots"
#define HIVE_GUARD_SNAPSHOT_DIR_FMT  "snap-%llu-idx-%llu"
#define HIVE_GUARD_SNAPSHOT_FILE_FMT "meta-snap-%llu-idx-%llu.sql"

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