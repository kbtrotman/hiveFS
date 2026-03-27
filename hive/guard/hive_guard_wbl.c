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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "hive_guard_wbl.h"

static int hifs_wbl_rotate_if_needed(struct hifs_wbl_ctx *ctx, size_t next_write);

struct hifs_wbl_async_req {
    struct hifs_wbl_async_req *next;
    struct hifs_wbl_ctx *ctx;
    size_t len;
    uint8_t data[];
};

static pthread_mutex_t g_wbl_queue_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_wbl_queue_cv = PTHREAD_COND_INITIALIZER;
static struct hifs_wbl_async_req *g_wbl_queue_head;
static struct hifs_wbl_async_req *g_wbl_queue_tail;
static uint64_t g_wbl_pending;
static unsigned int g_wbl_active_ctx;
static pthread_t g_wbl_worker;
static bool g_wbl_worker_running;
static bool g_wbl_worker_stop;
static int g_wbl_worker_errno;

static void hifs_wbl_worker_note_error(int err)
{
    pthread_mutex_lock(&g_wbl_queue_mu);
    if (g_wbl_worker_errno == 0) {
        g_wbl_worker_errno = err ? err : EIO;
        g_wbl_worker_stop = true;
    }
    pthread_cond_broadcast(&g_wbl_queue_cv);
    pthread_mutex_unlock(&g_wbl_queue_mu);
}

static void *hifs_wbl_worker_main(void *arg)
{
    (void)arg;

    for (;;) {
        struct hifs_wbl_async_req *req = NULL;
        int pending_err;

        pthread_mutex_lock(&g_wbl_queue_mu);
        while (!g_wbl_worker_stop && !g_wbl_queue_head) {
            pthread_cond_wait(&g_wbl_queue_cv, &g_wbl_queue_mu);
        }
        if (g_wbl_worker_stop && !g_wbl_queue_head) {
            pthread_mutex_unlock(&g_wbl_queue_mu);
            break;
        }
        req = g_wbl_queue_head;
        g_wbl_queue_head = req->next;
        if (!g_wbl_queue_head) {
            g_wbl_queue_tail = NULL;
        }
        pending_err = g_wbl_worker_errno;
        pthread_mutex_unlock(&g_wbl_queue_mu);

        if (!req) {
            continue;
        }

        if (pending_err == 0 && req->ctx && req->ctx->fd >= 0) {
            if (hifs_wbl_rotate_if_needed(req->ctx, req->len) < 0) {
                hifs_wbl_worker_note_error(errno);
            } else {
                size_t written = 0;
                while (written < req->len) {
                    ssize_t n = write(req->ctx->fd,
                                      req->data + written,
                                      req->len - written);
                    if (n < 0) {
                        hifs_wbl_worker_note_error(errno);
                        break;
                    }
                    written += (size_t)n;
                }
                if (written != req->len) {
                    hifs_wbl_worker_note_error(EIO);
                }
            }
        }

        free(req);

        pthread_mutex_lock(&g_wbl_queue_mu);
        if (g_wbl_pending > 0) {
            g_wbl_pending--;
            if (g_wbl_pending == 0) {
                pthread_cond_broadcast(&g_wbl_queue_cv);
            }
        }
        pthread_mutex_unlock(&g_wbl_queue_mu);
    }

    pthread_mutex_lock(&g_wbl_queue_mu);
    if (g_wbl_pending == 0) {
        pthread_cond_broadcast(&g_wbl_queue_cv);
    }
    pthread_mutex_unlock(&g_wbl_queue_mu);

    return NULL;
}

static int hifs_wbl_worker_start(void)
{
    int rc = 0;

    pthread_mutex_lock(&g_wbl_queue_mu);
    if (!g_wbl_worker_running) {
        g_wbl_worker_stop = false;
        g_wbl_worker_errno = 0;
        rc = pthread_create(&g_wbl_worker, NULL, hifs_wbl_worker_main, NULL);
        if (rc != 0) {
            errno = rc;
        } else {
            g_wbl_worker_running = true;
        }
    }
    pthread_mutex_unlock(&g_wbl_queue_mu);

    return rc == 0 ? 0 : -1;
}

static void hifs_wbl_worker_shutdown(void)
{
    struct hifs_wbl_async_req *req;

    pthread_mutex_lock(&g_wbl_queue_mu);
    if (!g_wbl_worker_running) {
        pthread_mutex_unlock(&g_wbl_queue_mu);
        return;
    }
    g_wbl_worker_stop = true;
    pthread_cond_broadcast(&g_wbl_queue_cv);
    pthread_mutex_unlock(&g_wbl_queue_mu);

    pthread_join(g_wbl_worker, NULL);

    pthread_mutex_lock(&g_wbl_queue_mu);
    g_wbl_worker_running = false;
    g_wbl_worker_stop = false;
    g_wbl_worker_errno = 0;
    g_wbl_pending = 0;
    while ((req = g_wbl_queue_head) != NULL) {
        g_wbl_queue_head = req->next;
        free(req);
    }
    g_wbl_queue_tail = NULL;
    pthread_mutex_unlock(&g_wbl_queue_mu);
}

static int hifs_wbl_async_submit(struct hifs_wbl_async_req *req)
{
    int err = 0;

    pthread_mutex_lock(&g_wbl_queue_mu);
    if (!g_wbl_worker_running || g_wbl_worker_stop || g_wbl_worker_errno != 0) {
        err = g_wbl_worker_errno ? g_wbl_worker_errno : EPIPE;
        errno = err;
        pthread_mutex_unlock(&g_wbl_queue_mu);
        return -1;
    }

    if (!g_wbl_queue_head) {
        g_wbl_queue_head = req;
    } else {
        g_wbl_queue_tail->next = req;
    }
    g_wbl_queue_tail = req;
    g_wbl_pending++;
    pthread_cond_signal(&g_wbl_queue_cv);
    pthread_mutex_unlock(&g_wbl_queue_mu);

    return 0;
}

static int hifs_wbl_wait_for_idle(void)
{
    pthread_mutex_lock(&g_wbl_queue_mu);
    while (g_wbl_pending > 0 && g_wbl_worker_errno == 0) {
        pthread_cond_wait(&g_wbl_queue_cv, &g_wbl_queue_mu);
    }
    int err = g_wbl_worker_errno;
    pthread_mutex_unlock(&g_wbl_queue_mu);

    if (err != 0) {
        errno = err;
        return -1;
    }
    return 0;
}

struct hifs_wbl_ctx g_hive_guard_wbl_ctx = {
    .fd = -1,
    .mu = PTHREAD_MUTEX_INITIALIZER,
    .mu_inited = true,
};

static pthread_mutex_t g_wbl_rotate_mu = PTHREAD_MUTEX_INITIALIZER;
static uint32_t g_wbl_next_slot;

static int hifs_wbl_ctx_ensure_mutex(struct hifs_wbl_ctx *ctx)
{
    int rc;

    if (!ctx) {
        errno = EINVAL;
        return -1;
    }

    if (ctx->mu_inited) {
        return 0;
    }

    rc = pthread_mutex_init(&ctx->mu, NULL);
    if (rc != 0) {
        errno = rc;
        return -1;
    }

    ctx->mu_inited = true;
    return 0;
}

static inline uint64_t hifs_wbl_next_seqno(struct hifs_wbl_ctx *ctx)
{
    return ctx->next_seqno++;
}

static inline uint32_t hifs_wbl_calc_crc32(const void *data, size_t len)
{
    const uint8_t *p = data;
    uint32_t crc = 0xffffffffu;
    size_t i;
    size_t j;

    for (i = 0; i < len; i++) {
        crc ^= p[i];
        for (j = 0; j < 8; j++) {
            uint32_t mask = -(crc & 1u);
            crc = (crc >> 1) ^ (0xedb88320u & mask);
        }
    }

    return ~crc;
}

static int hifs_wbl_current_slot(const char *path)
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

    return (int)(slot % HIFS_WBL_MAX_LOGS);
}

static void hifs_wbl_slot_path_for(const char *path,
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

    snprintf(out + dir_len, out_sz - dir_len, "wbl-%u.log", slot);
}

static int hifs_wbl_rotate_locked(struct hifs_wbl_ctx *ctx)
{
    char next_path[sizeof(ctx->path)];
    int current_slot;
    unsigned int next_slot;
    int new_fd;

    if (!ctx || ctx->fd < 0) {
        errno = EINVAL;
        return -1;
    }

    current_slot = hifs_wbl_current_slot(ctx->path);
    if (current_slot >= 0) {
        next_slot = (unsigned int)((current_slot + 1) % HIFS_WBL_MAX_LOGS);
    } else {
        next_slot = g_wbl_next_slot % HIFS_WBL_MAX_LOGS;
    }
    g_wbl_next_slot = (next_slot + 1) % HIFS_WBL_MAX_LOGS;

    hifs_wbl_slot_path_for(ctx->path, next_slot, next_path, sizeof(next_path));

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

static int hifs_wbl_rotate_if_needed(struct hifs_wbl_ctx *ctx, size_t next_write)
{
    struct stat st;
    uint64_t size;
    int rc = 0;

    if (!ctx || ctx->fd < 0) {
        errno = EINVAL;
        return -1;
    }

    pthread_mutex_lock(&g_wbl_rotate_mu);
    if (fstat(ctx->fd, &st) < 0) {
        rc = -1;
        goto out_unlock;
    }

    size = (uint64_t)st.st_size;
    if (size >= HIFS_WBL_MAX_BYTES ||
        next_write >= HIFS_WBL_MAX_BYTES ||
        HIFS_WBL_MAX_BYTES - size <= next_write) {
        rc = hifs_wbl_rotate_locked(ctx);
    }

out_unlock:
    pthread_mutex_unlock(&g_wbl_rotate_mu);
    return rc;
}

int hifs_wbl_open(struct hifs_wbl_ctx *ctx, const char *path, bool create)
{
    int flags;
    int fd;
    bool need_worker = false;

    if (!ctx || !path) {
        errno = EINVAL;
        return -1;
    }

    if (hifs_wbl_ctx_ensure_mutex(ctx) < 0) {
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

    pthread_mutex_lock(&g_wbl_queue_mu);
    if (g_wbl_active_ctx == 0) {
        need_worker = true;
    }
    g_wbl_active_ctx++;
    pthread_mutex_unlock(&g_wbl_queue_mu);

    if (need_worker && hifs_wbl_worker_start() < 0) {
        int saved = errno;
        pthread_mutex_lock(&g_wbl_queue_mu);
        if (g_wbl_active_ctx > 0) {
            g_wbl_active_ctx--;
        }
        pthread_mutex_unlock(&g_wbl_queue_mu);
        close(fd);
        errno = saved;
        return -1;
    }

    ctx->fd = fd;
    ctx->next_seqno = 0;
    strncpy(ctx->path, path, sizeof(ctx->path) - 1);
    ctx->path[sizeof(ctx->path) - 1] = '\0';

    return 0;
}

int hifs_wbl_close(struct hifs_wbl_ctx *ctx)
{
    int close_rc = 0;
    int flush_rc = 0;
    int saved_flush_errno = 0;
    bool stop_worker = false;

    if (!ctx) {
        errno = EINVAL;
        return -1;
    }

    if (hifs_wbl_ctx_ensure_mutex(ctx) < 0) {
        return -1;
    }

    pthread_mutex_lock(&ctx->mu);
    if (ctx->fd < 0) {
        pthread_mutex_unlock(&ctx->mu);
        return 0;
    }

    flush_rc = hifs_wbl_wait_for_idle();
    if (flush_rc < 0) {
        saved_flush_errno = errno;
    }

    int fd = ctx->fd;
    ctx->fd = -1;
    ctx->next_seqno = 0;
    ctx->path[0] = '\0';
    pthread_mutex_unlock(&ctx->mu);

    if (close(fd) < 0) {
        close_rc = -1;
    }

    pthread_mutex_lock(&g_wbl_queue_mu);
    if (g_wbl_active_ctx > 0) {
        g_wbl_active_ctx--;
        if (g_wbl_active_ctx == 0) {
            stop_worker = true;
        }
    }
    pthread_mutex_unlock(&g_wbl_queue_mu);

    if (stop_worker) {
        hifs_wbl_worker_shutdown();
    }

    if (close_rc < 0) {
        return -1;
    }

    if (flush_rc < 0) {
        errno = saved_flush_errno;
        return -1;
    }

    return 0;
}
int hifs_wbl_append(struct hifs_wbl_ctx *ctx,
                    uint16_t rec_type,
                    const void *payload,
                    uint32_t payload_len)
{
    struct hifs_wbl_hdr hdr;
    struct hifs_wbl_async_req *req;
    struct timespec ts;
    uint64_t seqno;
    uint32_t crc;
    size_t total_len;

    if (!ctx) {
        errno = EINVAL;
        return -1;
    }

    if (payload_len > 0 && !payload) {
        errno = EINVAL;
        return -1;
    }

    if (rec_type == 0) {
        errno = EINVAL;
        return -1;
    }

    clock_gettime(CLOCK_REALTIME, &ts);

    if (hifs_wbl_ctx_ensure_mutex(ctx) < 0) {
        return -1;
    }

    pthread_mutex_lock(&ctx->mu);
    if (ctx->fd < 0) {
        pthread_mutex_unlock(&ctx->mu);
        errno = EINVAL;
        return -1;
    }
    seqno = hifs_wbl_next_seqno(ctx);
    pthread_mutex_unlock(&ctx->mu);

    hdr.magic = HIFS_WBL_MAGIC_VALUE;
    hdr.version = HIFS_WBL_VERSION;
    hdr.type = rec_type;
    hdr.length = payload_len;
    hdr.seqno = seqno;
    hdr.ts_nsec = (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
    hdr.crc32 = 0;
    crc = payload_len ? hifs_wbl_calc_crc32(payload, payload_len) : 0;
    hdr.crc32 = crc;

    total_len = sizeof(hdr) + (size_t)payload_len;
    req = malloc(sizeof(*req) + total_len);
    if (!req) {
        errno = ENOMEM;
        return -1;
    }
    req->ctx = ctx;
    req->len = total_len;
    req->next = NULL;
    memcpy(req->data, &hdr, sizeof(hdr));
    if (payload_len) {
        memcpy(req->data + sizeof(hdr), payload, payload_len);
    }

    if (hifs_wbl_async_submit(req) < 0) {
        int saved = errno;
        free(req);
        errno = saved;
        return -1;
    }

    return 0;
}






int hifs_wbl_replay(struct hifs_wbl_ctx *ctx,
                    int (*apply_fn)(const struct hifs_wbl_hdr *hdr,
                                    const void *payload,
                                    void *arg),
                    void *arg);



int hifs_wbl_checkpoint(...);






// Helpers that just make it easier to call the correct WBL record type at the right times in the data flow.

static inline int hifs_wbl_emit(struct hifs_wbl_ctx *ctx,
                                enum hifs_wbl_rec_type type,
                                const void *payload,
                                size_t payload_len)
{
    if (!ctx || !payload) {
        errno = EINVAL;
        return -1;
    }

    return hifs_wbl_append(ctx,
                           (uint16_t)type,
                           payload,
                           (uint32_t)payload_len);
}

int hifs_wbl_mark_write_intent(struct hifs_wbl_ctx *ctx,
                               const struct hifs_wbl_write_intent_rec *rec)
{
    return hifs_wbl_emit(ctx,
                         HIFS_WBL_REC_WRITE_RECV,
                         rec,
                         sizeof(*rec));
}

int hifs_wbl_mark_landing_eccoded(struct hifs_wbl_ctx *ctx,
                                  const struct hifs_wbl_landing_rec *rec)
{
    return hifs_wbl_emit(ctx,
                         HIFS_WBL_REC_LANDING_WRITTEN,
                         rec,
                         sizeof(*rec));
}

int hifs_wbl_mark_stripe_prepared(struct hifs_wbl_ctx *ctx,
                                  const struct hifs_wbl_prepared_rec *rec)
{
    return hifs_wbl_emit(ctx,
                         HIFS_WBL_REC_STRIPE_PREPARED,
                         rec,
                         sizeof(*rec));
}

int hifs_wbl_mark_placement_assigned(struct hifs_wbl_ctx *ctx,
                                     const struct hifs_wbl_placement_rec *rec)
{
    return hifs_wbl_emit(ctx,
                         HIFS_WBL_REC_PLACEMENT_ASSIGNED,
                         rec,
                         sizeof(*rec));
}

int hifs_wbl_mark_outbound_queued(struct hifs_wbl_ctx *ctx,
                                  const struct hifs_wbl_outbound_queued_rec *rec)
{
    return hifs_wbl_emit(ctx,
                         HIFS_WBL_REC_OUTBOUND_QUEUED,
                         rec,
                         sizeof(*rec));
}

int hifs_wbl_mark_fragment_sent(struct hifs_wbl_ctx *ctx,
                                const struct hifs_wbl_fragment_event_rec *rec)
{
    return hifs_wbl_emit(ctx,
                         HIFS_WBL_REC_FRAGMENT_SENT,
                         rec,
                         sizeof(*rec));
}

int hifs_wbl_mark_fragment_received(struct hifs_wbl_ctx *ctx,
                                    const struct hifs_wbl_fragment_event_rec *rec)
{
    return hifs_wbl_emit(ctx,
                         HIFS_WBL_REC_FRAGMENT_WRITE_INDX,
                         rec,
                         sizeof(*rec));
}

int hifs_wbl_mark_fragment_acked(struct hifs_wbl_ctx *ctx,
                                 const struct hifs_wbl_fragment_event_rec *rec)
{
    return hifs_wbl_emit(ctx,
                         HIFS_WBL_REC_FRAGMENT_ACKED,
                         rec,
                         sizeof(*rec));
}

int hifs_wbl_mark_committed(struct hifs_wbl_ctx *ctx,
                            const struct hifs_wbl_commit_rec *rec)
{
    return hifs_wbl_emit(ctx,
                         HIFS_WBL_REC_STRIPE_COMMITTED,
                         rec,
                         sizeof(*rec));
}

int hifs_wbl_mark_persisting(struct hifs_wbl_ctx *ctx,
                             const struct hifs_wbl_persisting_rec *rec)
{
    return hifs_wbl_emit(ctx,
                         HIFS_WBL_REC_STRIPE_PERSISTING,
                         rec,
                         sizeof(*rec));
}

int hifs_wbl_mark_persisted(struct hifs_wbl_ctx *ctx,
                             const struct hifs_wbl_persisted_rec *rec)
{
    return hifs_wbl_emit(ctx,
                         HIFS_WBL_REC_STRIPE_REMOVE_CACHE,
                         rec,
                         sizeof(*rec));
}

int hifs_wbl_mark_reclaimable(struct hifs_wbl_ctx *ctx,
                              const struct hifs_wbl_reclaimable_rec *rec)
{
    return hifs_wbl_emit(ctx,
                         HIFS_WBL_REC_STRIPE_RECLAIMABLE,
                         rec,
                         sizeof(*rec));
}
