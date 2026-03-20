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
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>

#include "hive_guard_wbl.h"

struct hifs_wbl_ctx g_hive_guard_wbl_ctx = {
    .fd = -1,
};

static pthread_mutex_t g_wbl_rotate_mu = PTHREAD_MUTEX_INITIALIZER;
static uint32_t g_wbl_next_slot;

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

int hifs_wbl_close(struct hifs_wbl_ctx *ctx)
{
    int rc;

    if (!ctx) {
        errno = EINVAL;
        return -1;
    }

    if (ctx->fd < 0) {
        return 0;
    }

    rc = close(ctx->fd);
    if (rc < 0) {
        return -1;
    }

    ctx->fd = -1;
    ctx->next_seqno = 0;
    ctx->path[0] = '\0';

    return 0;
}
int hifs_wbl_append(struct hifs_wbl_ctx *ctx,
                    uint16_t rec_type,
                    const void *payload,
                    uint32_t payload_len)
{
    struct hifs_wbl_hdr hdr;
    struct iovec iov[2];
    struct timespec ts;
    ssize_t n;
    uint64_t seqno;
    uint32_t crc;
    int rc;

    if (!ctx || ctx->fd < 0) {
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

    rc = hifs_wbl_rotate_if_needed(ctx,
                                   sizeof(struct hifs_wbl_hdr) +
                                   (size_t)payload_len);
    if (rc < 0) {
        return -1;
    }

    clock_gettime(CLOCK_REALTIME, &ts);
    seqno = hifs_wbl_next_seqno(ctx);

    hdr.magic = HIFS_WBL_MAGIC_VALUE;
    hdr.version = HIFS_WBL_VERSION;
    hdr.type = rec_type;
    hdr.length = payload_len;
    hdr.seqno = seqno;
    hdr.ts_nsec = (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
    hdr.crc32 = 0;
    crc = payload_len ? hifs_wbl_calc_crc32(payload, payload_len) : 0;
    hdr.crc32 = crc;

    iov[0].iov_base = &hdr;
    iov[0].iov_len = sizeof(hdr);
    iov[1].iov_base = (void *)(payload_len ? payload : NULL);
    iov[1].iov_len = payload_len;

    n = writev(ctx->fd, iov, 2);
    if (n < 0) {
        return -1;
    }

    if ((size_t)n != sizeof(hdr) + payload_len) {
        errno = EIO;
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
                         HIFS_WBL_REC_WRITE_INTENT,
                         rec,
                         sizeof(*rec));
}

int hifs_wbl_mark_landing_eccoded(struct hifs_wbl_ctx *ctx,
                                  const struct hifs_wbl_landing_rec *rec)
{
    return hifs_wbl_emit(ctx,
                         HIFS_WBL_REC_LANDING_ECCODED,
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
                         HIFS_WBL_REC_FRAGMENT_RECEIVED,
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
                         HIFS_WBL_REC_STRIPE_PERSISTED,
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
