/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/epoll.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/uio.h>
#include <sys/stat.h>
 
#include <mysql.h>   // libmariadbclient or libmysqlclient

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
    struct StripeId ec_stripes[6];  /* 4 data + 2 parity or whatever profile */
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

struct RaftCmd {
    uint8_t op_type;
    uint8_t reserved[3];
    union {
        struct RaftPutBlock block;   // current fields
        struct RaftPutInode inode;   // e.g., hifs_inode_wire + fp_index info
        struct RaftPutDirent dirent; // hifs_dir_entry
    } u;
};


/* Start Raft in a background thread. Returns 0 on success. */
int  hg_raft_init(const struct hg_raft_config *cfg);

/* Optional: clean shutdown (can be TODO for now). */
void hg_raft_shutdown(void);

/* Helper: is this node currently the Raft leader? */
bool hg_guard_local_can_write(void);
int hifs_raft_submit_put_block(const struct RaftPutBlock *cmd);
int hifs_raft_submit_put_dirent(const struct RaftPutDirent *cmd);

#endif /* HIVE_GUARD_RAFT_H */
