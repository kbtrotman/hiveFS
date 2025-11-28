/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>

#include <liberasurecode/erasurecode.h>

#define HIFS_EC_STRIPES 6  /* k + m */
#define HIFS_SHARDS_PER_NODE 1  /* one shard per node until we have more ability in setup gui */

extern int last_node_in_cascade;
extern int cascade_length;

struct HifsEstripeLocations {
    uint32_t storage_node_id;
    uint32_t shard_id;
    uint64_t estripe_id;
    uint64_t block_offset;
};

/* ======================= Internal helpers ======================= */

/* Advance 6 positions from last_node_in_cascade, wrapping within [1, cascade_length] */
#define NEXT_STRIPE_NODE(last_node_in_cascade, cascade_length) \
    last_node_in_cascade = ((((last_node_in_cascade - 1) + 6) % (cascade_length)) + 1)


/* Decide which storage node & shard each stripe goes to */
void hifs_ec_choose_placement(uint64_t volume_id,
                              uint64_t block_no,
                              struct HifsEstripeLocations out_stripes[HIFS_EC_STRIPES]);

/* Allocate a unique global stripe id (could be per-Raft-group counter) */
uint64_t hifs_alloc_estripe_id(void);

/* Send one stripe to a storage node, get back its block_offset on that node */
int hifs_send_stripe_to_node(uint32_t storage_node_id,
                             uint32_t shard_id,
                             uint64_t estripe_id,
                             const uint8_t *data,
                             uint32_t len,
                             uint64_t *out_block_offset);

int hifs_recv_stripe_from_node(uint32_t storage_node_id,
                               uint32_t shard_id,
                               uint64_t estripe_id,
                               const uint8_t *data,
                               uint32_t len,
                               uint64_t *out_block_offset);

/* Submit the Raft command (this calls cowsql/raft) */
int hifs_raft_submit_put_block(const struct RaftPutBlock *cmd);



