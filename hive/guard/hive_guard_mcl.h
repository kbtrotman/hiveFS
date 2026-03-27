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
#include "hive_guard_meta.h"
#include "hive_guard_segmentation.h"

#define HIVE_MCLFILE HIVE_MCL_DIR "/mcl-%llu.log"
#define HIVE_MCL_DIR HIVE_DATA_DIR "/hive_mcl"

#define HIFS_MCL_VERSION       1


#define HIFS_MCL_TYPE "metadata-change-log"
#define HIFS_MCL_END "AT_END_AT_END"

#define HIFS_MCL_MAX_LOGS 7
#define HIFS_MCL_GB (1024ULL * 1024ULL * 1024ULL)
#define HIFS_MCL_MAX_BYTES ((uint64_t)HIFS_MCL_SIZE * HIFS_MCL_GB)
#define HIFS_MCL_MAGIC_VALUE 0xABBADABBUL

#define HIFS_MCL_MEM_MAX_RECORDS 1000u
#define HIFS_MCL_MEM_TABLE_SIZE 2048u
#define HIVE_MCL_ASYNC_QUEUE_MAX 256u

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
    CHANGE_VOLUME_SUPERBLOCK,
    CHANGE_MAP_DELTA,
};

enum hifs_mcl_change_type {
    HIFS_MCL_NEW = 0,
    HIFS_MCL_DELTA_MAP_APPEND = 1,
    HIFS_MCL_DELETE = 2,
    HIFS_MCL_DELTA_STRIPE_REF,
    HIFS_MCL_DELTA_PLACEMENT,
    HIFS_MCL_PERSIST_STATE,
    HIFS_MCL_DELTA_UPDATE
};

struct hifs_mcl_record_prefix {
    uint8_t item;
    uint8_t change_type;
};

struct hifs_mcl_map_delta_rec {
    uint64_t txn_id;
    uint64_t volume_id;
    uint64_t root_dentry_id;
    uint64_t inode_id;        /* or object/inode id once wired in */
    uint64_t dentry_id
    uint64_t block_no;
    uint32_t generation;
    uint32_t block_bytes;
    uint32_t placement_epoch;

    uint8_t  hash_algo;
    uint8_t  stripe_algo;
    uint8_t  reserved[6];

    uint8_t  content_hash[HIFS_BLOCK_HASH_SIZE];
    uint8_t  stripe_id[HIFS_STRIPE_ID_SIZE];
};

struct change_inode {
    uint64_t inode_id;
    struct hifs_inode old_inode;
    struct hifs_inode new_inode;
};

struct change_direntry {
    uint64_t direntry_id;
    struct hifs_direntry old_dentry;
    struct hifs_direntry new_dentry;
};

struct change_root_dentry {
    uint64_t root_dentry_id;
    struct hifs_volume_root_dentry old_root_dentry;
    struct hifs_volume_root_dentry new_root_dentry;
};

struct change_volume {
    uint64_t volume_id;
    struct hifs_volume_superblock old_volume;
    struct hifs_volume_superblock new_volume;
};

struct change_stripe_info_array {
    uint64_t stripe_info_array_id;
    struct HifsEstripeLocations old_stripe_info_array;
    struct HifsEstripeLocations new_stripe_info_array;
};

struct change_segment {
    uint64_t segment_id;
    struct hive_seg_mcl_block_entry old_segment;
    struct hive_seg_mcl_block_entry new_segment;
};

union hive_mcl_payload_max {
    struct hifs_mcl_map_delta_rec map_delta;
    struct change_inode inode;
    struct change_direntry direntry;
    struct change_root_dentry root;
    struct change_volume volume;
    struct change_stripe_info_array stripe_array;
};

#define HIVE_MCL_MAX_PAYLOAD_SIZE sizeof(union hive_mcl_payload_max)

struct hive_mcl_write_req {
    struct hive_mcl_write_req *next;
    struct hifs_mcl_ctx *ctx;
    enum hifs_meta_change_item item;
    enum hifs_mcl_change_type change_type;
    size_t entry_len;
    uint64_t txn_id;
    uint64_t object_id;
    uint8_t payload[HIVE_MCL_MAX_PAYLOAD_SIZE];
};


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

struct hive_mcl_segment_block_ref {
    struct hive_seg_mcl_block_entry block;
    uint64_t inode_key;
    uint64_t dir_inode_key;
    uint64_t root_dentry_id;
    uint64_t volume_sb_epoch;
    const char *direntry_name;
    size_t direntry_name_len;
};

struct hive_mcl_segment_persist_ctx {
    uint64_t segment_id;
    uint64_t txn_id;
    uint32_t generation;
    const struct hive_mcl_segment_block_ref *blocks;
    size_t block_count;
};


/* Prototypes */

struct hifs_mcl_ctx {
    int fd;
    uint64_t next_seqno;
    char path[256];
};

extern struct hifs_mcl_ctx g_hive_guard_mcl_ctx;

int hifs_mcl_open(struct hifs_mcl_ctx *ctx, const char *path, bool create);
int hifs_mcl_close(struct hifs_mcl_ctx *ctx);
int hifs_mcl_append(struct hifs_mcl_ctx *ctx,
                    enum hifs_meta_change_item item,
                    enum hifs_mcl_change_type change_type,
                    const void *entry,
                    size_t entry_len,
                    uint64_t txn_id,
                    uint64_t object_id);
int hifs_mcl_append_inode_change(struct hifs_mcl_ctx *ctx,
                                 enum hifs_mcl_change_type change_type,
                                 uint64_t txn_id,
                                 const struct change_inode *change);
int hifs_mcl_append_direntry_change(struct hifs_mcl_ctx *ctx,
                                    enum hifs_mcl_change_type change_type,
                                    uint64_t txn_id,
                                    const struct change_direntry *change);
int hifs_mcl_append_root_dentry_change(struct hifs_mcl_ctx *ctx,
                                       enum hifs_mcl_change_type change_type,
                                       uint64_t txn_id,
                                       const struct change_root_dentry *change);
int hifs_mcl_append_volume_change(struct hifs_mcl_ctx *ctx,
                                  enum hifs_mcl_change_type change_type,
                                  uint64_t txn_id,
                                  const struct change_volume *change);
int hifs_mcl_append_stripe_info_change(struct hifs_mcl_ctx *ctx,
                                       enum hifs_mcl_change_type change_type,
                                       uint64_t txn_id,
                                       const struct change_stripe_info_array *change);

void hifs_mcl_coalescer_run(struct hifs_mcl_ctx *mcl,
                            struct hifs_meta_ctx *meta);
int hive_mcl_segment_persist_metadata(const struct hive_mcl_segment_persist_ctx *ctx);
