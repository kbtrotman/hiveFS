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

#include "../../hicomm/hi_command.h"
#include "../../hifs_shared_defs.h"


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
    HG_OP_SNAPSHOT_MARK = 50,
    HG_OP_PUT_JOIN_SEC = 60,
    HG_OP_STORAGE_NODE_UPDATE = 61,
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
        struct RaftSnapshotMeta snapshot;
        struct hive_guard_join_context join_sec;
        struct hive_guard_storage_update_cmd storage_update;
    } u;
};


static pthread_mutex_t g_snap_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_snap_cv = PTHREAD_COND_INITIALIZER;

struct hg_local_snapshot_info {
    uint64_t snap_id;
    uint64_t raft_index;
    int      status;          // 0=ready, <0=error, 1=creating/unknown
    char     path[PATH_MAX];  // local snapshot file path
    uint64_t size_bytes;
    // optionally checksum string
};

static struct hg_local_snapshot_info g_last_snap = {
    .status = 1
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
int hifs_raft_submit_join_sec(const struct hive_guard_join_context *ctx);
int hifs_raft_submit_storage_update(const struct hive_guard_storage_update_cmd *cmd);
int hifs_raft_submit_snapshot_mark(uint64_t snap_id);

#endif /* HIVE_GUARD_RAFT_H */
