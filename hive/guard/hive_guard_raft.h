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
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/epoll.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/uio.h>
#include <sys/stat.h>
#include <limits.h>
#include <errno.h>

#include <mysql.h>   // libmariadbclient or libmysqlclient

#include <uv.h>
#include <raft.h>      // core algorithm state
#include <raft/uv.h>   // libuv-based I/O driver

#include "../../hifs_shared_defs.h"
#include "hive_guard.h"
#include "hive_guard_sock.h"
#include "hive_guard_auth.h"

struct hive_guard_token_metadata;


/* hive_guard_raft.h */

#ifndef HIVE_GUARD_RAFT_H
#define HIVE_GUARD_RAFT_H

#define OP_PUT_BLOCK  1

struct hg_raft_peer {
    uint64_t    id;
    const char *address;   /* "ip:port" */
};

struct hg_raft_config {
    uint64_t                self_id;
    const char             *self_address;  /* "ip:port" */
    const char             *data_dir;      /* e.g. "/var/lib/hive_guard/raft" */
    const struct hg_raft_peer *peers;
    unsigned                num_peers;
};

/* One stripe ID: which node, which local stripe row */
struct StripeId {
    uint32_t storage_node_id;
    uint32_t shard_id;
    uint64_t estripe_id;
    uint64_t block_offset;
};

enum hg_op_type {
    HG_OP_PUT_BLOCK = 1,
    HG_OP_PUT_INODE = 2,
    HG_OP_PUT_DIRENT = 3,
    HG_OP_PUT_SETTING = 4,
    HG_OP_PUT_CLUSTER_CERT = 5,
    HG_OP_PUT_CLUSTER_AUDIT = 6,
    HG_OP_ATOMIC_INODE_UPDATE = 7,
    HG_OP_DELETE_BLOCK = 8,
    HG_OP_DELETE_INODE = 9,
    HG_OP_ATOMIC_RENAME = 9,
    HG_OP_PUT_SESSION = 10,
    HG_OP_SESSION_CLEANUP = 11,
    HG_OP_SET_NODE_FULL_SYNC = 12,
    HG_OP_SET_NODE_TO_LEARNER = 13,
    HG_OP_PUT_JOIN_NODE = 14,
    HG_OP_PUT_NODE_DOWN = 15,
    HG_OP_CLUSTER_NODE_UP = 16,
    HG_OP_CLUSTER_FORCE_HEARTBEAT = 17,
    HG_OP_CLUSTER_DOWN = 18,
    HG_OP_CLUSTER_UP = 19,
    HG_OP_CLUSTER_INIT = 20,
    HG_OP_CLUSTER_NODE_FENCE = 21,
    HG_OP_STORAGE_NODE_UPDATE = 22,

    HG_OP_SNAPSHOT_MARK = 30,
    HG_OP_LEASE_MAKE = 31,
    HG_OP_LEASE_RENEW = 32,
    HG_OP_LEASE_RELEASE = 33,
    HG_OP_CACHE_CHECK = 34,
    HG_OP_CACHE_CLEAR = 35,
    HG_OP_CACHE_EVICTION = 36,
    HG_OP_CACHE_PURGE = 37,
// Ops up to 50 are for cluster-specific data-plane
// Ops in 50's are for GUI metadata updates
    HG_OP_NEW_TOKEN = 50,
    HG_OP_EXPIRE_TOKEN = 51,

    /* later: other read/write routines, etc. */
};

/* Raft metadata command for a deduped/EC block */
struct RaftPutBlock {
    uint8_t   hash_algo;   /* HIFS_HASH_ALGO_* */
    uint8_t   reserved8[3];
    uint64_t  volume_id;
    uint64_t  block_no;
    uint64_t  version;
    uint8_t   hash[32];             /* SHA-256 of block */
    struct StripeId ec_stripes[HIFS_EC_STRIPES];
};

struct RaftPutInode {
    uint64_t  volume_id;
    uint64_t  inode_id;
    uint64_t  version;
    struct hifs_inode_wire inode;
    uint16_t  fp_index;             /* which fingerprint slot */
    uint16_t  reserved;
};

struct RaftPutDirent {
    uint64_t  volume_id;
    uint64_t  dir_id;
    uint64_t  version;
    struct hifs_dir_entry dirent;
    uint16_t  reserved;
};

#define HG_CLUSTER_SETTING_KEY_MAX   64
#define HG_CLUSTER_SETTING_VALUE_MAX 256
struct RaftClusterSetting {
    uint64_t cluster_id;
    uint32_t flags;
    uint32_t reserved;
    char     key[HG_CLUSTER_SETTING_KEY_MAX];
    char     value[HG_CLUSTER_SETTING_VALUE_MAX];
};

#define HG_CLUSTER_CERT_FPR_LEN 32
#define HG_CLUSTER_CERT_LABEL_MAX 64
struct RaftClusterCert {
    uint64_t cluster_id;
    uint64_t node_id;
    uint32_t version;
    uint32_t reserved;
    uint8_t  fingerprint[HG_CLUSTER_CERT_FPR_LEN];
    char     label[HG_CLUSTER_CERT_LABEL_MAX];
};

#define HG_CLUSTER_AUDIT_MSG_MAX 256
struct RaftClusterAudit {
    uint64_t cluster_id;
    uint64_t node_id;
    uint64_t timestamp_ns;
    uint32_t severity;
    uint32_t reserved;
    char     message[HG_CLUSTER_AUDIT_MSG_MAX];
};

struct RaftClusterNodeMgmtCmd {
    uint64_t cluster_id;
    uint64_t node_id;
    uint64_t timestamp_ns;
    uint64_t arg0;
    uint64_t arg1;
    uint32_t flags;
    uint32_t reserved;
};

struct RaftClusterMgmtCmd {
    uint64_t cluster_id;
    uint64_t timestamp_ns;
    uint64_t arg0;
    uint64_t arg1;
    uint32_t flags;
    uint32_t reserved;
};

struct RaftAtomicInodeUpdate {
    uint64_t volume_id;
    uint64_t inode_id;
    uint64_t expect_version;
    uint64_t new_version;
    uint64_t field_mask;
    uint16_t fp_index;
    uint16_t reserved;
    struct hifs_inode_wire inode;
};

struct RaftDeleteBlock {
    uint64_t volume_id;
    uint64_t block_no;
    uint64_t version;
    uint8_t  hash_algo;
    uint8_t  reserved8[7];
    uint8_t  hash[HIFS_BLOCK_HASH_SIZE];
};

struct RaftDeleteInode {
    uint64_t volume_id;
    uint64_t inode_id;
    uint64_t version;
    uint32_t flags;
    uint32_t reserved;
};

struct RaftAtomicRename {
    uint64_t volume_id;
    uint64_t src_parent_inode;
    uint64_t dst_parent_inode;
    uint64_t src_inode;
    uint64_t dst_inode;
    uint32_t flags;
    uint32_t reserved;
    uint32_t src_name_len;
    uint32_t dst_name_len;
    char     src_name[HIFS_MAX_NAME_SIZE];
    char     dst_name[HIFS_MAX_NAME_SIZE];
};

#define HG_SESSION_USER_MAX     64
#define HG_SESSION_TOKEN_MAX    256
#define HG_SESSION_CONTEXT_MAX  256

struct RaftPutSession {
    uint64_t session_id;
    uint64_t node_id;
    uint64_t owner_uid;
    uint64_t created_ns;
    uint64_t expires_ns;
    uint32_t flags;
    uint32_t reserved;
    char     user_name[HG_SESSION_USER_MAX];
    char     token[HG_SESSION_TOKEN_MAX];
    char     context[HG_SESSION_CONTEXT_MAX];
};

struct RaftSessionCleanup {
    uint64_t session_id;
    uint64_t node_id;
    uint64_t timestamp_ns;
    uint32_t reason;
    uint32_t reserved;
    char     user_name[HG_SESSION_USER_MAX];
};

struct RaftJoinSec {
    uint64_t cluster_id;
    uint64_t node_id;
    uint64_t min_nodes_req;
    uint32_t flags;
    uint32_t reserved;
    char     cluster_name[GUARD_CLUSTER_NAME_LEN];
    char     cluster_desc[GUARD_CLUSTER_DESC_LEN];
    char     cluster_state[GUARD_SOCK_STATE_LEN];
    char     database_state[GUARD_SOCK_STATE_LEN];
    char     kv_state[GUARD_SOCK_STATE_LEN];
    char     cont1_state[GUARD_SOCK_STATE_LEN];
    char     cont2_state[GUARD_SOCK_STATE_LEN];
    char     bootstrap_token[GUARD_SOCK_TOKEN_LEN + 1];
    char     first_boot_ts[GUARD_SOCK_TS_LEN];
    char     config_status[GUARD_SOCK_STATUS_LEN];
    char     config_progress[GUARD_SOCK_STATUS_LEN];
    char     config_msg[GUARD_SOCK_MSG_LEN];
    char     hive_version[GUARD_SOCK_STATUS_LEN];
    char     hive_patch_level[GUARD_SOCK_STATUS_LEN];
    char     pub_key[GUARD_SOCK_PUBKEY_LEN];
    char     cduid[GUARD_SOCK_UID_LEN];
    char     machine_uid[GUARD_SOCK_UID_LEN];
    char     action[GUARD_SOCK_STATUS_LEN];
    char     user_id[GUARD_SOCK_UID_LEN];
    int32_t  raft_replay;
    int32_t  reserved_i32;
};

struct RaftClusterMakeSec {
    uint64_t cluster_id;
    uint64_t request_id;
    uint64_t timestamp_ns;
    uint32_t min_nodes_required;
    uint32_t flags;
    char     cluster_state[GUARD_SOCK_STATE_LEN];
    char     config_status[GUARD_SOCK_STATUS_LEN];
    char     description[HIFS_CLUSTER_DESC_MAX];
    char     initiator_uid[GUARD_SOCK_UID_LEN];
};

#define HG_LEASE_KEY_MAX 192

struct RaftLeaseCommand {
    char     key[HG_LEASE_KEY_MAX];
    uint64_t token;
    uint64_t ttl_ms;
    uint64_t timestamp_ms;
    uint32_t owner_node_id;
    uint32_t requester_node_id;
    uint32_t flags;
    uint8_t  mode;
    uint8_t  reserved[7];
};

#define HG_CACHE_TAG_MAX 128

struct RaftCacheCommand {
    uint64_t cluster_id;
    uint64_t node_id;
    uint64_t shard_id;
    uint64_t volume_id;
    uint64_t inode_id;
    uint64_t block_no;
    uint32_t flags;
    uint32_t reserved;
    char     tag[HG_CACHE_TAG_MAX];
};

#define HG_TOKEN_VALUE_MAX 64

struct RaftTokenCommand {
    uint64_t token_id;
    uint64_t expires_ns;
    uint32_t token_type;
    uint32_t flags;
    char     machine_uid[GUARD_SOCK_UID_LEN];
    char     token[HG_TOKEN_VALUE_MAX];
    struct hive_guard_token_metadata meta;
};

struct RaftTokenExpireCommand {
    uint64_t token_id;
    uint32_t flags;
    uint32_t reserved;
    char     token[HG_TOKEN_VALUE_MAX];
};

#define SNAPSHOT_MAGIC 0x534E4150u  // 'SNAP'

struct RaftSnapshotMeta {
    uint32_t magic;     // SNAPSHOT_MAGIC
    uint16_t opcode;    // meta_opcode
    uint16_t reserved;
    uint64_t snap_id;   // caller-chosen ID (monotonic)
    uint64_t ss_time;   // snapshot creation time (unix ns)
    uint64_t type;      // snapshot type (manual/auto/etc)
    uint64_t last_index_idx; // last included Raft log index
};

struct RaftCmd {
    uint8_t op_type;
    uint8_t reserved[3];
    union {
        struct RaftPutBlock block;   // current fields
        struct RaftPutInode inode;   // e.g., hifs_inode_wire + fp_index info
        struct RaftPutDirent dirent; // hifs_dir_entry
        struct RaftAtomicInodeUpdate atomic_inode;
        struct RaftDeleteBlock delete_block;
        struct RaftDeleteInode delete_inode;
        struct RaftAtomicRename atomic_rename;
        struct RaftPutSession session_put;
        struct RaftSessionCleanup session_cleanup;
        struct RaftClusterSetting setting;
        struct RaftClusterCert  cluster_cert;
        struct RaftClusterAudit cluster_audit;
        struct RaftJoinSec join_sec;
        struct RaftClusterMakeSec cluster_make_sec;
        struct RaftClusterNodeMgmtCmd node_full_sync;
        struct RaftClusterNodeMgmtCmd join_node;
        struct RaftClusterNodeMgmtCmd node_down;
        struct RaftClusterNodeMgmtCmd cluster_node_up;
        struct RaftClusterMgmtCmd cluster_force_heartbeat;
        struct RaftClusterMgmtCmd cluster_down;
        struct RaftClusterMgmtCmd cluster_up;
        struct RaftClusterMgmtCmd cluster_init;
        struct RaftClusterNodeMgmtCmd cluster_node_fence;
        struct RaftClusterNodeMgmtCmd node_to_learner;
        struct RaftLeaseCommand lease;
        struct RaftCacheCommand cache;
        struct RaftTokenCommand new_token;
        struct RaftTokenExpireCommand expire_token;
        struct RaftSnapshotMeta snapshot;
        struct hive_guard_storage_update_cmd storage_update;
    } u;
};
struct hg_local_snapshot_info {
    uint64_t snap_id;
    uint64_t raft_index;
    int      status;          // 0=ready, <0=error, 1=creating/unknown
    char     path[PATH_MAX];  // local snapshot file path
    uint64_t size_bytes;
    // optionally checksum string
};

// source of a snapshot file transfer over TCP, multiple nodes may have it
struct hg_snapshot_source {
    uint64_t snap_id;
    uint64_t raft_index;
    char     source_addr[256];   // "ip:port" of node serving file
    char     local_path[PATH_MAX]; // local path on source node
};


/* Start Raft in a background thread. Returns 0 on success. */
int  hg_raft_init(const struct hg_raft_config *cfg);

/* Optional: clean shutdown (can be TODO for now). */
void hg_raft_shutdown(void);

/* Helper: is this node currently the Raft leader? */
bool hg_guard_local_can_write(void);
int hifs_raft_submit_put_block(const struct RaftPutBlock *cmd);
int hifs_raft_submit_put_dirent(const struct RaftPutDirent *cmd);
int hifs_raft_submit_session(const struct RaftPutSession *session);
int hifs_raft_submit_join_sec(const struct hive_guard_join_context *ctx);
int hifs_raft_submit_storage_update(const struct hive_guard_storage_update_cmd *cmd);
int hifs_raft_submit_snapshot_mark(uint64_t snap_id);
int hg_prepare_snapshot_for_new_node(const struct hg_raft_config *cfg,
                                     uint64_t snap_id,
                                     const char *new_node_addr,
                                     struct hg_snapshot_source *out_src);
int hg_raft_call_update_token(const struct hive_guard_token_metadata *meta);

#endif /* HIVE_GUARD_RAFT_H */
