

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "hive_guard_segmentation.h"
#include "hive_guard_wbl.h"
#include "hive_guard_raft.h"
#include "hive_guard_mcl.h"
#include "hive_guard_stats.h"
#include "hive_guard.h"


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

static pthread_mutex_t g_seg_landing_io_mu = PTHREAD_MUTEX_INITIALIZER;
static struct hive_seg_cache_ctx g_seg_cache = {
    .mu = PTHREAD_MUTEX_INITIALIZER,
    .ready = false,
    .landing_idx_fd = -1,
    .outbound_idx_fd = -1,
    .read_return_idx_fd = -1,
    .landing_idx = {
        .entries = NULL,
        .count = 0,
        .capacity = 0,
    },
    .outbound_idx = {
        .entries = NULL,
        .count = 0,
        .capacity = 0,
    },
};

static bool hive_seg_index_mem_append(struct hive_seg_index_mem *mem,
                                      const struct hive_seg_index_disk_rec *rec);
static int hive_seg_write_all(int fd, const void *buf, size_t len);
static int hive_seg_pwrite_all(int fd,
                               const void *buf,
                               size_t len,
                               off_t offset);
static inline struct hive_seg_index_disk_rec *
hive_seg_cache_find_outbound_rec_locked(uint64_t seg_id,
                                        uint64_t volume_id,
                                        uint64_t block_no,
                                        uint32_t node_id,
                                        uint64_t offset)
{
    if (!g_seg_cache.outbound_idx.entries)
        return NULL;
    for (size_t idx = g_seg_cache.outbound_idx.count; idx > 0; --idx) {
        struct hive_seg_index_disk_rec *rec =
            &g_seg_cache.outbound_idx.entries[idx - 1];
        if (rec->seg_id == seg_id &&
            rec->volume_id == volume_id &&
            rec->block_no == block_no &&
            rec->reserved == node_id &&
            rec->offset == offset) {
            return rec;
        }
    }
    return NULL;
}

static int hive_seg_cache_lock_outbound(uint64_t txn_id,
                                        uint32_t generation,
                                        const struct hive_seg_block_input *blk,
                                        uint32_t node_id,
                                        const struct hifs_seg_loc *loc)
{
    if (!blk || !loc || node_id == 0)
        return -EINVAL;

    pthread_mutex_lock(&g_seg_cache.mu);
    if (!g_seg_cache.ready) {
        pthread_mutex_unlock(&g_seg_cache.mu);
        return -ESHUTDOWN;
    }

    struct hive_seg_index_disk_rec *rec =
        hive_seg_cache_find_outbound_rec_locked(loc->seg_id,
                                                blk->volume_id,
                                                blk->block_no,
                                                node_id,
                                                loc->offset);
    bool existing = (rec != NULL);
    if (!existing) {
        struct hive_seg_index_disk_rec tmp = {
            .seg_id = loc->seg_id,
            .volume_id = blk->volume_id,
            .block_no = blk->block_no,
            .txn_id = txn_id,
            .generation = generation,
            .state = HIFS_STRIPE_OUTBOUND_QUEUED,
            .offset = loc->offset,
            .length = loc->length,
            .reserved = node_id,
        };
        if (!hive_seg_index_mem_append(&g_seg_cache.outbound_idx, &tmp)) {
            pthread_mutex_unlock(&g_seg_cache.mu);
            return -ENOMEM;
        }
        rec = &g_seg_cache.outbound_idx.entries[g_seg_cache.outbound_idx.count - 1];
    } else {
        rec->txn_id = txn_id;
        rec->generation = generation;
        rec->state = HIFS_STRIPE_OUTBOUND_QUEUED;
        rec->length = loc->length;
    }

    int fd = g_seg_cache.outbound_idx_fd;
    int rc = 0;
    if (fd >= 0) {
        if (existing) {
            size_t idx = (size_t)(rec - g_seg_cache.outbound_idx.entries);
            rc = hive_seg_pwrite_all(fd,
                                     rec,
                                     sizeof(*rec),
                                     (off_t)idx * (off_t)sizeof(*rec));
        } else {
            rc = hive_seg_write_all(fd, rec, sizeof(*rec));
        }
    }

    pthread_mutex_unlock(&g_seg_cache.mu);
    return rc;
}

static inline void hive_seg_log_outbound_queued(const struct hive_seg_block_input *blk,
                                                uint64_t txn_id,
                                                uint32_t node_id,
                                                const struct hifs_seg_loc *loc)
{
    if (!blk || !loc || node_id == 0)
        return;

    struct hifs_wbl_outbound_queued_rec rec = {
        .txn_id = txn_id,
        .stripe_id = blk->block_no,
        .node_id = node_id,
        .frag_loc = *loc,
    };
    if (hifs_wbl_mark_outbound_queued(&g_hive_guard_wbl_ctx, &rec) != 0) {
        hifs_err("hive_segmentation: unable to append WBL outbound queued record for %llu:%llu node=%u: %s",
                 (unsigned long long)blk->volume_id,
                 (unsigned long long)blk->block_no,
                 node_id,
                 strerror(errno));
    }
}

static int hive_seg_mark_outbound_queued(uint64_t txn_id,
                                         uint32_t generation,
                                         const struct hive_seg_block_input *blk,
                                         uint32_t node_id,
                                         const struct hifs_seg_loc *loc)
{
    int rc = hive_seg_cache_lock_outbound(txn_id, generation, blk, node_id, loc);
    if (rc != 0)
        return rc;
    hive_seg_log_outbound_queued(blk, txn_id, node_id, loc);
    return 0;
}
static inline uint64_t hive_seg_now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static inline size_t hive_seg_vappend(char *buf,
                                      size_t buf_sz,
                                      size_t pos,
                                      const char *fmt,
                                      va_list ap)
{
    if (!buf || pos >= buf_sz)
        return buf_sz;
    int rc = vsnprintf(buf + pos, buf_sz - pos, fmt, ap);
    if (rc < 0)
        return buf_sz;
    size_t written = (size_t)rc;
    if (written >= buf_sz - pos)
        return buf_sz;
    return pos + written;
}

static inline size_t hive_seg_append(char *buf,
                                     size_t buf_sz,
                                     size_t pos,
                                     const char *fmt,
                                     ...)
{
    if (!buf || pos >= buf_sz)
        return buf_sz;
    va_list ap;
    va_start(ap, fmt);
    size_t new_pos = hive_seg_vappend(buf, buf_sz, pos, fmt, ap);
    va_end(ap);
    return new_pos;
}

static inline void hive_seg_format_target_summary(char *buf,
                                                  size_t buf_sz,
                                                  const struct hifs_fragment_target *targets,
                                                  size_t count)
{
    if (!buf || buf_sz == 0) {
        return;
    }
    size_t pos = 0;
    buf[0] = '\0';
    pos = hive_seg_append(buf, buf_sz, pos, "targets=[");
    if (!targets || count == 0) {
        hive_seg_append(buf, buf_sz, pos, "]");
        return;
    }
    for (size_t i = 0; i < count && pos < buf_sz; ++i) {
        pos = hive_seg_append(buf,
                              buf_sz,
                              pos,
                              "%s%u:%u",
                              (i == 0) ? "" : ",",
                              targets[i].frag_idx,
                              targets[i].node_id);
    }
    hive_seg_append(buf, buf_sz, pos, "]");
}

static inline void hive_seg_format_frag_summary(char *buf,
                                                size_t buf_sz,
                                                const struct hifs_seg_loc *locs,
                                                size_t count)
{
    if (!buf || buf_sz == 0) {
        return;
    }
    size_t pos = 0;
    buf[0] = '\0';
    pos = hive_seg_append(buf, buf_sz, pos, "frags=[");
    if (!locs || count == 0) {
        hive_seg_append(buf, buf_sz, pos, "]");
        return;
    }
    for (size_t i = 0; i < count && pos < buf_sz; ++i) {
        pos = hive_seg_append(buf,
                              buf_sz,
                              pos,
                              "%s%llu:%u+%u",
                              (i == 0) ? "" : ",",
                              (unsigned long long)locs[i].seg_id,
                              locs[i].offset,
                              locs[i].length);
    }
    hive_seg_append(buf, buf_sz, pos, "]");
}

static inline void hive_seg_format_block_durable(char *buf,
                                                 size_t buf_sz,
                                                 const struct hive_seg_block_input *blk,
                                                 size_t frag_count,
                                                 size_t target_count)
{
    if (!buf || buf_sz == 0) {
        return;
    }
    if (!blk) {
        buf[0] = '\0';
        return;
    }
    size_t pos = 0;
    buf[0] = '\0';
    pos = hive_seg_append(buf,
                          buf_sz,
                          pos,
                          "placement_epoch=%u len=%u",
                          blk->placement_epoch,
                          blk->len);
    hive_seg_append(buf,
                    buf_sz,
                    pos,
                    " frags=%zu targets=%zu",
                    frag_count,
                    target_count);
}

static void hive_seg_log_raft_event(const char *event,
                                    const struct hive_seg_block_input *blk,
                                    uint64_t txn_id,
                                    const char *detail)
{
    if (!blk)
        return;
    struct RaftCmd cmd = {0};
    cmd.op_type = HG_OP_PUT_CLUSTER_AUDIT;
    struct RaftClusterAudit *audit = &cmd.u.cluster_audit;
    audit->cluster_id = hbc.cluster_id;
    audit->node_id = storage_node_id;
    audit->severity = 0;
    audit->reserved = 0;
    audit->timestamp_ns = hive_seg_now_ns();
    if (!event)
        event = "seg-stage";
    snprintf(audit->message,
             sizeof(audit->message),
             "%s vol=%llu block=%llu txn=%llu %s",
             event,
             (unsigned long long)blk->volume_id,
             (unsigned long long)blk->block_no,
             (unsigned long long)txn_id,
             detail ? detail : "");
    int rc = hg_raft_submit_cmd_sync(&cmd, NULL);
    if (rc != 0) {
        int err = rc < 0 ? -rc : rc;
        hifs_err("hive_segmentation: failed to append raft audit (%s) for %llu:%llu: %s",
                 event,
                 (unsigned long long)blk->volume_id,
                 (unsigned long long)blk->block_no,
                 strerror(err));
    }
}

static void hive_seg_log_wbl_placement_assigned(uint64_t txn_id,
                                                const struct hive_seg_block_input *blk,
                                                const struct hifs_fragment_target *targets,
                                                size_t target_count)
{
    if (!blk || !targets || target_count == 0)
        return;

    struct hifs_wbl_placement_rec rec = {
        .txn_id = txn_id,
        .stripe_id = blk->block_no,
    };

    if (target_count > HIFS_EC_TOTAL_SRIPES)
        target_count = HIFS_EC_TOTAL_SRIPES;
    memcpy(rec.targets,
           targets,
           target_count * sizeof(struct hifs_fragment_target));
    for (size_t i = target_count; i < HIFS_EC_TOTAL_SRIPES; ++i) {
        rec.targets[i].frag_idx = 0;
        rec.targets[i].node_id = 0;
    }

    if (hifs_wbl_mark_placement_assigned(&g_hive_guard_wbl_ctx, &rec) != 0) {
        hifs_err("hive_segmentation: unable to append WBL placement record for %llu:%llu: %s",
                 (unsigned long long)blk->volume_id,
                 (unsigned long long)blk->block_no,
                 strerror(errno));
    }
}

static void hive_seg_publish_placement_state(const struct hive_seg_block_input *blk,
                                             uint64_t txn_id,
                                             const struct hifs_fragment_target *targets,
                                             size_t target_count,
                                             const struct hifs_seg_loc *frag_locs,
                                             size_t frag_count)
{
    if (!blk || !targets || target_count == 0)
        return;

    char detail[HG_CLUSTER_AUDIT_MSG_MAX];

    hive_seg_format_target_summary(detail, sizeof(detail), targets, target_count);
    hive_seg_log_raft_event("placement-assigned", blk, txn_id, detail);

    hive_seg_format_frag_summary(detail, sizeof(detail), frag_locs, frag_count);
    hive_seg_log_raft_event("stripe-prepared", blk, txn_id, detail);

    hive_seg_format_block_durable(detail,
                                  sizeof(detail),
                                  blk,
                                  frag_count,
                                  target_count);
    hive_seg_log_raft_event("block-durable", blk, txn_id, detail);

    hive_seg_log_wbl_placement_assigned(txn_id, blk, targets, target_count);
}
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

struct hive_seg_node_stage {
    uint32_t node_id;
    struct hive_seg_stage_cursor cursor;
};

static struct hive_seg_node_stage *g_seg_node_stages;
static size_t g_seg_node_stage_count;
static size_t g_seg_node_stage_capacity;

static inline uint64_t hive_seg_stage_next_id_locked(void)
{
    return g_seg_stage_allocator.next_seg_id++;
}

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
    g_seg_stage_allocator.cursor.seg_id = hive_seg_stage_next_id_locked();
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

static inline void hive_seg_index_mem_reset(struct hive_seg_index_mem *mem)
{
    if (!mem)
        return;
    free(mem->entries);
    mem->entries = NULL;
    mem->count = 0;
    mem->capacity = 0;
}

static bool hive_seg_index_mem_append(struct hive_seg_index_mem *mem,
                                      const struct hive_seg_index_disk_rec *rec)
{
    if (!mem || !rec)
        return false;
    if (mem->count == mem->capacity) {
        size_t new_cap = mem->capacity ? (mem->capacity * 2u) : 128u;
        void *tmp = realloc(mem->entries, new_cap * sizeof(*mem->entries));
        if (!tmp)
            return false;
        mem->entries = tmp;
        mem->capacity = new_cap;
    }
    mem->entries[mem->count++] = *rec;
    return true;
}

static inline void hive_seg_cache_close_fd(int *fd)
{
    if (fd && *fd >= 0) {
        close(*fd);
        *fd = -1;
    }
}

static int hive_seg_ensure_directory(const char *path)
{
    if (!path || !*path)
        return -EINVAL;
    if (mkdir(path, 0755) == 0)
        return 0;
    if (errno == EEXIST)
        return 0;
    return -errno;
}

static int hive_seg_open_index(const char *path)
{
    int fd = open(path,
                  O_RDWR | O_CREAT | O_APPEND
#ifdef O_CLOEXEC
                  | O_CLOEXEC
#endif
                  ,
                  S_IRUSR | S_IWUSR | S_IRGRP);
    if (fd < 0)
        return -errno;
    return fd;
}

static int hive_seg_cache_load_locked(int fd, struct hive_seg_index_mem *mem)
{
    if (fd < 0 || !mem)
        return 0;
    if (lseek(fd, 0, SEEK_SET) < 0)
        return -errno;
    struct hive_seg_index_disk_rec rec;
    ssize_t rd;
    while ((rd = read(fd, &rec, sizeof(rec))) == (ssize_t)sizeof(rec)) {
        if (!hive_seg_index_mem_append(mem, &rec))
            return -ENOMEM;
    }
    if (rd < 0)
        return -errno;
    if (lseek(fd, 0, SEEK_END) < 0)
        return -errno;
    return 0;
}

static int hive_seg_cache_init(void)
{
    pthread_mutex_lock(&g_seg_cache.mu);
    if (g_seg_cache.ready) {
        pthread_mutex_unlock(&g_seg_cache.mu);
        return 0;
    }

    int rc = 0;
    const char *dirs[] = {
        HIVE_DATA_DIR,
        HIVE_GUARD_SEGMT_DIR,
        HIVE_GUARD_SEGMT_LANDING,
        HIVE_GUARD_SEGMT_OUTBOUND,
        HIVE_GUARD_SEGMT_READ_RETURN,
    };
    for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); ++i) {
        rc = hive_seg_ensure_directory(dirs[i]);
        if (rc != 0)
            goto fail;
    }

    g_seg_cache.landing_idx_fd = hive_seg_open_index(HIVE_GUARD_SEGMT_LAND_INDX);
    if (g_seg_cache.landing_idx_fd < 0) {
        rc = g_seg_cache.landing_idx_fd;
        goto fail;
    }
    g_seg_cache.outbound_idx_fd = hive_seg_open_index(HIVE_GUARD_SEGMT_OUTB_INDX);
    if (g_seg_cache.outbound_idx_fd < 0) {
        rc = g_seg_cache.outbound_idx_fd;
        goto fail;
    }
    g_seg_cache.read_return_idx_fd = hive_seg_open_index(HIVE_GUARD_SEGMT_RRET_INDX);
    if (g_seg_cache.read_return_idx_fd < 0) {
        rc = g_seg_cache.read_return_idx_fd;
        goto fail;
    }

    rc = hive_seg_cache_load_locked(g_seg_cache.landing_idx_fd,
                                    &g_seg_cache.landing_idx);
    if (rc != 0)
        goto fail;

    rc = hive_seg_cache_load_locked(g_seg_cache.outbound_idx_fd,
                                    &g_seg_cache.outbound_idx);
    if (rc != 0)
        goto fail;

    g_seg_cache.ready = true;
    pthread_mutex_unlock(&g_seg_cache.mu);
    return 0;

fail:
    hive_seg_cache_close_fd(&g_seg_cache.landing_idx_fd);
    hive_seg_cache_close_fd(&g_seg_cache.outbound_idx_fd);
    hive_seg_cache_close_fd(&g_seg_cache.read_return_idx_fd);
    hive_seg_index_mem_reset(&g_seg_cache.landing_idx);
    hive_seg_index_mem_reset(&g_seg_cache.outbound_idx);
    pthread_mutex_unlock(&g_seg_cache.mu);
    return rc;
}

static void hive_seg_cache_shutdown(void)
{
    pthread_mutex_lock(&g_seg_cache.mu);
    g_seg_cache.ready = false;
    hive_seg_cache_close_fd(&g_seg_cache.landing_idx_fd);
    hive_seg_cache_close_fd(&g_seg_cache.outbound_idx_fd);
    hive_seg_cache_close_fd(&g_seg_cache.read_return_idx_fd);
    hive_seg_index_mem_reset(&g_seg_cache.landing_idx);
    hive_seg_index_mem_reset(&g_seg_cache.outbound_idx);
    pthread_mutex_unlock(&g_seg_cache.mu);
}

static inline int hive_seg_write_all(int fd, const void *buf, size_t len)
{
    const uint8_t *p = buf;
    size_t written = 0;
    while (written < len) {
        ssize_t rc = write(fd, p + written, len - written);
        if (rc < 0) {
            if (errno == EINTR)
                continue;
            return -errno;
        }
        written += (size_t)rc;
    }
    return 0;
}

static inline int hive_seg_pwrite_all(int fd,
                                      const void *buf,
                                      size_t len,
                                      off_t offset)
{
    const uint8_t *p = buf;
    size_t written = 0;
    while (written < len) {
        ssize_t rc = pwrite(fd, p + written, len - written, offset + (off_t)written);
        if (rc < 0) {
            if (errno == EINTR)
                continue;
            return -errno;
        }
        written += (size_t)rc;
    }
    return 0;
}

static void hive_seg_landing_path(const struct hive_seg_block_input *blk,
                                  char *out,
                                  size_t out_sz)
{
    if (!out || out_sz == 0)
        return;
    if (!blk || !blk->has_landing_loc) {
        snprintf(out, out_sz, HIVE_GUARD_SEGMT_LANDING "/pending.seg");
        return;
    }
    snprintf(out,
             out_sz,
             HIVE_GUARD_SEGMT_LANDING "/seg-%llu-%u.seg",
             (unsigned long long)blk->landing_loc.seg_id,
             blk->landing_loc.offset);
}

static bool hive_seg_encode_block(const struct hive_seg_block_input *blk,
                                  struct hifs_ec_stripe_set *out)
{
    if (!blk || !out)
        return false;
    enum hifs_hash_algorithm algo = blk->hash_algo;
    if (algo == HIFS_HASH_ALGO_NONE)
        algo = HIFS_HASH_ALGO_SHA256;
    const uint8_t *pre_hash = blk->has_hash ? blk->hash : NULL;
    return hifs_volume_block_ec_encode(blk->data, blk->len, algo, pre_hash, out);
}

static int hive_seg_write_landing_file(uint64_t txn_id,
                                       uint32_t generation,
                                       const struct hive_seg_block_input *blk,
                                       const struct hifs_ec_stripe_set *ec)
{
    if (!blk || !blk->data || blk->len == 0)
        return -EINVAL;

    char path[PATH_MAX];
    hive_seg_landing_path(blk, path, sizeof(path));

    struct hive_seg_block_file_hdr hdr = {
        .magic = HIVE_SEG_BLOCK_FILE_MAGIC,
        .version = HIVE_SEG_BLOCK_FILE_VERSION,
        .chunk_count = (uint16_t)(ec ? ec->chunk_count : 0u),
        .chunk_len = (uint32_t)(ec ? ec->chunk_len : 0u),
        .block_bytes = blk->len,
        .landing_length = blk->has_landing_loc ? blk->landing_loc.length : blk->len,
        .seg_id = blk->has_landing_loc ? blk->landing_loc.seg_id : 0,
        .volume_id = blk->volume_id,
        .block_no = blk->block_no,
        .txn_id = txn_id,
        .generation = generation,
        .landing_offset = blk->has_landing_loc ? blk->landing_loc.offset : 0u,
        .hash_algo = blk->hash_algo,
        .stripe_algo = blk->stripe_algo,
    };
    if (blk->has_hash)
        memcpy(hdr.hash, blk->hash, sizeof(hdr.hash));
    else
        memset(hdr.hash, 0, sizeof(hdr.hash));

    pthread_mutex_lock(&g_seg_landing_io_mu);
    int fd = open(path,
                  O_WRONLY | O_CREAT | O_TRUNC
#ifdef O_CLOEXEC
                  | O_CLOEXEC
#endif
                  ,
                  S_IRUSR | S_IWUSR | S_IRGRP);
    if (fd < 0) {
        int err = errno;
        pthread_mutex_unlock(&g_seg_landing_io_mu);
        return -err;
    }

    int rc = hive_seg_write_all(fd, &hdr, sizeof(hdr));
    if (rc == 0) {
        if (ec && ec->chunks && ec->chunk_count > 0) {
            off_t offset = (off_t)sizeof(hdr);
            for (size_t i = 0; i < ec->chunk_count && rc == 0; ++i) {
                rc = hive_seg_pwrite_all(fd,
                                         ec->chunks[i],
                                         ec->chunk_len,
                                         offset);
                offset += (off_t)ec->chunk_len;
            }
        } else {
            rc = hive_seg_pwrite_all(fd,
                                     blk->data,
                                     blk->len,
                                     (off_t)sizeof(hdr));
        }
    }

    close(fd);
    pthread_mutex_unlock(&g_seg_landing_io_mu);
    return rc;
}

static int hive_seg_cache_record_landing(uint64_t txn_id,
                                         uint32_t generation,
                                         const struct hive_seg_block_input *blk)
{
    if (!blk || !blk->has_landing_loc)
        return -EINVAL;

    struct hive_seg_index_disk_rec rec = {
        .seg_id = blk->landing_loc.seg_id,
        .volume_id = blk->volume_id,
        .block_no = blk->block_no,
        .txn_id = txn_id,
        .generation = generation,
        .state = HIFS_STRIPE_LANDING_ECCODED,
        .offset = blk->landing_loc.offset,
        .length = blk->landing_loc.length,
        .reserved = 0,
    };

    pthread_mutex_lock(&g_seg_cache.mu);
    if (!g_seg_cache.ready) {
        pthread_mutex_unlock(&g_seg_cache.mu);
        return -ESHUTDOWN;
    }
    if (!hive_seg_index_mem_append(&g_seg_cache.landing_idx, &rec)) {
        pthread_mutex_unlock(&g_seg_cache.mu);
        return -ENOMEM;
    }
    int fd = g_seg_cache.landing_idx_fd;
    int rc = 0;
    if (fd >= 0)
        rc = hive_seg_write_all(fd, &rec, sizeof(rec));
    pthread_mutex_unlock(&g_seg_cache.mu);
    return rc;
}

static inline void hive_seg_log_landing_eccoded(const struct hive_seg_block_input *blk,
                                                uint64_t txn_id)
{
    if (!blk)
        return;
    struct hifs_wbl_landing_rec landing = {
        .txn_id = txn_id,
        .stripe_id = blk->block_no,
    };
    if (blk->has_landing_loc)
        landing.landing_loc = blk->landing_loc;
    else {
        landing.landing_loc.seg_id = blk->block_no;
        landing.landing_loc.offset = 0;
        landing.landing_loc.length = blk->len;
    }
    if (hifs_wbl_mark_landing_eccoded(&g_hive_guard_wbl_ctx, &landing) != 0) {
        hifs_err("hive_segmentation: unable to append WBL landing event for %llu:%llu: %s",
                 (unsigned long long)blk->volume_id,
                 (unsigned long long)blk->block_no,
                 strerror(errno));
    }
}

static int hive_seg_stage_landing_eccoded(struct hive_seg_block_input *blk,
                                          uint64_t txn_id,
                                          uint32_t generation)
{
    if (!blk)
        return -EINVAL;
    if (!blk->has_landing_loc)
        return -EIO;

    int rc = hive_seg_cache_init();
    if (rc != 0)
        return rc;

    struct hifs_ec_stripe_set ec = {0};
    struct hifs_ec_stripe_set *ec_ptr = NULL;
    if (blk->data && blk->len > 0 && hive_seg_encode_block(blk, &ec))
        ec_ptr = &ec;

    if (!ec_ptr)
        return -EIO;

    rc = hive_seg_prepare_fragments(blk, txn_id, generation, ec_ptr);
    if (rc != 0)
        goto out;

    rc = hive_seg_write_landing_file(txn_id, generation, blk, ec_ptr);
    if (rc != 0)
        goto out;

    rc = hive_seg_cache_record_landing(txn_id, generation, blk);
    if (rc != 0)
        goto out;

    hive_seg_log_landing_eccoded(blk, txn_id);

out:
    if (ec_ptr)
        hifs_volume_block_ec_free(ec_ptr);
    return rc;
}

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

static struct hive_seg_node_stage *
hive_seg_node_stage_acquire_locked(uint32_t node_id)
{
    for (size_t i = 0; i < g_seg_node_stage_count; ++i) {
        if (g_seg_node_stages[i].node_id == node_id)
            return &g_seg_node_stages[i];
    }

    size_t needed = g_seg_node_stage_count + 1;
    if (needed > g_seg_node_stage_capacity) {
        size_t new_cap = g_seg_node_stage_capacity ? (g_seg_node_stage_capacity * 2u) : 8u;
        void *tmp = realloc(g_seg_node_stages,
                            new_cap * sizeof(*g_seg_node_stages));
        if (!tmp)
            return NULL;
        g_seg_node_stages = tmp;
        g_seg_node_stage_capacity = new_cap;
    }

    struct hive_seg_node_stage *stage = &g_seg_node_stages[g_seg_node_stage_count++];
    stage->node_id = node_id;
    stage->cursor.seg_id = 0;
    stage->cursor.bytes_used = 0;
    return stage;
}

static int hive_seg_stage_fragment_locked(uint32_t node_id,
                                          uint32_t len,
                                          struct hifs_seg_loc *loc)
{
    if (!loc || len == 0)
        return -EINVAL;
    struct hive_seg_node_stage *stage = hive_seg_node_stage_acquire_locked(node_id);
    if (!stage)
        return -ENOMEM;
    uint32_t aligned = hive_seg_align_len(len);
    if (aligned == 0)
        aligned = len;
    if (stage->cursor.seg_id == 0 ||
        stage->cursor.bytes_used + aligned > HIFS_SEGMENT_SIZE) {
        stage->cursor.seg_id = hive_seg_stage_next_id_locked();
        stage->cursor.bytes_used = 0;
    }
    loc->seg_id = stage->cursor.seg_id;
    loc->offset = stage->cursor.bytes_used;
    loc->length = len;
    stage->cursor.bytes_used += aligned;
    return 0;
}

static void hive_seg_fragment_path(char *out,
                                   size_t out_sz,
                                   uint32_t node_id,
                                   uint64_t seg_id)
{
    if (!out || out_sz == 0)
        return;
    snprintf(out,
             out_sz,
             HIVE_GUARD_SEGMT_LANDING "/node-%u-seg-%llu.seg",
             node_id,
             (unsigned long long)seg_id);
}

static int hive_seg_write_fragment(uint32_t node_id,
                                   const struct hifs_seg_loc *loc,
                                   const uint8_t *data)
{
    if (!loc || !data || loc->length == 0)
        return -EINVAL;

    char path[PATH_MAX];
    hive_seg_fragment_path(path, sizeof(path), node_id, loc->seg_id);

    pthread_mutex_lock(&g_seg_landing_io_mu);
    int fd = open(path,
                  O_RDWR | O_CREAT
#ifdef O_CLOEXEC
                  | O_CLOEXEC
#endif
                  ,
                  S_IRUSR | S_IWUSR | S_IRGRP);
    if (fd < 0) {
        int err = errno;
        pthread_mutex_unlock(&g_seg_landing_io_mu);
        return -err;
    }

    int rc = hive_seg_pwrite_all(fd,
                                 data,
                                 loc->length,
                                 (off_t)loc->offset);
    close(fd);
    pthread_mutex_unlock(&g_seg_landing_io_mu);
    return rc;
}

static int hive_seg_cache_record_fragment(uint64_t txn_id,
                                          uint32_t generation,
                                          const struct hive_seg_block_input *blk,
                                          uint32_t frag_idx,
                                          uint32_t node_id,
                                          const struct hifs_seg_loc *loc)
{
    (void)frag_idx;
    if (!blk || !loc)
        return -EINVAL;

    struct hive_seg_index_disk_rec rec = {
        .seg_id = loc->seg_id,
        .volume_id = blk->volume_id,
        .block_no = blk->block_no,
        .txn_id = txn_id,
        .generation = generation,
        .state = HIFS_STRIPE_PREPARED,
        .offset = loc->offset,
        .length = loc->length,
        .reserved = node_id,
    };

    pthread_mutex_lock(&g_seg_cache.mu);
    if (!g_seg_cache.ready) {
        pthread_mutex_unlock(&g_seg_cache.mu);
        return -ESHUTDOWN;
    }
    if (!hive_seg_index_mem_append(&g_seg_cache.outbound_idx, &rec)) {
        pthread_mutex_unlock(&g_seg_cache.mu);
        return -ENOMEM;
    }
    int fd = g_seg_cache.outbound_idx_fd;
    int rc = 0;
    if (fd >= 0)
        rc = hive_seg_write_all(fd, &rec, sizeof(rec));
    pthread_mutex_unlock(&g_seg_cache.mu);
    return rc;
}

static inline uint64_t hive_seg_block_stripe_hint(const struct hive_seg_block_input *blk)
{
    uint64_t g = blk ? blk->block_no : 0;
    if (blk && blk->has_stripe_id)
        memcpy(&g, blk->stripe_id, sizeof(g));
    return g;
}

static void hive_seg_build_placement_seed(const struct hive_seg_block_input *blk,
                                          uint64_t txn_id,
                                          uint32_t generation,
                                          struct hifs_placement_seed *seed)
{
    memset(seed, 0, sizeof(*seed));
    seed->cluster_id = hbc.cluster_id;
    seed->fs_id = blk->volume_id;
    seed->inode_id = blk->volume_id;
    seed->stripe_id = hive_seg_block_stripe_hint(blk);
    seed->txn_id = txn_id;
    seed->lba_start = blk->block_no;
    seed->block_count = 1;
    seed->generation = generation;
    seed->placement_epoch = blk->placement_epoch;
    snprintf(seed->block_id,
             sizeof(seed->block_id),
             "%llu:%llu",
             (unsigned long long)blk->volume_id,
             (unsigned long long)blk->block_no);
}

static int hive_seg_log_stripe_prepared(uint64_t txn_id,
                                        uint64_t stripe_id,
                                        uint32_t generation,
                                        const struct hifs_seg_loc *frag_locs,
                                        size_t frag_count)
{
    struct hifs_wbl_prepared_rec rec = {
        .txn_id = txn_id,
        .stripe_id = stripe_id,
        .generation = generation,
    };
    if (frag_locs && frag_count > 0) {
        if (frag_count > HIFS_EC_TOTAL_SRIPES)
            frag_count = HIFS_EC_TOTAL_SRIPES;
        memcpy(rec.frag_locs,
               frag_locs,
               frag_count * sizeof(struct hifs_seg_loc));
    }
    if (hifs_wbl_mark_stripe_prepared(&g_hive_guard_wbl_ctx, &rec) != 0) {
        int err = errno ? -errno : -EIO;
        hifs_err("hive_segmentation: unable to append WBL stripe prepared record for txn=%llu: %s",
                 (unsigned long long)txn_id,
                 strerror(errno));
        return err;
    }
    return 0;
}

static int hive_seg_prepare_fragments(struct hive_seg_block_input *blk,
                                      uint64_t txn_id,
                                      uint32_t generation,
                                      const struct hifs_ec_stripe_set *ec)
{
    if (!blk || !ec || !ec->chunks || ec->chunk_count == 0 || ec->chunk_len == 0)
        return -EINVAL;
    if (ec->chunk_len > UINT32_MAX)
        return -EOVERFLOW;

    struct hifs_placement_seed seed;
    hive_seg_build_placement_seed(blk, txn_id, generation, &seed);
    struct hifs_placement_result placement = {0};
    int rc = hifs_place_stripe(&seed, &placement);
    if (rc != 0)
        return rc;

    size_t frag_count = ec->chunk_count;
    if (frag_count > HIFS_EC_TOTAL_SRIPES)
        frag_count = HIFS_EC_TOTAL_SRIPES;
    if (placement.target_count < frag_count)
        frag_count = placement.target_count;
    if (frag_count == 0)
        return -EINVAL;

    blk->has_frag_targets = true;
    blk->frag_target_count = placement.target_count;
    memcpy(blk->frag_targets,
           placement.targets,
           sizeof(placement.targets));

    struct hifs_seg_loc frag_locs[HIFS_EC_TOTAL_SRIPES];
    memset(frag_locs, 0, sizeof(frag_locs));

    uint32_t chunk_len = (uint32_t)ec->chunk_len;
    for (size_t i = 0; i < frag_count; ++i) {
        if (!ec->chunks[i])
            return -EIO;
        uint32_t node_id = placement.targets[i].node_id;
        if (node_id == 0)
            return -ENODEV;
        pthread_mutex_lock(&g_seg_stage_allocator.mu);
        rc = hive_seg_stage_fragment_locked(node_id, chunk_len, &frag_locs[i]);
        pthread_mutex_unlock(&g_seg_stage_allocator.mu);
        if (rc != 0)
            return rc;
        rc = hive_seg_write_fragment(node_id, &frag_locs[i], ec->chunks[i]);
        if (rc != 0)
            return rc;
        rc = hive_seg_cache_record_fragment(txn_id,
                                            generation,
                                            blk,
                                            (uint32_t)i,
                                            node_id,
                                            &frag_locs[i]);
        if (rc != 0)
            return rc;
        rc = hive_seg_mark_outbound_queued(txn_id,
                                           generation,
                                           blk,
                                           node_id,
                                           &frag_locs[i]);
        if (rc != 0)
            return rc;
    }

    rc = hive_seg_log_stripe_prepared(txn_id,
                                      blk->block_no,
                                      generation,
                                      frag_locs,
                                      frag_count);
    if (rc != 0)
        return rc;

    hive_seg_publish_placement_state(blk,
                                     txn_id,
                                     blk->frag_targets,
                                     frag_count,
                                     frag_locs,
                                     frag_count);

    hifs_guard_notify_write_ack(blk->volume_id,
                                blk->block_no,
                                blk->hash,
                                HIFS_BLOCK_HASH_SIZE);
    return 0;
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
    dst->has_frag_targets = false;
    dst->frag_target_count = 0;
    memset(dst->frag_targets, 0, sizeof(dst->frag_targets));
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
}

// TODO: Now that we have the incoming data on permanent disk, we can 

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
    int rc = hive_seg_stage_landing_eccoded(blk, txn_id, generation);
    if (rc != 0)
        return rc;
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

    int rc = hive_seg_cache_init();
    if (rc != 0) {
        atomic_store(&g_seg_started, false);
        return rc;
    }

    pthread_mutex_lock(&g_seg_mu);
    g_seg_head = g_seg_tail = g_seg_size = 0;
    g_seg_running = true;
    pthread_mutex_unlock(&g_seg_mu);

    rc = hive_seg_setup_workers();
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

    hive_seg_cache_shutdown();
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
