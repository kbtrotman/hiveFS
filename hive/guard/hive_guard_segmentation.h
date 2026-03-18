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
#include <stddef.h>
#include <stdint.h>

#include "../../hifs_shared_defs.h"
#include "../common/hive_common.h"
#include "hive_guard_placement.h"

#define HIVE_GUARD_SEGMT_DIR          HIVE_DATA_DIR "/writeback_cache"
#define HIVE_GUARD_SEGMT_LANDING      HIVE_GUARD_SEGMT_DIR "/landing"
#define HIVE_GUARD_SEGMT_OUTBOUND     HIVE_GUARD_SEGMT_DIR "/outbound"
#define HIVE_GUARD_SEGMT_READ_RETURN  HIVE_GUARD_SEGMT_DIR "/read_return"
#define HIVE_GUARD_SEGMT_LAND_INDX    HIVE_GUARD_SEGMT_LANDING "/landing_segment.indx"
#define HIVE_GUARD_SEGMT_OUTB_INDX    HIVE_GUARD_SEGMT_LANDING "/outbound_segment.indx"
#define HIVE_GUARD_SEGMT_RRET_INDX    HIVE_GUARD_SEGMT_LANDING "/read_return_segment.indx"

typedef void (*hive_seg_release_fn)(void *ctx, void *data, uint32_t len);

#define HIVE_SEG_QUEUE_DEPTH 512u
#define HIVE_SEG_MAX_WORKERS 8u

struct hive_seg_block_input {
    uint64_t volume_id;
    uint64_t block_no;
    void *data;
    uint32_t len;
    enum hifs_hash_algorithm hash_algo;
    bool has_hash;
    uint8_t hash[HIFS_BLOCK_HASH_SIZE];
    bool has_stripe_id;
    uint8_t stripe_id[HIFS_STRIPE_ID_SIZE];
    enum hifs_stripe_id_algorithm stripe_algo;
    uint32_t placement_epoch;
    hive_seg_release_fn release_cb;
    void *release_ctx;
    bool has_landing_loc;
    struct hifs_seg_loc landing_loc;
};

struct hive_seg_single_request {
    const struct hive_seg_block_input *block;
    uint64_t txn_hint;
    uint32_t generation;
};

struct hive_seg_contig_request {
    const struct hive_seg_block_input *blocks;
    uint32_t block_count;
    uint64_t txn_hint;
    uint32_t generation;
};

/* prototype */
int hive_seg_submit_single(const struct hive_seg_single_request *req);
int hive_seg_submit_contig(const struct hive_seg_contig_request *req);

int hive_recieve_data_plane(void);
void hive_segmentation_shutdown(void);
