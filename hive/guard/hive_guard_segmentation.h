/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

#pragma once

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../../hifs_shared_defs.h"
#include "../common/hive_common.h"
#include "hive_guard_placement.h"

#define HIVE_GUARD_SEGMT_DIR            HIVE_DATA_DIR "/writeback_cache"
#define HIVE_GUARD_SEGMT_RECV_NEW       HIVE_GUARD_SEGMT_DIR "/recv_new"
#define HIVE_GUARD_SEGMT_PROC_SEG       HIVE_GUARD_SEGMT_DIR "/process_segments"
#define HIVE_GUARD_SEGMT_READ_RETURN    HIVE_GUARD_SEGMT_DIR "/read_return"
#define HIVE_GUARD_SEGMT_RECV_NEW_INDX  HIVE_GUARD_SEGMT_RECV_NEW "/recv_new_segment.indx"
#define HIVE_GUARD_SEGMT_PROC_SEG_INDX  HIVE_GUARD_SEGMT_PROC_SEG "/proc_seg_segment.indx"
#define HIVE_GUARD_SEGMT_RRET_INDX      HIVE_GUARD_SEGMT_READ_RETURN "/read_return_segment.indx"

#define HIVE_SEG_BLOCK_FILE_MAGIC   0x48534742u
#define HIVE_SEG_BLOCK_FILE_VERSION 1u
#define HIVE_SEG_QUEUE_DEPTH 512u
#define HIVE_SEG_MAX_WORKERS 8u

typedef void (*hive_seg_release_fn)(void *ctx, void *data, uint32_t len);

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
    bool has_frag_targets;
    uint32_t frag_target_count;
    struct hifs_fragment_target frag_targets[HIFS_EC_TOTAL_SRIPES];
};

enum hive_seg_task_type {
    HIVE_SEG_TASK_SINGLE = 0,
    HIVE_SEG_TASK_CONTIG = 1,
};

struct hive_seg_task {
    enum hive_seg_task_type type;
    uint64_t txn_hint;
    uint32_t generation;
    union {
        struct hive_seg_block_input single;
        struct {
            struct hive_seg_block_input *blocks;
            uint32_t count;
        } contig;
    } u;
};

struct hive_seg_mcl_block_entry {
    uint64_t volume_id;
    uint64_t block_no;
    enum hifs_hash_algorithm hash_algo;
    enum hifs_stripe_id_algorithm stripe_algo;
    uint32_t block_bytes;
    uint32_t placement_epoch;
    uint8_t hash[HIFS_BLOCK_HASH_SIZE];
    uint8_t stripe_id[HIFS_STRIPE_ID_SIZE];
};

struct hive_seg_stage_cursor {
    uint64_t seg_id;
    uint32_t bytes_used;
};

#pragma pack(push, 1)
struct hive_seg_block_file_hdr {
    uint32_t magic;
    uint16_t version;
    uint16_t chunk_count;
    uint32_t chunk_len;
    uint32_t block_bytes;
    uint32_t landing_length;
    uint64_t seg_id;
    uint64_t volume_id;
    uint64_t block_no;
    uint64_t txn_id;
    uint32_t generation;
    uint32_t landing_offset;
    uint8_t hash_algo;
    uint8_t stripe_algo;
    uint8_t reserved[6];
    uint8_t hash[HIFS_BLOCK_HASH_SIZE];
};

struct hive_seg_index_disk_rec {
    uint64_t seg_id;
    uint64_t volume_id;
    uint64_t block_no;
    uint64_t txn_id;
    uint32_t generation;
    uint32_t state;
    uint64_t offset;
    uint32_t length;
    uint32_t reserved;
};
#pragma pack(pop)

struct hive_seg_index_mem {
    struct hive_seg_index_disk_rec *entries;
    size_t count;
    size_t capacity;
};

struct hive_seg_cache_ctx {
    pthread_mutex_t mu;
    bool ready;
    int landing_idx_fd;
    int outbound_idx_fd;
    int read_return_idx_fd;
    struct hive_seg_index_mem landing_idx;
    struct hive_seg_index_mem outbound_idx;
    struct hive_seg_index_mem read_return_idx;
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

int hive_seg_checkout_persisted_segment(uint64_t seg_id,
                                        uint8_t **out_data,
                                        size_t *out_len);
void hive_seg_release_persisted_segment(uint64_t seg_id,
                                        bool changed);
