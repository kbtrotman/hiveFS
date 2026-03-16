/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

 #include <string.h>

#include "hive_guard_erasure_code.h"
#include "../common/hive_common.h"
#include "../../linux_lkm/hifs_shared_defs.h"

#define HIVE_MCLFILE HIVE_MCL_DIR "/mcl-%llu.log"
#define HIVE_MCL_DIR HIVE_DATA_DIR "/hive_mcl"

#define HIFS_MCL_VERSION       1


#define HIFS_MCL_TYPE "metadata-change-log"
#define HIFS_MCL_END "AT_END_AT_END"

#define HIFS_MCL_MAX_LOGS 7
#define HIFS_MCL_GB (1024ULL * 1024ULL * 1024ULL)
#define HIFS_MCL_MAX_BYTES ((uint64_t)HIFS_MCL_SIZE * HIFS_MCL_GB)
#define HIFS_MCL_MAGIC_VALUE 0xABBADABBUL

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

enum hifs_meta_change_item {
    CHANGE_INODE,
    CHANGE_DIRENTRY,
    CHANGE_STRIPE_INFO,
    CHANGE_ROOT_DIRENTRY,
    CHANGE_VOLUME_SUPERBLOCK
};

enum hifs_mcl_change_type {
    HIFS_MCL_UPDATE = 1,
    HIFS_MCL_NEW,
    HIFS_MCL_DELETE,
    HIFS_MCL_STRIPE_ADD,
    HIFS_MCL_STATE_UPDATE,
    HIFS_MCL_TIME_UPDATE,
    HIFS_MCL_CHECKPOINT
};

struct hifs_mcl_record_prefix {
    uint8_t item;
    uint8_t change_type;
}

struct hifs_mcl_mem_entry {
    enum hifs_mcl_change_type type;
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

struct hifs_mcl_ctx {
    int fd;
    uint64_t next_seqno;
    char path[256];
};

int hifs_mcl_open(struct hifs_mcl_ctx *ctx, const char *path, bool create);
int hifs_mcl_close(struct hifs_mcl_ctx *ctx);
int hifs_mcl_append(struct hifs_mcl_ctx *ctx,
                    enum hifs_meta_change_item item,
                    enum hifs_mcl_change_type change_type,
                    const void *entry,
                    size_t entry_len,
                    uint64_t txn_id,
                    uint64_t object_id);
