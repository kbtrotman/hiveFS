/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

 #include <string.h>

#include "../common/hive_common.h"


#define HIVE_PLACEMENT_DIR HIVE_DATA_DIR "/hive_placement"
#define HIVE_PLACEMENT_FILE HIVE_PLACEMENT_DIR "/spl-%llu.log"  /* may not need this, it's probably in the WBL already. */

#define HIFS_PLACEMENT_LOG_DIR HIVE_LOG_DIR "/placement_logs"
#define HIFS_PLACEMENT_LOG_FILE HIFS_PLACEMENT_LOG_DIR "/placement-%llu.log"

#define HIFS_SEGMENT_SIZE  (16U * 1024U * 1024U) /* 16 MiB segments */
#define HIFS_SEGMENT_VERSION   1
#define HIFS_PLACEMENT_VERSION 1

#define HIFS_LANDING_CACHE_PCT           40
#define HIFS_OUTBOUND_CACHE_PCT          30
#define HIFS_READ_RETURN_CACHE_PCT       20
#define HIFS_EMERGENCY_HEADROOM_PCT      10


enum hifs_stripe_state {
    HIFS_STRIPE_NEW = 0,
    HIFS_STRIPE_INTENT_LOGGED,
    HIFS_STRIPE_LANDING_RESERVED,
    HIFS_STRIPE_LANDING_WRITTEN,
    HIFS_STRIPE_PREPARED,
    HIFS_STRIPE_PLACED,
    HIFS_STRIPE_OUTBOUND_QUEUED,
    HIFS_STRIPE_SENT_PARTIAL,
    HIFS_STRIPE_ACKED_PARTIAL,
    HIFS_STRIPE_COMMITTED,
    HIFS_STRIPE_PERSISTING,
    HIFS_STRIPE_PERSISTED,
    HIFS_STRIPE_RECLAIMABLE
};

struct hifs_block_range {
    uint64_t inode_id;
    uint64_t lba_start;
    uint32_t block_count;
};

struct hifs_segment {
    uint64_t seg_id;
    uint32_t curr_block_count;
};

struct hifs_seg_loc {
    uint64_t seg_id;
    uint64_t offset;
    uint32_t length;
};

struct hifs_fragment_target {
    uint32_t frag_idx;
    uint32_t node_id;
};

struct hifs_placement_result {
    uint64_t stripe_id;
    uint32_t target_count;
    struct hifs_fragment_target targets[HIFS_NUM_OF_SRIPES];
};



