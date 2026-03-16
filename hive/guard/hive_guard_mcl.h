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


#define HIVE_MCLFILE HIVE_MCL_DIR "/mcl-%llu.log"
#define HIVE_MCL_DIR HIVE_DATA_DIR "/hive_mcl"

struct hifs_mcl_hdr {
    uint32_t magic;
    uint16_t version;
    uint16_t type;
    uint32_t length;
    uint64_t seqno;
    uint64_t txn_id;
    uint64_t object_id;
    uint32_t crc32;
};

enum hifs_mcl_rec_type {
    HIFS_MCL_INODE_HOT_UPDATE = 1,
    HIFS_MCL_MAP_DELTA_APPEND,
    HIFS_MCL_DIR_DELTA_APPEND,
    HIFS_MCL_STRIPE_REF_UPDATE,
    HIFS_MCL_PERSIST_STATE_UPDATE,
    HIFS_MCL_CACHE_STATE_UPDATE,
    HIFS_MCL_COALESCE_CHECKPOINT
};

enum change_type {
    CHANGE_INODE,
    CHANGE_MAP,
    CHANGE_DIR,
    CHANGE_STRIPE_REF,
    CHANGE_PERSIST_STATE,
    CHANGE_CACHE_STATE,
    CHANGE_SIZE,
    CHANGE_DELETE
};

struct hifs_mcl_mem_entry {
    enum change_type type;
    uint64_t stripe_id[HIFS_EC_TOTAL_SRIPES];  /* for stripe ref updates */
    uint64_t txn_id;
    uint32_t state;
    uint32_t node_mask;
    uint64_t landing_seg_id;
    uint64_t landing_off;
    uint64_t stripe_seg_id;
    uint64_t stripe_off;
    uint64_t last_update_ns;
    char block_id[HIFS_OBJ_ID_LEN];
    char inode_id[HIFS_OBJ_ID_LEN];
};


/* Prototypes */

int hifs_mcl_open(...);
int hifs_mcl_close(...);
int hifs_mcl_append(...);
int hifs_mcl_replay(...);
int hifs_mcl_checkpoint(...);
int hifs_mcl_mark_placement_assigned(...);
int hifs_mcl_mark_fragment_sent(...);
int hifs_mcl_mark_committed(...);