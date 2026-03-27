/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>

#include "hive_guard_mcl.h"
#include "hive_guard_raft.h"



static pthread_mutex_t g_mcl_async_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_mcl_async_cv = PTHREAD_COND_INITIALIZER;
static pthread_cond_t g_mcl_async_space_cv = PTHREAD_COND_INITIALIZER;
static pthread_cond_t g_mcl_async_idle_cv = PTHREAD_COND_INITIALIZER;
static struct hive_mcl_write_req g_mcl_async_pool[HIVE_MCL_ASYNC_QUEUE_MAX];
static struct hive_mcl_write_req *g_mcl_async_free_list;
static struct hive_mcl_write_req *g_mcl_async_head;
static struct hive_mcl_write_req *g_mcl_async_tail;
static size_t g_mcl_async_depth;
static size_t g_mcl_async_inflight;
static int g_mcl_async_fatal_err;
static pthread_once_t g_mcl_async_once = PTHREAD_ONCE_INIT;
static pthread_t g_mcl_async_thread;
static bool g_mcl_async_thread_started;
static int g_mcl_async_thread_err;

static void hive_mcl_async_start_once(void);
static int hive_mcl_async_ensure_thread(void);
static void *hive_mcl_async_worker(void *arg);
static int hive_mcl_async_queue_record(struct hifs_mcl_ctx *ctx,
                                       enum hifs_meta_change_item item,
                                       enum hifs_mcl_change_type change_type,
                                       const void *entry,
                                       size_t entry_len,
                                       uint64_t txn_id,
                                       uint64_t object_id);
static void hive_mcl_async_wait_idle(void);
static int hive_mcl_write_record(struct hifs_mcl_ctx *ctx,
                                 enum hifs_meta_change_item item,
                                 enum hifs_mcl_change_type change_type,
                                 const void *entry,
                                 size_t entry_len,
                                 uint64_t txn_id,
                                 uint64_t object_id);

enum hive_mcl_index_kind {
    HIVE_MCL_KEY_MAP_DELTA,
    HIVE_MCL_KEY_INODE,
    HIVE_MCL_KEY_DIR,
};

enum hive_mcl_slot_state {
    HIVE_MCL_SLOT_EMPTY = 0,
    HIVE_MCL_SLOT_OCCUPIED,
    HIVE_MCL_SLOT_TOMBSTONE,
};

enum hive_mcl_delta_flags {
    HIVE_MCL_INDEX_FLAG_DIRTY     = 1u << 0,
    HIVE_MCL_INDEX_FLAG_APPLIED   = 1u << 1,
    HIVE_MCL_INDEX_FLAG_OBSOLETE  = 1u << 2,
};

struct hive_mcl_index_key {
    enum hive_mcl_index_kind kind;
    union {
        struct {
            uint64_t inode_key;
            uint64_t block_no;
        } map;
        struct {
            uint64_t inode_key;
        } inode;
        struct {
            uint64_t dir_inode_key;
            uint16_t name_len;
            char name[HIFS_MAX_NAME_SIZE];
        } dir;
    } u;
};

union hive_mcl_mem_payload {
    struct hifs_mcl_map_delta_rec map_delta;
};

struct hive_mcl_mem_table_entry {
    struct hive_mcl_index_key key;
    union hive_mcl_mem_payload payload;
    enum hifs_meta_change_item item;
    enum hifs_mcl_change_type change_type;
    uint64_t txn_id;
    uint32_t generation;
    uint64_t last_update_ns;
    uint32_t flags;
    enum hive_mcl_slot_state slot_state;
    uint64_t hash;
};

static struct hive_mcl_mem_table_entry g_mcl_mem_table[HIFS_MCL_MEM_TABLE_SIZE];
static size_t g_mcl_mem_count;
static pthread_mutex_t g_mcl_mem_mu = PTHREAD_MUTEX_INITIALIZER;

struct hifs_mcl_pending_delta {
    uint64_t volume_id;
    uint64_t inode_key;
    uint64_t block_no;
    uint32_t generation;
    uint64_t txn_id;
    uint8_t content_hash[HIFS_BLOCK_HASH_SIZE];
    uint8_t stripe_id[HIFS_STRIPE_ID_SIZE];
};

struct hive_mcl_metadata_packet {
    struct hive_seg_mcl_block_entry block;
    uint64_t inode_key;
    uint64_t dir_inode_key;
    uint64_t root_dentry_id;
    uint64_t volume_sb_epoch;
    uint64_t txn_id;
    uint32_t generation;
    bool has_delta;
    struct hifs_mcl_map_delta_rec delta;
    hifs_object_id_t block_id;
    hifs_object_id_t map_id;
    hifs_object_id_t direntry_id;
    hifs_object_id_t inode_id;
    hifs_object_id_t root_id;
    hifs_object_id_t volume_id;
};

static void hive_mcl_async_start_once(void)
{
    for (size_t i = 0; i < HIVE_MCL_ASYNC_QUEUE_MAX; i++) {
        g_mcl_async_pool[i].next = g_mcl_async_free_list;
        g_mcl_async_free_list = &g_mcl_async_pool[i];
    }

    int rc = pthread_create(&g_mcl_async_thread, NULL, hive_mcl_async_worker, NULL);
    if (rc == 0) {
        g_mcl_async_thread_started = true;
        g_mcl_async_thread_err = 0;
    } else {
        g_mcl_async_thread_started = false;
        g_mcl_async_thread_err = rc;
    }
}

static int hive_mcl_async_ensure_thread(void)
{
    pthread_once(&g_mcl_async_once, hive_mcl_async_start_once);
    if (!g_mcl_async_thread_started) {
        errno = g_mcl_async_thread_err ? g_mcl_async_thread_err : EFAULT;
        return -1;
    }
    return 0;
}

static void *hive_mcl_async_worker(void *arg)
{
    (void)arg;

    for (;;) {
        pthread_mutex_lock(&g_mcl_async_mu);
        while (!g_mcl_async_head) {
            pthread_cond_wait(&g_mcl_async_cv, &g_mcl_async_mu);
        }

        struct hive_mcl_write_req *req = g_mcl_async_head;
        g_mcl_async_head = req->next;
        if (!g_mcl_async_head) {
            g_mcl_async_tail = NULL;
        }
        g_mcl_async_depth--;
        g_mcl_async_inflight++;
        pthread_mutex_unlock(&g_mcl_async_mu);

        int rc = hive_mcl_write_record(req->ctx,
                                       req->item,
                                       req->change_type,
                                       req->payload,
                                       req->entry_len,
                                       req->txn_id,
                                       req->object_id);
        int err = (rc == 0) ? 0 : (errno ? errno : EIO);

        pthread_mutex_lock(&g_mcl_async_mu);
        g_mcl_async_inflight--;
        req->next = g_mcl_async_free_list;
        g_mcl_async_free_list = req;
        pthread_cond_signal(&g_mcl_async_space_cv);
        if (g_mcl_async_depth == 0 && g_mcl_async_inflight == 0) {
            pthread_cond_broadcast(&g_mcl_async_idle_cv);
        }
        if (rc != 0 && g_mcl_async_fatal_err == 0) {
            g_mcl_async_fatal_err = err ? err : EIO;
        }
        pthread_mutex_unlock(&g_mcl_async_mu);
    }

    return NULL;
}

static int hive_mcl_async_queue_record(struct hifs_mcl_ctx *ctx,
                                       enum hifs_meta_change_item item,
                                       enum hifs_mcl_change_type change_type,
                                       const void *entry,
                                       size_t entry_len,
                                       uint64_t txn_id,
                                       uint64_t object_id)
{
    struct hive_mcl_write_req *req;

    if (entry_len > HIVE_MCL_MAX_PAYLOAD_SIZE) {
        errno = EOVERFLOW;
        return -1;
    }

    pthread_mutex_lock(&g_mcl_async_mu);
    while (!g_mcl_async_free_list && g_mcl_async_fatal_err == 0) {
        pthread_cond_wait(&g_mcl_async_space_cv, &g_mcl_async_mu);
    }

    if (g_mcl_async_fatal_err != 0) {
        errno = g_mcl_async_fatal_err;
        pthread_mutex_unlock(&g_mcl_async_mu);
        return -1;
    }

    req = g_mcl_async_free_list;
    g_mcl_async_free_list = req->next;
    pthread_mutex_unlock(&g_mcl_async_mu);

    req->ctx = ctx;
    req->item = item;
    req->change_type = change_type;
    req->entry_len = entry_len;
    req->txn_id = txn_id;
    req->object_id = object_id;
    if (entry_len) {
        memcpy(req->payload, entry, entry_len);
    }
    req->next = NULL;

    pthread_mutex_lock(&g_mcl_async_mu);
    if (g_mcl_async_fatal_err != 0) {
        req->next = g_mcl_async_free_list;
        g_mcl_async_free_list = req;
        pthread_cond_signal(&g_mcl_async_space_cv);
        errno = g_mcl_async_fatal_err;
        pthread_mutex_unlock(&g_mcl_async_mu);
        return -1;
    }

    if (g_mcl_async_tail) {
        g_mcl_async_tail->next = req;
    } else {
        g_mcl_async_head = req;
    }
    g_mcl_async_tail = req;
    g_mcl_async_depth++;
    pthread_cond_signal(&g_mcl_async_cv);
    pthread_mutex_unlock(&g_mcl_async_mu);

    return 0;
}

static void hive_mcl_async_wait_idle(void)
{
    if (!g_mcl_async_thread_started) {
        return;
    }

    pthread_mutex_lock(&g_mcl_async_mu);
    while (g_mcl_async_depth > 0 || g_mcl_async_inflight > 0) {
        pthread_cond_wait(&g_mcl_async_idle_cv, &g_mcl_async_mu);
    }
    pthread_mutex_unlock(&g_mcl_async_mu);
}

static inline uint64_t hive_mcl_mix64(uint64_t x);
static inline uint64_t hive_mcl_hash_bytes(const void *data, size_t len, uint64_t seed);
static struct hive_mcl_mem_table_entry *
hive_mcl_mem_index_lookup_slot_locked(const struct hive_mcl_index_key *key,
                                      bool create,
                                      bool *is_new);
static inline struct hive_mcl_index_key hive_mcl_make_map_key(uint64_t inode_key, uint64_t block_no);

static void hive_mcl_fill_object_id(uint64_t seed, hifs_object_id_t *out)
{
    for (size_t i = 0; i < HIFS_META_OBJECT_ID_SIZE; i += sizeof(seed)) {
        seed = hive_mcl_mix64(seed + (uint64_t)i);
        memcpy(out->bytes + i, &seed, sizeof(seed));
    }
}

static uint64_t hive_mcl_seed_from_components(uint64_t a,
                                              uint64_t b,
                                              const void *extra,
                                              size_t extra_len)
{
    uint64_t seed = 0x768f543fa32b19d7ull;

    seed = hive_mcl_hash_bytes(&a, sizeof(a), seed);
    seed = hive_mcl_hash_bytes(&b, sizeof(b), seed);
    if (extra && extra_len) {
        seed = hive_mcl_hash_bytes(extra, extra_len, seed);
    }

    return seed;
}

static void hive_mcl_build_ids_from_block(const struct hive_mcl_segment_block_ref *ref,
                                          struct hive_mcl_metadata_packet *pkt)
{
    const struct hive_seg_mcl_block_entry *blk = &ref->block;
    const char *name = ref->direntry_name ? ref->direntry_name : "";
    const size_t name_len = ref->direntry_name ? ref->direntry_name_len : 0u;

    uint64_t seed;

    seed = hive_mcl_seed_from_components(blk->volume_id,
                                         blk->block_no,
                                         &ref->inode_key,
                                         sizeof(ref->inode_key));
    hive_mcl_fill_object_id(seed, &pkt->block_id);

    seed = hive_mcl_seed_from_components(ref->inode_key,
                                         blk->block_no,
                                         &ref->volume_sb_epoch,
                                         sizeof(ref->volume_sb_epoch));
    hive_mcl_fill_object_id(seed, &pkt->map_id);

    seed = hive_mcl_seed_from_components(ref->dir_inode_key,
                                         ref->inode_key,
                                         name,
                                         name_len);
    hive_mcl_fill_object_id(seed, &pkt->direntry_id);

    seed = hive_mcl_seed_from_components(ref->inode_key,
                                         pkt->generation,
                                         blk->hash,
                                         sizeof(blk->hash));
    hive_mcl_fill_object_id(seed, &pkt->inode_id);

    seed = hive_mcl_seed_from_components(ref->root_dentry_id,
                                         ref->dir_inode_key,
                                         &ref->volume_sb_epoch,
                                         sizeof(ref->volume_sb_epoch));
    hive_mcl_fill_object_id(seed, &pkt->root_id);

    seed = hive_mcl_seed_from_components(blk->volume_id,
                                         ref->volume_sb_epoch,
                                         blk->stripe_id,
                                         sizeof(blk->stripe_id));
    hive_mcl_fill_object_id(seed, &pkt->volume_id);
}

static void hive_mcl_capture_map_delta_locked(uint64_t inode_key,
                                              uint64_t block_no,
                                              struct hive_mcl_metadata_packet *pkt)
{
    struct hive_mcl_index_key key = hive_mcl_make_map_key(inode_key, block_no);
    struct hive_mcl_mem_table_entry *entry =
        hive_mcl_mem_index_lookup_slot_locked(&key, false, NULL);

    if (!entry || entry->item != CHANGE_MAP_DELTA) {
        pkt->has_delta = false;
        memset(&pkt->delta, 0, sizeof(pkt->delta));
        return;
    }

    pkt->delta = entry->payload.map_delta;
    pkt->has_delta = true;
}

static int hive_mcl_metadata_batch_write(const struct hive_mcl_metadata_packet *packets,
                                         size_t packet_count)
{
    (void)packets;
    (void)packet_count;

    /* Placeholder for future hive_guard_kv.c integration. */
    return 0;
}

static int hive_mcl_dispatch_metadata_to_cluster(const struct hive_mcl_metadata_packet *packets,
                                                 size_t packet_count)
{
    (void)packets;
    (void)packet_count;

    /* Future: send packets via hive_guard_sn_tcp.c to peers and await ACKs. */
    return 0;
}

static int hive_mcl_publish_metadata_finalized(uint64_t segment_id,
                                               uint64_t txn_id,
                                               uint32_t generation,
                                               size_t block_count)
{
    struct RaftCmd cmd = {0};
    struct RaftClusterAudit *audit = &cmd.u.cluster_audit;
    struct timespec ts;

    cmd.op_type = HG_OP_PUT_CLUSTER_AUDIT;
    audit->cluster_id = hbc.cluster_id;
    audit->node_id = hbc.storage_node_id;
    clock_gettime(CLOCK_REALTIME, &ts);
    audit->timestamp_ns = (uint64_t)ts.tv_sec * 1000000000ull +
                          (uint64_t)ts.tv_nsec;
    snprintf(audit->message,
             sizeof(audit->message),
             "metadata-finalized seg=%llu txn=%llu gen=%u blocks=%zu",
             (unsigned long long)segment_id,
             (unsigned long long)txn_id,
             generation,
             block_count);

    if (hg_raft_submit_cmd_sync(&cmd, NULL) != 0) {
        errno = EIO;
        return -1;
    }

    return 0;
}

int hive_mcl_segment_persist_metadata(const struct hive_mcl_segment_persist_ctx *ctx)
{
    struct hive_mcl_metadata_packet *packets = NULL;
    int rc = -1;

    if (!ctx || !ctx->blocks || ctx->block_count == 0) {
        errno = EINVAL;
        return -1;
    }

    packets = calloc(ctx->block_count, sizeof(*packets));
    if (!packets) {
        return -1;
    }

    for (size_t i = 0; i < ctx->block_count; i++) {
        const struct hive_mcl_segment_block_ref *ref = &ctx->blocks[i];
        struct hive_mcl_metadata_packet *pkt = &packets[i];

        pkt->block = ref->block;
        pkt->inode_key = ref->inode_key;
        pkt->dir_inode_key = ref->dir_inode_key;
        pkt->root_dentry_id = ref->root_dentry_id;
        pkt->volume_sb_epoch = ref->volume_sb_epoch;
        pkt->txn_id = ctx->txn_id;
        pkt->generation = ctx->generation;

        hive_mcl_build_ids_from_block(ref, pkt);
    }

    pthread_mutex_lock(&g_mcl_mem_mu);
    for (size_t i = 0; i < ctx->block_count; i++) {
        hive_mcl_capture_map_delta_locked(ctx->blocks[i].inode_key,
                                          ctx->blocks[i].block.block_no,
                                          &packets[i]);
    }
    pthread_mutex_unlock(&g_mcl_mem_mu);

    rc = hive_mcl_metadata_batch_write(packets, ctx->block_count);
    if (rc == 0) {
        rc = hive_mcl_dispatch_metadata_to_cluster(packets, ctx->block_count);
    }
    if (rc == 0) {
        rc = hive_mcl_publish_metadata_finalized(ctx->segment_id,
                                                 ctx->txn_id,
                                                 ctx->generation,
                                                 ctx->block_count);
    }

    free(packets);
    return rc;
}

static int hive_mcl_append_change_record(struct hifs_mcl_ctx *ctx,
                                         enum hifs_meta_change_item item,
                                         enum hifs_mcl_change_type change_type,
                                         uint64_t txn_id,
                                         uint64_t object_id,
                                         const void *record,
                                         size_t record_len)
{
    if (!record) {
        errno = EINVAL;
        return -1;
    }

    if (!ctx) {
        ctx = &g_hive_guard_mcl_ctx;
    }

    return hifs_mcl_append(ctx,
                           item,
                           change_type,
                           record,
                           record_len,
                           txn_id,
                           object_id);
}

int hifs_mcl_append_inode_change(struct hifs_mcl_ctx *ctx,
                                 enum hifs_mcl_change_type change_type,
                                 uint64_t txn_id,
                                 const struct change_inode *change)
{
    if (!change) {
        errno = EINVAL;
        return -1;
    }

    return hive_mcl_append_change_record(ctx,
                                         CHANGE_INODE,
                                         change_type,
                                         txn_id,
                                         change->inode_id,
                                         change,
                                         sizeof(*change));
}

int hifs_mcl_append_direntry_change(struct hifs_mcl_ctx *ctx,
                                    enum hifs_mcl_change_type change_type,
                                    uint64_t txn_id,
                                    const struct change_direntry *change)
{
    if (!change) {
        errno = EINVAL;
        return -1;
    }

    return hive_mcl_append_change_record(ctx,
                                         CHANGE_DIRENTRY,
                                         change_type,
                                         txn_id,
                                         change->direntry_id,
                                         change,
                                         sizeof(*change));
}

int hifs_mcl_append_root_dentry_change(struct hifs_mcl_ctx *ctx,
                                       enum hifs_mcl_change_type change_type,
                                       uint64_t txn_id,
                                       const struct change_root_dentry *change)
{
    if (!change) {
        errno = EINVAL;
        return -1;
    }

    return hive_mcl_append_change_record(ctx,
                                         CHANGE_ROOT_DIRENTRY,
                                         change_type,
                                         txn_id,
                                         change->root_dentry_id,
                                         change,
                                         sizeof(*change));
}

int hifs_mcl_append_volume_change(struct hifs_mcl_ctx *ctx,
                                  enum hifs_mcl_change_type change_type,
                                  uint64_t txn_id,
                                  const struct change_volume *change)
{
    if (!change) {
        errno = EINVAL;
        return -1;
    }

    return hive_mcl_append_change_record(ctx,
                                         CHANGE_VOLUME_SUPERBLOCK,
                                         change_type,
                                         txn_id,
                                         change->volume_id,
                                         change,
                                         sizeof(*change));
}

int hifs_mcl_append_stripe_info_change(struct hifs_mcl_ctx *ctx,
                                       enum hifs_mcl_change_type change_type,
                                       uint64_t txn_id,
                                       const struct change_stripe_info_array *change)
{
    if (!change) {
        errno = EINVAL;
        return -1;
    }

    return hive_mcl_append_change_record(ctx,
                                         CHANGE_STRIPE_INFO,
                                         change_type,
                                         txn_id,
                                         change->stripe_info_array_id,
                                         change,
                                         sizeof(*change));
}
static bool hive_mcl_entry_is_pending_map_delta(const struct hive_mcl_mem_table_entry *entry)
{
    if (!entry || entry->slot_state != HIVE_MCL_SLOT_OCCUPIED) {
        return false;
    }

    if (entry->item != CHANGE_MAP_DELTA) {
        return false;
    }

    if (!(entry->flags & HIVE_MCL_INDEX_FLAG_DIRTY)) {
        return false;
    }

    if (entry->flags & HIVE_MCL_INDEX_FLAG_OBSOLETE) {
        return false;
    }

    return true;
}

static size_t hive_mcl_collect_pending_deltas(struct hifs_mcl_pending_delta **out)
{
    struct hifs_mcl_pending_delta *deltas = NULL;
    size_t count = 0;
    size_t idx = 0;

    if (out) {
        *out = NULL;
    }

    pthread_mutex_lock(&g_mcl_mem_mu);
    for (size_t i = 0; i < HIFS_MCL_MEM_TABLE_SIZE; i++) {
        if (hive_mcl_entry_is_pending_map_delta(&g_mcl_mem_table[i])) {
            count++;
        }
    }

    if (count == 0) {
        pthread_mutex_unlock(&g_mcl_mem_mu);
        return 0;
    }

    deltas = malloc(count * sizeof(*deltas));
    if (!deltas) {
        pthread_mutex_unlock(&g_mcl_mem_mu);
        return 0;
    }

    for (size_t i = 0; i < HIFS_MCL_MEM_TABLE_SIZE; i++) {
        struct hive_mcl_mem_table_entry *entry = &g_mcl_mem_table[i];

        if (!hive_mcl_entry_is_pending_map_delta(entry)) {
            continue;
        }

        deltas[idx].volume_id = entry->payload.map_delta.volume_id;
        deltas[idx].inode_key = entry->key.u.map.inode_key;
        deltas[idx].block_no = entry->key.u.map.block_no;
        deltas[idx].generation = entry->payload.map_delta.generation;
        deltas[idx].txn_id = entry->payload.map_delta.txn_id;
        memcpy(deltas[idx].content_hash,
               entry->payload.map_delta.content_hash,
               sizeof(deltas[idx].content_hash));
        memcpy(deltas[idx].stripe_id,
               entry->payload.map_delta.stripe_id,
               sizeof(deltas[idx].stripe_id));
        idx++;
    }
    pthread_mutex_unlock(&g_mcl_mem_mu);

    if (out) {
        *out = deltas;
    } else {
        free(deltas);
    }

    return idx;
}

static int hive_mcl_pending_delta_cmp(const void *a, const void *b)
{
    const struct hifs_mcl_pending_delta *da = a;
    const struct hifs_mcl_pending_delta *db = b;

    if (da->volume_id < db->volume_id) {
        return -1;
    }
    if (da->volume_id > db->volume_id) {
        return 1;
    }
    if (da->inode_key < db->inode_key) {
        return -1;
    }
    if (da->inode_key > db->inode_key) {
        return 1;
    }
    if (da->block_no < db->block_no) {
        return -1;
    }
    if (da->block_no > db->block_no) {
        return 1;
    }
    if (da->generation > db->generation) {
        return -1;
    }
    if (da->generation < db->generation) {
        return 1;
    }
    if (da->txn_id < db->txn_id) {
        return -1;
    }
    if (da->txn_id > db->txn_id) {
        return 1;
    }
    return 0;
}

static void hive_mcl_mem_index_mark_map_delta_applied(const struct hifs_mcl_pending_delta *delta)
{
    struct hive_mcl_index_key key;
    struct hive_mcl_mem_table_entry *entry;

    if (!delta) {
        return;
    }

    key = hive_mcl_make_map_key(delta->inode_key, delta->block_no);

    pthread_mutex_lock(&g_mcl_mem_mu);
    entry = hive_mcl_mem_index_lookup_slot_locked(&key, false, NULL);
    if (entry &&
        entry->payload.map_delta.generation == delta->generation &&
        entry->payload.map_delta.txn_id == delta->txn_id) {
        entry->flags &= ~HIVE_MCL_INDEX_FLAG_DIRTY;
        entry->flags |= HIVE_MCL_INDEX_FLAG_APPLIED;
        entry->last_update_ns = hive_mcl_now_ns();
    }
    pthread_mutex_unlock(&g_mcl_mem_mu);
}

void hifs_mcl_coalescer_run(struct hifs_mcl_ctx *mcl,
                            struct hifs_meta_ctx *meta)
{
    struct hifs_mcl_pending_delta *deltas = NULL;
    size_t delta_count;

    if (!mcl || !meta) {
        return;
    }

    delta_count = hive_mcl_collect_pending_deltas(&deltas);
    if (delta_count == 0 || !deltas) {
        free(deltas);
        return;
    }

    qsort(deltas, delta_count, sizeof(*deltas), hive_mcl_pending_delta_cmp);

    for (size_t i = 0; i < delta_count;) {
        const uint64_t volume_id = deltas[i].volume_id;
        const uint64_t inode_key = deltas[i].inode_key;
        size_t group_start = i;
        struct hifs_meta_map_builder builder = {0};
        bool builder_ok = hifs_meta_map_begin(meta, volume_id, inode_key, &builder) == 0;

        while (i < delta_count &&
               deltas[i].volume_id == volume_id &&
               deltas[i].inode_key == inode_key) {
            if (builder_ok) {
                if (hifs_meta_map_apply_delta(&builder,
                                              deltas[i].block_no,
                                              deltas[i].content_hash,
                                              deltas[i].stripe_id,
                                              deltas[i].generation) != 0) {
                    builder_ok = false;
                }
            }
            i++;
        }

        size_t group_end = i;

        if (builder_ok) {
            if (hifs_meta_map_commit(meta, &builder, NULL) == 0) {
                for (size_t j = group_start; j < group_end; j++) {
                    hive_mcl_mem_index_mark_map_delta_applied(&deltas[j]);
                }
            } else {
                builder_ok = false;
            }
        }

        free(builder.deltas);
        builder.deltas = NULL;
        builder.delta_count = 0;
        builder.delta_cap = 0;
    }

    free(deltas);
    (void)mcl;
}

int hifs_mcl_replay_record(const struct hifs_mcl_hdr *hdr,
                           const void *payload,
                           void *arg)
{
    const struct hifs_mcl_record_prefix *prefix;
    const uint8_t *rec_bytes;
    size_t rec_len;

    (void)arg;

    if (!hdr || !payload) {
        errno = EINVAL;
        return -1;
    }

    if (hdr->length < sizeof(*prefix)) {
        errno = EINVAL;
        return -1;
    }

    prefix = payload;
    rec_bytes = (const uint8_t *)payload + sizeof(*prefix);
    rec_len = (size_t)hdr->length - sizeof(*prefix);

    switch ((enum hifs_meta_change_item)prefix->item) {
    case CHANGE_MAP_DELTA: {
        const struct hifs_mcl_map_delta_rec *rec;
        enum hifs_mcl_change_type change_type;

        if (rec_len != sizeof(struct hifs_mcl_map_delta_rec)) {
            errno = EINVAL;
            return -1;
        }

        rec = (const void *)rec_bytes;
        change_type = (enum hifs_mcl_change_type)prefix->change_type;

        hive_mcl_mem_index_track_map_delta(rec, change_type);
        break;
    }
    default:
        break;
    }

    return 0;
}

static inline uint64_t hive_mcl_mix64(uint64_t x)
{
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccduLL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53uLL;
    x ^= x >> 33;
    return x;
}

static inline uint64_t hive_mcl_hash_bytes(const void *data, size_t len, uint64_t seed)
{
    const uint8_t *p = data;

    for (size_t i = 0; i < len; i++) {
        seed ^= (uint64_t)p[i] + 0x9e3779b185ebca87ull + (seed << 6) + (seed >> 2);
    }

    return hive_mcl_mix64(seed);
}

static inline struct hive_mcl_index_key hive_mcl_make_map_key(uint64_t inode_key, uint64_t block_no)
{
    struct hive_mcl_index_key key = {0};

    key.kind = HIVE_MCL_KEY_MAP_DELTA;
    key.u.map.inode_key = inode_key;
    key.u.map.block_no = block_no;

    return key;
}

static inline struct hive_mcl_index_key hive_mcl_make_inode_key(uint64_t inode_key)
{
    struct hive_mcl_index_key key = {0};

    key.kind = HIVE_MCL_KEY_INODE;
    key.u.inode.inode_key = inode_key;

    return key;
}

static inline struct hive_mcl_index_key hive_mcl_make_dir_key(uint64_t dir_inode_key,
                                                              const char *name,
                                                              size_t name_len)
{
    struct hive_mcl_index_key key = {0};

    key.kind = HIVE_MCL_KEY_DIR;
    key.u.dir.dir_inode_key = dir_inode_key;
    if (name_len > HIFS_MAX_NAME_SIZE) {
        name_len = HIFS_MAX_NAME_SIZE;
    }
    key.u.dir.name_len = (uint16_t)name_len;
    memcpy(key.u.dir.name, name, name_len);
    if (name_len < HIFS_MAX_NAME_SIZE) {
        key.u.dir.name[name_len] = '\0';
    }

    return key;
}

static inline bool hive_mcl_keys_equal(const struct hive_mcl_index_key *a,
                                       const struct hive_mcl_index_key *b)
{
    if (a->kind != b->kind) {
        return false;
    }

    switch (a->kind) {
    case HIVE_MCL_KEY_MAP_DELTA:
        return a->u.map.inode_key == b->u.map.inode_key &&
               a->u.map.block_no == b->u.map.block_no;
    case HIVE_MCL_KEY_INODE:
        return a->u.inode.inode_key == b->u.inode.inode_key;
    case HIVE_MCL_KEY_DIR:
        if (a->u.dir.dir_inode_key != b->u.dir.dir_inode_key ||
            a->u.dir.name_len != b->u.dir.name_len) {
            return false;
        }
        return memcmp(a->u.dir.name, b->u.dir.name, a->u.dir.name_len) == 0;
    }

    return false;
}

static inline uint64_t hive_mcl_key_hash(const struct hive_mcl_index_key *key)
{
    switch (key->kind) {
    case HIVE_MCL_KEY_MAP_DELTA: {
        uint64_t seed = key->u.map.inode_key * 0x9e3779b185ebca87ull;
        seed ^= key->u.map.block_no + (seed << 7);
        return hive_mcl_mix64(seed);
    }
    case HIVE_MCL_KEY_INODE:
        return hive_mcl_mix64(key->u.inode.inode_key | 1ull);
    case HIVE_MCL_KEY_DIR: {
        uint64_t seed = hive_mcl_hash_bytes(key->u.dir.name, key->u.dir.name_len, key->u.dir.dir_inode_key);
        return hive_mcl_mix64(seed ^ 0x74626c6469726575ull);
    }
    }

    return 0;
}

static inline uint64_t hive_mcl_now_ns(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static struct hive_mcl_mem_table_entry *
hive_mcl_mem_index_lookup_slot_locked(const struct hive_mcl_index_key *key,
                                      bool create,
                                      bool *is_new)
{
    uint64_t hash = hive_mcl_key_hash(key);
    size_t idx = (size_t)(hash & (HIFS_MCL_MEM_TABLE_SIZE - 1u));
    struct hive_mcl_mem_table_entry *first_tombstone = NULL;

    if (is_new) {
        *is_new = false;
    }

    for (size_t probe = 0; probe < HIFS_MCL_MEM_TABLE_SIZE; probe++) {
        struct hive_mcl_mem_table_entry *entry = &g_mcl_mem_table[idx];

        if (entry->slot_state == HIVE_MCL_SLOT_EMPTY) {
            if (!create) {
                return NULL;
            }

            entry = first_tombstone ? first_tombstone : entry;
            entry->slot_state = HIVE_MCL_SLOT_OCCUPIED;
            entry->key = *key;
            entry->hash = hash;
            if (is_new) {
                *is_new = true;
            }
            return entry;
        }

        if (entry->slot_state == HIVE_MCL_SLOT_TOMBSTONE) {
            if (!first_tombstone) {
                first_tombstone = entry;
            }
        } else if (entry->hash == hash && hive_mcl_keys_equal(&entry->key, key)) {
            return entry;
        }

        idx = (idx + 1u) & (HIFS_MCL_MEM_TABLE_SIZE - 1u);
    }

    if (create && first_tombstone) {
        first_tombstone->slot_state = HIVE_MCL_SLOT_OCCUPIED;
        first_tombstone->key = *key;
        first_tombstone->hash = hash;
        if (is_new) {
            *is_new = true;
        }
        return first_tombstone;
    }

    return NULL;
}

static void hive_mcl_mem_index_remove_entry_unlocked(struct hive_mcl_mem_table_entry *entry)
{
    memset(entry, 0, sizeof(*entry));
    entry->slot_state = HIVE_MCL_SLOT_TOMBSTONE;
}

static void hive_mcl_mem_index_evict_unlocked(void)
{
    size_t victim = HIFS_MCL_MEM_TABLE_SIZE;
    uint64_t oldest_ns = UINT64_MAX;

    for (size_t i = 0; i < HIFS_MCL_MEM_TABLE_SIZE; i++) {
        struct hive_mcl_mem_table_entry *entry = &g_mcl_mem_table[i];

        if (entry->slot_state != HIVE_MCL_SLOT_OCCUPIED) {
            continue;
        }

        if (entry->flags & HIVE_MCL_INDEX_FLAG_DIRTY) {
            continue;
        }

        if (entry->last_update_ns < oldest_ns) {
            oldest_ns = entry->last_update_ns;
            victim = i;
        }
    }

    if (victim == HIFS_MCL_MEM_TABLE_SIZE) {
        oldest_ns = UINT64_MAX;
        for (size_t i = 0; i < HIFS_MCL_MEM_TABLE_SIZE; i++) {
            struct hive_mcl_mem_table_entry *entry = &g_mcl_mem_table[i];

            if (entry->slot_state != HIVE_MCL_SLOT_OCCUPIED) {
                continue;
            }

            if (entry->last_update_ns < oldest_ns) {
                oldest_ns = entry->last_update_ns;
                victim = i;
            }
        }
    }

    if (victim < HIFS_MCL_MEM_TABLE_SIZE) {
        hive_mcl_mem_index_remove_entry_unlocked(&g_mcl_mem_table[victim]);
        if (g_mcl_mem_count > 0) {
            g_mcl_mem_count--;
        }
    }
}

static void hive_mcl_mem_index_update_flags(const struct hive_mcl_index_key *key,
                                            uint32_t set_mask,
                                            uint32_t clear_mask)
{
    pthread_mutex_lock(&g_mcl_mem_mu);
    struct hive_mcl_mem_table_entry *entry = hive_mcl_mem_index_lookup_slot_locked(key, false, NULL);

    if (entry) {
        entry->flags &= ~clear_mask;
        entry->flags |= set_mask;
        entry->last_update_ns = hive_mcl_now_ns();
    }

    pthread_mutex_unlock(&g_mcl_mem_mu);
}

static void hive_mcl_mem_index_track_map_delta(const struct hifs_mcl_map_delta_rec *rec,
                                               enum hifs_mcl_change_type change_type)
{
    struct hive_mcl_index_key key = hive_mcl_make_map_key(rec->inode_key, rec->block_no);
    bool is_new = false;

    pthread_mutex_lock(&g_mcl_mem_mu);
    if (g_mcl_mem_count >= HIFS_MCL_MEM_MAX_RECORDS) {
        hive_mcl_mem_index_evict_unlocked();
    }

    struct hive_mcl_mem_table_entry *entry = hive_mcl_mem_index_lookup_slot_locked(&key, true, &is_new);
    if (!entry) {
        pthread_mutex_unlock(&g_mcl_mem_mu);
        return;
    }

    entry->item = CHANGE_MAP_DELTA;
    entry->change_type = change_type;
    entry->txn_id = rec->txn_id;
    entry->generation = rec->generation;
    entry->payload.map_delta = *rec;
    entry->flags = (entry->flags & HIVE_MCL_INDEX_FLAG_OBSOLETE) | HIVE_MCL_INDEX_FLAG_DIRTY;
    entry->flags &= ~HIVE_MCL_INDEX_FLAG_APPLIED;
    entry->last_update_ns = hive_mcl_now_ns();

    if (is_new) {
        g_mcl_mem_count++;
    }

    pthread_mutex_unlock(&g_mcl_mem_mu);
}

static inline uint64_t hive_mcl_now_ns(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void hive_mcl_mem_cache_put(const struct hive_seg_mcl_block_entry *blk,
                                   uint64_t txn_id,
                                   uint32_t generation,
                                   uint64_t inode_key)
{
    struct hive_mcl_mem_table_entry entry;
    size_t slot;

    entry.block = *blk;
    entry.txn_id = txn_id;
    entry.inode_key = inode_key;
    entry.generation = generation;
    entry.last_update_ns = hive_mcl_now_ns();

    pthread_mutex_lock(&g_mcl_mem_mu);
    if (g_mcl_mem_count < HIFS_MCL_MEM_MAX_RECORDS) {
        slot = (g_mcl_mem_head + g_mcl_mem_count) % HIFS_MCL_MEM_MAX_RECORDS;
        g_mcl_mem_count++;
    } else {
        slot = g_mcl_mem_head;
        g_mcl_mem_head = (g_mcl_mem_head + 1) % HIFS_MCL_MEM_MAX_RECORDS;
    }
    g_mcl_mem_table[slot] = entry;
    pthread_mutex_unlock(&g_mcl_mem_mu);
}

int hive_seg_emit_mcl_map_delta(const struct hive_seg_mcl_block_entry *blk,
                                uint64_t txn_id,
                                uint32_t generation,
                                uint64_t inode_key)
{
    struct hifs_mcl_map_delta_rec rec = {0};

    if (!blk) {
        errno = EINVAL;
        return -1;
    }

    rec.txn_id          = txn_id;
    rec.volume_id       = blk->volume_id;
    rec.inode_key       = inode_key;
    rec.block_no        = blk->block_no;
    rec.generation      = generation;
    rec.block_bytes     = blk->block_bytes;
    rec.placement_epoch = blk->placement_epoch;
    rec.hash_algo       = (uint8_t)blk->hash_algo;
    rec.stripe_algo     = (uint8_t)blk->stripe_algo;

    memcpy(rec.content_hash, blk->hash, HIFS_BLOCK_HASH_SIZE);
    memcpy(rec.stripe_id, blk->stripe_id, HIFS_STRIPE_ID_SIZE);

    hive_mcl_mem_cache_put(blk, txn_id, generation, inode_key);

    return hifs_mcl_append(&g_hive_guard_mcl_ctx,
                           CHANGE_MAP_DELTA,
                           HIFS_MCL_DELTA_MAP_APPEND,
                           &rec,
                           sizeof(rec),
                           txn_id,
                           inode_key);
}


struct hifs_mcl_ctx g_hive_guard_mcl_ctx = {
    .fd = -1,
};

static pthread_mutex_t g_mcl_rotate_mu = PTHREAD_MUTEX_INITIALIZER;
static uint32_t g_mcl_next_slot;

static inline uint64_t hifs_mcl_next_seqno(struct hifs_mcl_ctx *ctx)
{
    return ctx->next_seqno++;
}

static inline uint32_t hifs_mcl_crc32_seed(uint32_t crc, const void *data, size_t len)
{
    const uint8_t *p = data;

    for (size_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (size_t j = 0; j < 8; j++) {
            uint32_t mask = -(crc & 1u);
            crc = (crc >> 1) ^ (0xedb88320u & mask);
        }
    }

    return crc;
}

static inline size_t hifs_mcl_payload_len(enum hifs_meta_change_item item)
{
    switch (item) {
    case CHANGE_INODE:
        return sizeof(struct change_inode);
    case CHANGE_DIRENTRY:
        return sizeof(struct change_direntry);
    case CHANGE_STRIPE_INFO:
        return sizeof(struct change_stripe_info_array);
    case CHANGE_ROOT_DIRENTRY:
        return sizeof(struct change_root_dentry);
    case CHANGE_VOLUME_SUPERBLOCK:
        return sizeof(struct change_volume);
    case CHANGE_MAP_DELTA:
        return sizeof(struct hifs_mcl_map_delta_rec);
    default:
        return 0;
    }
}

static int hifs_mcl_current_slot(const char *path)
{
    const char *base;
    const char *digits;
    char *endptr;
    long slot;

    if (!path) {
        return -1;
    }

    base = strrchr(path, '/');
    base = base ? base + 1 : path;

    digits = base;
    while (*digits && !isdigit((unsigned char)*digits)) {
        digits++;
    }

    if (!*digits) {
        return -1;
    }

    slot = strtol(digits, &endptr, 10);
    if (digits == endptr || slot < 0) {
        return -1;
    }

    return (int)(slot % HIFS_MCL_MAX_LOGS);
}

static void hifs_mcl_slot_path_for(const char *path,
                                   unsigned int slot,
                                   char *out,
                                   size_t out_sz)
{
    const char *slash;
    size_t dir_len;

    if (!out || out_sz == 0) {
        return;
    }

    slash = strrchr(path ? path : "", '/');
    dir_len = slash ? (size_t)(slash - (path ? path : "") + 1) : 0;
    if (dir_len >= out_sz) {
        dir_len = out_sz - 1;
    }

    if (dir_len && path) {
        memcpy(out, path, dir_len);
    }
    out[dir_len] = '\0';

    snprintf(out + dir_len, out_sz - dir_len, "mcl-%u.log", slot);
}

static int hifs_mcl_rotate_locked(struct hifs_mcl_ctx *ctx)
{
    char next_path[sizeof(ctx->path)];
    int current_slot;
    unsigned int next_slot;
    int new_fd;

    if (!ctx || ctx->fd < 0) {
        errno = EINVAL;
        return -1;
    }

    current_slot = hifs_mcl_current_slot(ctx->path);
    if (current_slot >= 0) {
        next_slot = (unsigned int)((current_slot + 1) % HIFS_MCL_MAX_LOGS);
    } else {
        next_slot = g_mcl_next_slot % HIFS_MCL_MAX_LOGS;
    }
    g_mcl_next_slot = (next_slot + 1) % HIFS_MCL_MAX_LOGS;

    hifs_mcl_slot_path_for(ctx->path, next_slot, next_path, sizeof(next_path));

    unlink(next_path);
    new_fd = open(next_path, O_WRONLY | O_CREAT | O_TRUNC | O_APPEND
#ifdef O_CLOEXEC
                          | O_CLOEXEC
#endif
                          , S_IRUSR | S_IWUSR | S_IRGRP);
    if (new_fd < 0) {
        return -1;
    }

    if (close(ctx->fd) < 0) {
        int saved = errno;
        close(new_fd);
        errno = saved;
        return -1;
    }

    ctx->fd = new_fd;
    strncpy(ctx->path, next_path, sizeof(ctx->path) - 1);
    ctx->path[sizeof(ctx->path) - 1] = '\0';
    ctx->next_seqno = 0;

    return 0;
}

static int hifs_mcl_rotate_if_needed(struct hifs_mcl_ctx *ctx, size_t next_write)
{
    struct stat st;
    uint64_t size;
    int rc = 0;

    if (!ctx || ctx->fd < 0) {
        errno = EINVAL;
        return -1;
    }

    pthread_mutex_lock(&g_mcl_rotate_mu);
    if (fstat(ctx->fd, &st) < 0) {
        rc = -1;
        goto out_unlock;
    }

    size = (uint64_t)st.st_size;
    if (size >= HIFS_MCL_MAX_BYTES ||
        next_write >= HIFS_MCL_MAX_BYTES ||
        HIFS_MCL_MAX_BYTES - size <= next_write) {
        rc = hifs_mcl_rotate_locked(ctx);
    }

out_unlock:
    pthread_mutex_unlock(&g_mcl_rotate_mu);
    return rc;
}

int hifs_mcl_open(struct hifs_mcl_ctx *ctx, const char *path, bool create)
{
    int flags;
    int fd;

    if (!ctx || !path) {
        errno = EINVAL;
        return -1;
    }

    flags = O_WRONLY | O_APPEND;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
    if (create) {
        flags |= O_CREAT;
    }

    fd = open(path, flags, S_IRUSR | S_IWUSR | S_IRGRP);
    if (fd < 0) {
        return -1;
    }

    ctx->fd = fd;
    ctx->next_seqno = 0;
    strncpy(ctx->path, path, sizeof(ctx->path) - 1);
    ctx->path[sizeof(ctx->path) - 1] = '\0';

    return 0;
}

int hifs_mcl_close(struct hifs_mcl_ctx *ctx)
{
    int rc;

    if (!ctx) {
        errno = EINVAL;
        return -1;
    }

    if (ctx->fd < 0) {
        return 0;
    }

    hive_mcl_async_wait_idle();

    rc = close(ctx->fd);
    if (rc < 0) {
        return -1;
    }

    ctx->fd = -1;
    ctx->next_seqno = 0;
    ctx->path[0] = '\0';

    return 0;
}

static int hive_mcl_write_record(struct hifs_mcl_ctx *ctx,
                                 enum hifs_meta_change_item item,
                                 enum hifs_mcl_change_type change_type,
                                 const void *entry,
                                 size_t entry_len,
                                 uint64_t txn_id,
                                 uint64_t object_id)
{
    struct hifs_mcl_hdr hdr;
    struct hifs_mcl_record_prefix prefix;
    struct iovec iov[3];
    ssize_t n;
    uint64_t seqno;
    size_t expected_len;
    size_t total_len;
    uint32_t crc;
    int iovcnt = 3;
    int rc;

    if (!ctx || ctx->fd < 0 || !entry) {
        errno = EINVAL;
        return -1;
    }

    expected_len = hifs_mcl_payload_len(item);
    if (expected_len == 0) {
        errno = EINVAL;
        return -1;
    }

    if (entry_len == 0) {
        entry_len = expected_len;
    } else if (entry_len != expected_len) {
        errno = EINVAL;
        return -1;
    }

    total_len = sizeof(struct hifs_mcl_hdr) + sizeof(prefix) + entry_len;
    rc = hifs_mcl_rotate_if_needed(ctx, total_len);
    if (rc < 0) {
        return -1;
    }

    seqno = hifs_mcl_next_seqno(ctx);

    prefix.item = (uint8_t)item;
    prefix.change_type = (uint8_t)change_type;

    hdr.magic = HIFS_MCL_MAGIC_VALUE;
    hdr.version = HIFS_MCL_VERSION;
    hdr.type = (uint16_t)item;
    hdr.length = (uint32_t)(sizeof(prefix) + entry_len);
    hdr.seqno = seqno;
    hdr.txn_id = txn_id;
    hdr.object_id = object_id;

    crc = 0xffffffffu;
    crc = hifs_mcl_crc32_seed(crc, &prefix, sizeof(prefix));
    crc = hifs_mcl_crc32_seed(crc, entry, entry_len);
    hdr.crc32 = ~crc;

    iov[0].iov_base = &hdr;
    iov[0].iov_len = sizeof(hdr);
    iov[1].iov_base = &prefix;
    iov[1].iov_len = sizeof(prefix);
    iov[2].iov_base = (void *)entry;
    iov[2].iov_len = entry_len;

    if (entry_len == 0) {
        iovcnt = 2;
    }

    n = writev(ctx->fd, iov, iovcnt);
    if (n < 0) {
        return -1;
    }

    if ((size_t)n != sizeof(hdr) + sizeof(prefix) + (size_t)entry_len) {
        errno = EIO;
        return -1;
    }

    return 0;
}

int hifs_mcl_append(struct hifs_mcl_ctx *ctx,
                    enum hifs_meta_change_item item,
                    enum hifs_mcl_change_type change_type,
                    const void *entry,
                    size_t entry_len,
                    uint64_t txn_id,
                    uint64_t object_id)
{
    size_t expected_len;

    if (!entry) {
        errno = EINVAL;
        return -1;
    }

    if (!ctx) {
        ctx = &g_hive_guard_mcl_ctx;
    }

    if (ctx->fd < 0) {
        errno = EBADF;
        return -1;
    }

    expected_len = hifs_mcl_payload_len(item);
    if (expected_len == 0) {
        errno = EINVAL;
        return -1;
    }

    if (entry_len == 0) {
        entry_len = expected_len;
    } else if (entry_len != expected_len) {
        errno = EINVAL;
        return -1;
    }

    if (entry_len > HIVE_MCL_MAX_PAYLOAD_SIZE) {
        errno = EOVERFLOW;
        return -1;
    }

    if (hive_mcl_async_ensure_thread() != 0) {
        return hive_mcl_write_record(ctx,
                                     item,
                                     change_type,
                                     entry,
                                     entry_len,
                                     txn_id,
                                     object_id);
    }

    return hive_mcl_async_queue_record(ctx,
                                       item,
                                       change_type,
                                       entry,
                                       entry_len,
                                       txn_id,
                                       object_id);
}
