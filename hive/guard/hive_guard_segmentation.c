

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "hive_guard_segmentation.h"
#include "hive_guard_wbl.h"
#include "hive_guard_mcl.h"
#include "hive_guard_stats.h"
#include "hive_guard.h"


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
    uint32_t block_bytes;
    uint32_t placement_epoch;
    uint8_t hash[HIFS_BLOCK_HASH_SIZE];
    uint8_t stripe_id[HIFS_STRIPE_ID_SIZE];
};

static struct hive_seg_task g_seg_queue[HIVE_SEG_QUEUE_DEPTH];
static size_t g_seg_head;
static size_t g_seg_tail;
static size_t g_seg_size;
static pthread_mutex_t g_seg_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_seg_cv = PTHREAD_COND_INITIALIZER;
static pthread_cond_t g_seg_space_cv = PTHREAD_COND_INITIALIZER;
static pthread_t g_seg_workers[HIVE_SEG_MAX_WORKERS];
static size_t g_seg_worker_count;
static _Atomic bool g_seg_started = false;
static bool g_seg_running;
static atomic_uint_fast64_t g_seg_txn_seq = ATOMIC_VAR_INIT(1);

static inline uint64_t hive_seg_resolve_txn(uint64_t hint)
{
    if (hint != 0)
        return hint;
    return atomic_fetch_add_explicit(&g_seg_txn_seq, 1, memory_order_relaxed);
}

static inline void hive_seg_release_block(struct hive_seg_block_input *blk)
{
    if (!blk)
        return;
    if (blk->release_cb) {
        blk->release_cb(blk->release_ctx, blk->data, blk->len);
    } else if (blk->data) {
        free(blk->data);
    }
    blk->data = NULL;
}

static void hive_seg_task_cleanup(struct hive_seg_task *task)
{
    if (!task)
        return;
    if (task->type == HIVE_SEG_TASK_SINGLE) {
        hive_seg_release_block(&task->u.single);
    } else if (task->type == HIVE_SEG_TASK_CONTIG && task->u.contig.blocks) {
        for (uint32_t i = 0; i < task->u.contig.count; ++i)
            hive_seg_release_block(&task->u.contig.blocks[i]);
        free(task->u.contig.blocks);
        task->u.contig.blocks = NULL;
        task->u.contig.count = 0;
    }
}

struct hive_seg_stage_cursor {
    uint64_t seg_id;
    uint32_t bytes_used;
};

static struct {
    pthread_mutex_t mu;
    uint64_t next_seg_id;
    struct hive_seg_stage_cursor cursor;
} g_seg_stage_allocator = {
    .mu = PTHREAD_MUTEX_INITIALIZER,
    .next_seg_id = 1,
    .cursor = {
        .seg_id = 0,
        .bytes_used = 0,
    },
};

static inline uint32_t hive_seg_align_len(uint32_t len)
{
    if (len == 0)
        return 0;
    if (len >= HIFS_SEGMENT_SIZE)
        return HIFS_SEGMENT_SIZE;
    uint32_t rem = len % HIFS_DEFAULT_BLOCK_SIZE;
    if (rem == 0)
        return len;
    return len + (HIFS_DEFAULT_BLOCK_SIZE - rem);
}

static inline void hive_seg_stage_rotate_locked(void)
{
    g_seg_stage_allocator.cursor.seg_id = g_seg_stage_allocator.next_seg_id++;
    g_seg_stage_allocator.cursor.bytes_used = 0;
}

static inline void hive_seg_stage_ensure_capacity_locked(uint32_t need)
{
    if (g_seg_stage_allocator.cursor.seg_id == 0 ||
        (need > 0 &&
         g_seg_stage_allocator.cursor.bytes_used + need > HIFS_SEGMENT_SIZE)) {
        hive_seg_stage_rotate_locked();
    }
}

static inline void hive_seg_mark_landing(struct hive_seg_block_input *blk,
                                         uint64_t seg_id,
                                         uint32_t offset)
{
    blk->has_landing_loc = true;
    blk->landing_loc.seg_id = seg_id;
    blk->landing_loc.offset = offset;
    blk->landing_loc.length = blk->len;
}

// TODO: Here after the landing intent write, we need to do the next stage, which is LANDING_WRITTEN. To do that we:
// 1. There are several new file devfs for segment directories and indexes in the hive_guard_segmentation.h file:
//     #define HIVE_GUARD_SEGMT_DIR          HIVE_DATA_DIR "/writeback_cache"
//     #define HIVE_GUARD_SEGMT_LANDING      HIVE_GUARD_SEGMT_DIR "/landing"
//     #define HIVE_GUARD_SEGMT_OUTBOUND     HIVE_GUARD_SEGMT_DIR "/outbound"
//     #define HIVE_GUARD_SEGMT_READ_RETURN  HIVE_GUARD_SEGMT_DIR "/read_return"
//     #define HIVE_GUARD_SEGMT_LAND_INDX    HIVE_GUARD_SEGMT_LANDING "/landing_segment.indx"
//     #define HIVE_GUARD_SEGMT_OUTB_INDX    HIVE_GUARD_SEGMT_LANDING "/outbound_segment.indx"
//     #define HIVE_GUARD_SEGMT_RRET_INDX    HIVE_GUARD_SEGMT_LANDING "/read_return_segment.indx"
//   Like the WBL/MCL files, the indexes above should be opened on hive_guard start. But they are also in mem. We only write & read at
//   important junctions when something changes that has to be written to a sigment, mostly new segments, but also when segments go over tcp/ip.
// 2. Before the incoming blocks went right to Erasure Coding. We interrupted that process I hope by re-directing the incoming blocks here. But now, we need to call
//    the main erasure coding entry point and we need to alter EC logic to accept our incoming segment instead of a single block or contiguous blocks.
// 3. EC should also rreturn a poitner to the altered segment in mem here. We need to write the segment file created in the EC step directly to the LANDING cache dir
//    now. Then we write a LANDING_WRITTEN to the WBL log.
// Next, we will run the placement algorithm, collect all the stripe data and run the next log step.  

static inline void hive_seg_stage_block_locked(struct hive_seg_block_input *blk)
{
    if (!blk || blk->len == 0)
        return;
    uint32_t aligned = hive_seg_align_len(blk->len);
    if (aligned == 0)
        aligned = blk->len;
    hive_seg_stage_ensure_capacity_locked(aligned);
    hive_seg_mark_landing(blk,
                          g_seg_stage_allocator.cursor.seg_id,
                          g_seg_stage_allocator.cursor.bytes_used);
    g_seg_stage_allocator.cursor.bytes_used += aligned;
}

static void hive_seg_stage_blocks(struct hive_seg_block_input *blocks,
                                  uint32_t count)
{
    if (!blocks || count == 0)
        return;

    pthread_mutex_lock(&g_seg_stage_allocator.mu);

    uint64_t total = 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (blocks[i].len == 0)
            continue;
        total += hive_seg_align_len(blocks[i].len);
    }

    if (total > 0 && total <= HIFS_SEGMENT_SIZE)
        hive_seg_stage_ensure_capacity_locked((uint32_t)total);
    else if (g_seg_stage_allocator.cursor.seg_id == 0)
        hive_seg_stage_rotate_locked();

    for (uint32_t i = 0; i < count; ++i)
        hive_seg_stage_block_locked(&blocks[i]);

    pthread_mutex_unlock(&g_seg_stage_allocator.mu);
}

static inline void hive_seg_copy_block_input(struct hive_seg_block_input *dst,
                                             const struct hive_seg_block_input *src)
{
    if (!dst || !src)
        return;
    *dst = *src;
    if (!src->has_hash)
        memset(dst->hash, 0, sizeof(dst->hash));
    if (!src->has_stripe_id)
        memset(dst->stripe_id, 0, sizeof(dst->stripe_id));
    dst->has_landing_loc = false;
    memset(&dst->landing_loc, 0, sizeof(dst->landing_loc));
}

static inline void hive_seg_log_write_intent(const struct hive_seg_block_input *blk,
                                             uint64_t txn_id,
                                             uint32_t generation)
{
    if (!blk)
        return;

    struct hifs_wbl_write_intent_rec rec = {
        .txn_id = txn_id,
        .stripe_id = blk->block_no,
        .inode_id = blk->volume_id,
        .generation = generation,
    };
    rec.range.inode_id = blk->volume_id;
    rec.range.lba_start = blk->block_no;
    rec.range.block_count = 1;
    snprintf(rec.block_id, sizeof(rec.block_id),
             "%llu:%llu",
             (unsigned long long)blk->volume_id,
             (unsigned long long)blk->block_no);
    if (hifs_wbl_mark_write_intent(&g_hive_guard_wbl_ctx, &rec) != 0) {
        hifs_err("hive_segmentation: unable to append WBL write intent for %llu:%llu: %s",
                 (unsigned long long)blk->volume_id,
                 (unsigned long long)blk->block_no,
                 strerror(errno));
    }

    struct hifs_wbl_landing_rec landing = {
        .txn_id = txn_id,
        .stripe_id = blk->block_no,
    };
    if (blk->has_landing_loc) {
        landing.landing_loc = blk->landing_loc;
    } else {
        landing.landing_loc.seg_id = blk->block_no;
        landing.landing_loc.offset = 0;
        landing.landing_loc.length = blk->len;
    }
    if (hifs_wbl_mark_landing_written(&g_hive_guard_wbl_ctx, &landing) != 0) {
        hifs_err("hive_segmentation: unable to append WBL landing event for %llu:%llu: %s",
                 (unsigned long long)blk->volume_id,
                 (unsigned long long)blk->block_no,
                 strerror(errno));
    }
}

static inline void hive_seg_log_metadata(const struct hive_seg_block_input *blk,
                                         uint64_t txn_id)
{
    if (!blk)
        return;
    struct hive_seg_mcl_block_entry entry = {
        .volume_id = blk->volume_id,
        .block_no = blk->block_no,
        .hash_algo = blk->hash_algo,
        .block_bytes = blk->len,
        .placement_epoch = blk->placement_epoch,
    };
    memcpy(entry.hash, blk->hash, sizeof(entry.hash));
    memcpy(entry.stripe_id, blk->stripe_id, sizeof(entry.stripe_id));
    if (hifs_mcl_append(&g_hive_guard_mcl_ctx,
                        CHANGE_STRIPE_INFO,
                        HIFS_MCL_NEW,
                        &entry,
                        sizeof(entry),
                        txn_id,
                        blk->block_no) != 0) {
        hifs_err("hive_segmentation: unable to append MCL record for %llu:%llu: %s",
                 (unsigned long long)blk->volume_id,
                 (unsigned long long)blk->block_no,
                 strerror(errno));
    }
}

static int hive_seg_apply_block(struct hive_seg_block_input *blk,
                                uint64_t txn_id,
                                uint32_t generation)
{
    if (!blk || !blk->data || blk->len == 0)
        return -EINVAL;
    if (!blk->has_hash)
        return -EINVAL;

    hive_seg_log_write_intent(blk, txn_id, generation);
    hive_seg_log_metadata(blk, txn_id);

    return hifs_put_block(blk->volume_id,
                          blk->block_no,
                          blk->data,
                          blk->len,
                          blk->hash_algo,
                          blk->hash,
                          blk->stripe_id,
                          blk->stripe_algo,
                          blk->placement_epoch);
}

static void hive_seg_process_task(struct hive_seg_task *task)
{
    if (!task)
        return;
    uint64_t resolved_txn = hive_seg_resolve_txn(task->txn_hint);
    if (task->type == HIVE_SEG_TASK_SINGLE) {
        int rc = hive_seg_apply_block(&task->u.single,
                                      resolved_txn,
                                      task->generation);
        if (rc != 0) {
            hifs_err("hive_segmentation: failed to apply block %llu:%llu rc=%d",
                     (unsigned long long)task->u.single.volume_id,
                     (unsigned long long)task->u.single.block_no,
                     rc);
        }
        return;
    }

    uint64_t total_bytes = 0;
    for (uint32_t i = 0; i < task->u.contig.count; ++i) {
        struct hive_seg_block_input *blk = &task->u.contig.blocks[i];
        int rc = hive_seg_apply_block(blk, resolved_txn, task->generation);
        if (rc != 0) {
            hifs_err("hive_segmentation: failed to apply contig block %u/%u (%llu:%llu) rc=%d",
                     i + 1,
                     task->u.contig.count,
                     (unsigned long long)blk->volume_id,
                     (unsigned long long)blk->block_no,
                     rc);
            break;
        }
        total_bytes += blk->len;
    }
    atomic_fetch_add(&g_stats.contig_write_calls, 1);
    atomic_fetch_add(&g_stats.contig_write_bytes, total_bytes);
}

static bool hive_seg_queue_pop(struct hive_seg_task *out)
{
    pthread_mutex_lock(&g_seg_mu);
    while (g_seg_running && g_seg_size == 0)
        pthread_cond_wait(&g_seg_cv, &g_seg_mu);

    if (!g_seg_running && g_seg_size == 0) {
        pthread_mutex_unlock(&g_seg_mu);
        return false;
    }

    *out = g_seg_queue[g_seg_head];
    g_seg_head = (g_seg_head + 1u) % HIVE_SEG_QUEUE_DEPTH;
    g_seg_size--;
    pthread_cond_signal(&g_seg_space_cv);
    pthread_mutex_unlock(&g_seg_mu);
    return true;
}

static void *hive_seg_worker_main(void *arg)
{
    (void)arg;
    struct hive_seg_task task;
    while (hive_seg_queue_pop(&task)) {
        hive_seg_process_task(&task);
        hive_seg_task_cleanup(&task);
    }
    return NULL;
}

static inline size_t hive_seg_worker_budget(void)
{
    long cpu_cnt = sysconf(_SC_NPROCESSORS_ONLN);
    if (cpu_cnt <= 0)
        cpu_cnt = 2;
    if (cpu_cnt > (long)HIVE_SEG_MAX_WORKERS)
        cpu_cnt = (long)HIVE_SEG_MAX_WORKERS;
    return (size_t)cpu_cnt;
}

static int hive_seg_queue_push(const struct hive_seg_task *task)
{
    if (!task)
        return -EINVAL;
    pthread_mutex_lock(&g_seg_mu);
    while (g_seg_running && g_seg_size == HIVE_SEG_QUEUE_DEPTH)
        pthread_cond_wait(&g_seg_space_cv, &g_seg_mu);

    if (!g_seg_running) {
        pthread_mutex_unlock(&g_seg_mu);
        return -ESHUTDOWN;
    }

    g_seg_queue[g_seg_tail] = *task;
    g_seg_tail = (g_seg_tail + 1u) % HIVE_SEG_QUEUE_DEPTH;
    g_seg_size++;
    pthread_cond_signal(&g_seg_cv);
    pthread_mutex_unlock(&g_seg_mu);
    return 0;
}

static int hive_seg_setup_workers(void)
{
    g_seg_worker_count = hive_seg_worker_budget();
    for (size_t i = 0; i < g_seg_worker_count; ++i) {
        int rc = pthread_create(&g_seg_workers[i], NULL, hive_seg_worker_main, NULL);
        if (rc != 0) {
            g_seg_worker_count = i;
            return -rc;
        }
    }
    return 0;
}

int hive_recieve_data_plane(void)
{
    bool expected = false;
    if (!atomic_compare_exchange_strong_explicit(&g_seg_started,
                                                 &expected,
                                                 true,
                                                 memory_order_seq_cst,
                                                 memory_order_seq_cst)) {
        return 0;
    }

    pthread_mutex_lock(&g_seg_mu);
    g_seg_head = g_seg_tail = g_seg_size = 0;
    g_seg_running = true;
    pthread_mutex_unlock(&g_seg_mu);

    int rc = hive_seg_setup_workers();
    if (rc != 0) {
        hive_segmentation_shutdown();
        return rc;
    }
    return 0;
}

void hive_segmentation_shutdown(void)
{
    if (!atomic_load(&g_seg_started))
        return;

    pthread_mutex_lock(&g_seg_mu);
    g_seg_running = false;
    pthread_cond_broadcast(&g_seg_cv);
    pthread_cond_broadcast(&g_seg_space_cv);
    pthread_mutex_unlock(&g_seg_mu);

    for (size_t i = 0; i < g_seg_worker_count; ++i)
        pthread_join(g_seg_workers[i], NULL);
    g_seg_worker_count = 0;

    pthread_mutex_lock(&g_seg_mu);
    while (g_seg_size > 0) {
        struct hive_seg_task task = g_seg_queue[g_seg_head];
        g_seg_head = (g_seg_head + 1u) % HIVE_SEG_QUEUE_DEPTH;
        g_seg_size--;
        pthread_mutex_unlock(&g_seg_mu);
        hive_seg_task_cleanup(&task);
        pthread_mutex_lock(&g_seg_mu);
    }
    pthread_mutex_unlock(&g_seg_mu);

    atomic_store(&g_seg_started, false);
}

int hive_seg_submit_single(const struct hive_seg_single_request *req)
{
    if (!req || !req->block)
        return -EINVAL;
    if (!atomic_load(&g_seg_started))
        return -EAGAIN;

    struct hive_seg_task task = {
        .type = HIVE_SEG_TASK_SINGLE,
        .txn_hint = req->txn_hint,
        .generation = req->generation,
    };
    hive_seg_copy_block_input(&task.u.single, req->block);
    hive_seg_stage_blocks(&task.u.single, 1);
    return hive_seg_queue_push(&task);
}

int hive_seg_submit_contig(const struct hive_seg_contig_request *req)
{
    if (!req || !req->blocks || req->block_count == 0)
        return -EINVAL;
    if (!atomic_load(&g_seg_started))
        return -EAGAIN;

    struct hive_seg_task task = {
        .type = HIVE_SEG_TASK_CONTIG,
        .txn_hint = req->txn_hint,
        .generation = req->generation,
    };
    task.u.contig.count = req->block_count;
    task.u.contig.blocks = calloc(req->block_count, sizeof(*task.u.contig.blocks));
    if (!task.u.contig.blocks)
        return -ENOMEM;
    for (uint32_t i = 0; i < req->block_count; ++i)
        hive_seg_copy_block_input(&task.u.contig.blocks[i], &req->blocks[i]);
    hive_seg_stage_blocks(task.u.contig.blocks, req->block_count);

    int rc = hive_seg_queue_push(&task);
    if (rc != 0)
        free(task.u.contig.blocks);
    return rc;
}
