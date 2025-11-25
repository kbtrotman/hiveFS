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
    uint32_t node_id;          /* storage node id */
    uint64_t local_estripe_id; /* estripe_id in that node's hive_data.ecblocks */
};

/* Raft metadata command for a deduped/EC block */
struct RaftPutBlock {
    uint8_t   op_type;     /* OP_PUT_BLOCK */
    uint8_t   hash_algo;   /* 1 = SHA-256 */
    uint16_t  reserved16;  /* padding / future flags */
    uint32_t  reserved32;  /* padding / future use */

    uint64_t  volume_id;   /* which volume this block belongs to */
    uint64_t  block_no;    /* logical block number within the volume */
    uint64_t  version;     /* optional version of this mapping */

    uint8_t   hash[32];    /* SHA-256 of 4K block */

    struct StripeId ec_stripes[6];  /* 4 data + 2 parity stripes */
};


/* Start Raft in a background thread. Returns 0 on success. */
int  hg_raft_init(const struct hg_raft_config *cfg);

/* Optional: clean shutdown (can be TODO for now). */
void hg_raft_shutdown(void);

/* Helper: is this node currently the Raft leader? */
bool hg_guard_local_can_write(void);

#endif /* HIVE_GUARD_RAFT_H */
