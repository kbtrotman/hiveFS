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
#include <unistd.h>

#include "hive_guard_mcl.h"

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
        return sizeof(struct hifs_inode);
    case CHANGE_DIRENTRY:
        return sizeof(struct hifs_dir_entry);
    case CHANGE_STRIPE_INFO:
        return sizeof(struct HifsEstripeLocations);
    case CHANGE_ROOT_DIRENTRY:
        return sizeof(struct hifs_volume_root_dentry);
    case CHANGE_VOLUME_SUPERBLOCK:
        return sizeof(struct hifs_volume_superblock);
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

    rc = close(ctx->fd);
    if (rc < 0) {
        return -1;
    }

    ctx->fd = -1;
    ctx->next_seqno = 0;
    ctx->path[0] = '\0';

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
