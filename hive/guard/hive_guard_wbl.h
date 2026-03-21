/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

#include <stdbool.h>
#include <pthread.h>
#include <string.h>

#include "../common/hive_common.h"

#define HIVE_WBL_DIR HIVE_DATA_DIR "/hive_wbl"
#define HIVE_WBLFILE HIVE_WBL_DIR "/wbl-%llu.log"

#define HIFS_WBL_VERSION       1

#define HIFS_WBL_TYPE "write-back-log"
#define HIFS_WBL_END "AT_END_AT_END"

#define HIFS_WBL_MAX_LOGS 7
#define HIFS_WBL_GB (1024ULL * 1024ULL * 1024ULL)
#define HIFS_WBL_MAX_BYTES ((uint64_t)HIFS_WBL_SIZE * HIFS_WBL_GB)
#define HIFS_WBL_MAGIC_VALUE 0xABBADABBUL

struct hifs_wbl_hdr {
    uint32_t magic;
    uint64_t version;
    uint16_t type;
    uint32_t length;
    uint64_t seqno;
    uint64_t ts_nsec;
    uint32_t crc32;
};

enum hifs_wbl_rec_type {
    HIFS_WBL_REC_WRITE_RECV = 1,  /* was HIFS_WBL_REC_WRITE_INTENT */
    HIFS_WBL_REC_LANDING_WRITTEN,  /* written to raft */    /* was HIFS_WBL_REC_LANDING_ECCODED */
    HIFS_WBL_REC_STRIPE_PREPARED,  /* EC is done, frags prepared */
    HIFS_WBL_REC_PLACEMENT_ASSIGNED,  /* placement algorithm run*/
    HIFS_WBL_REC_OUTBOUND_QUEUED,     /* Freeze the segment to go to TCP/IP to be transferred */
    HIFS_WBL_REC_FRAGMENT_SENT,      /* written to the foreign node via TCP/IP */
    HIFS_WBL_REC_FRAGMENT_WRITE_INDX,  /* The foreign node has to add it to its cache and indexes and ensure frags are persisted */ /* was HIFS_WBL_REC_FRAGMENT_RECEIVED*/
    HIFS_WBL_REC_FRAGMENT_ACKED,       /* The foreign node has to acknowledge reciept of the frag so the sending node can remove it from cache */
    HIFS_WBL_REC_STRIPE_COMMITTED,     /* Once we reach 16MB, we commit the segment to raft and freeze it from further writes.*/
    HIFS_WBL_REC_STRIPE_PERSISTING,     /* We persist the fragment segment to KV */
    HIFS_WBL_REC_STRIPE_REMOVE_CACHE,    /* was HIFS_WBL_REC_STRIPE_PERSISTED */
    HIFS_WBL_REC_STRIPE_RECLAIMABLE,     /* A segment is returned from KV for a read and the stage marker is set back to HIFS_WBL_REC_STRIPE_COMMITTED */
    HIFS_WBL_REC_CHECKPOINT           /* Need to implement checkpoints here later */
};

struct hifs_wbl_mem_entry {
    uint64_t curr_txn_id;
    uint64_t stripe_id;
    uint64_t inode_id;
    uint32_t state;
    uint32_t generation;

    struct hifs_block_range range;

    struct hifs_seg_loc landing_loc;
    struct hifs_seg_loc outbound_loc;
    struct hifs_seg_loc frag_locs[HIFS_EC_TOTAL_SRIPES];

    struct hifs_fragment_target targets[HIFS_EC_TOTAL_SRIPES];

    uint8_t sent_bitmap;
    uint8_t recv_bitmap;
    uint8_t ack_bitmap;
    uint8_t persist_bitmap;

    uint64_t last_update_ns;
    char block_id[HIFS_OBJ_ID_LEN];
};



// Define these:

#define HIFS_WBL_UUID_TEXT_LEN 33

struct hifs_wbl_write_intent_rec {
    char txn_id[HIFS_WBL_UUID_TEXT_LEN];
    char stripe_id[HIFS_WBL_UUID_TEXT_LEN];
    uint64_t inode_id;
    uint32_t generation;
    struct hifs_block_range range;
    char block_id[HIFS_OBJ_ID_LEN];
};

struct hifs_wbl_landing_rec {
    char txn_id[HIFS_WBL_UUID_TEXT_LEN];
    char stripe_id[HIFS_WBL_UUID_TEXT_LEN];
    struct hifs_seg_loc landing_loc;
};

struct hifs_wbl_prepared_rec {
    char txn_id[HIFS_WBL_UUID_TEXT_LEN];
    char stripe_id[HIFS_WBL_UUID_TEXT_LEN];
    uint32_t generation;
    struct hifs_seg_loc frag_locs[HIFS_EC_TOTAL_SRIPES];
};

struct hifs_wbl_placement_rec {
    char txn_id[HIFS_WBL_UUID_TEXT_LEN];
    char stripe_id[HIFS_WBL_UUID_TEXT_LEN];
    struct hifs_fragment_target targets[HIFS_EC_TOTAL_SRIPES];
};

struct hifs_wbl_outbound_queued_rec {
    char txn_id[HIFS_WBL_UUID_TEXT_LEN];
    char stripe_id[HIFS_WBL_UUID_TEXT_LEN];
    uint32_t node_id;
    struct hifs_seg_loc frag_loc;
};

struct hifs_wbl_fragment_event_rec {
    char txn_id[HIFS_WBL_UUID_TEXT_LEN];
    char stripe_id[HIFS_WBL_UUID_TEXT_LEN];
    uint32_t frag_idx;
    uint32_t node_id;
};

struct hifs_wbl_commit_rec {
    char txn_id[HIFS_WBL_UUID_TEXT_LEN];
    char stripe_id[HIFS_WBL_UUID_TEXT_LEN];
    uint8_t ack_bitmap;
};

struct hifs_wbl_persisting_rec {
    char txn_id[HIFS_WBL_UUID_TEXT_LEN];
    char stripe_id[HIFS_WBL_UUID_TEXT_LEN];
    uint8_t persist_bitmap;
};

struct hifs_wbl_persisted_rec {
    char txn_id[HIFS_WBL_UUID_TEXT_LEN];
    char stripe_id[HIFS_WBL_UUID_TEXT_LEN];
    uint8_t persist_bitmap;
};

#define HIFS_WBL_RECLAIMABLE_F_UNCHANGED 0x01u

struct hifs_wbl_reclaimable_rec {
    char txn_id[HIFS_WBL_UUID_TEXT_LEN];
    char stripe_id[HIFS_WBL_UUID_TEXT_LEN];
    uint8_t flags;
    uint8_t reserved[7];
};

struct hifs_wbl_ctx {
    int fd;
    uint64_t next_seqno;
    char path[256];
    pthread_mutex_t mu;
    bool mu_inited;
};

extern struct hifs_wbl_ctx g_hive_guard_wbl_ctx;

/* Prototypes */

int hifs_wbl_open(struct hifs_wbl_ctx *ctx, const char *path, bool create);
int hifs_wbl_close(struct hifs_wbl_ctx *ctx);
int hifs_wbl_append(struct hifs_wbl_ctx *ctx,
                    uint16_t rec_type,
                    const void *payload,
                    uint32_t payload_len);

int hifs_wbl_replay(struct hifs_wbl_ctx *ctx,
                    int (*apply_fn)(const struct hifs_wbl_hdr *hdr,
                                    const void *payload,
                                    void *arg),
                    void *arg);
int hifs_wbl_checkpoint(...);
int hifs_wbl_mark_write_intent(struct hifs_wbl_ctx *ctx,
                               const struct hifs_wbl_write_intent_rec *rec);
int hifs_wbl_mark_landing_eccoded(struct hifs_wbl_ctx *ctx,
                                  const struct hifs_wbl_landing_rec *rec);
int hifs_wbl_mark_stripe_prepared(struct hifs_wbl_ctx *ctx,
                                  const struct hifs_wbl_prepared_rec *rec);
int hifs_wbl_mark_placement_assigned(struct hifs_wbl_ctx *ctx,
                                     const struct hifs_wbl_placement_rec *rec);
int hifs_wbl_mark_outbound_queued(struct hifs_wbl_ctx *ctx,
                                  const struct hifs_wbl_outbound_queued_rec *rec);
int hifs_wbl_mark_fragment_sent(struct hifs_wbl_ctx *ctx,
                                const struct hifs_wbl_fragment_event_rec *rec);
int hifs_wbl_mark_fragment_received(struct hifs_wbl_ctx *ctx,
                                    const struct hifs_wbl_fragment_event_rec *rec);
int hifs_wbl_mark_fragment_acked(struct hifs_wbl_ctx *ctx,
                                 const struct hifs_wbl_fragment_event_rec *rec);
int hifs_wbl_mark_committed(struct hifs_wbl_ctx *ctx,
                            const struct hifs_wbl_commit_rec *rec);
int hifs_wbl_mark_persisting(struct hifs_wbl_ctx *ctx,
                             const struct hifs_wbl_persisting_rec *rec);
int hifs_wbl_mark_persisted(struct hifs_wbl_ctx *ctx,
                            const struct hifs_wbl_persisted_rec *rec);
int hifs_wbl_mark_reclaimable(struct hifs_wbl_ctx *ctx,
                              const struct hifs_wbl_reclaimable_rec *rec);
