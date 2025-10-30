/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

#include "hifs.h"
#include <linux/vmalloc.h>
#include <linux/align.h>
#include <linux/byteorder/generic.h>
#include <linux/slab.h>
#include <linux/math64.h>
#include <linux/ktime.h>

/*
   <<<HiveFS Cache Management>>>
   This file contains functions to manage the in-memory cache bitmaps for
   mirroring whats on local disk for fast lookup of present/dirty blocks,
   inodes, and directory entries.

   Note that hifs_inode_cache is defined here as well, but it is an in-memory
   slab cache for hifs_inode structures, not related to the on-disk cache bitmaps
   even though it's defined here and it's name is similar.

   Since the local disk space for hivefs is a cache, before all reads/writes are 
   sent to hi-command, those operations, except superblock, are done through these
   cache bitmaps in this file, then compared to hi_command and mirrored there.

*/


/* Slab cache for in-kernel hifs_inode allocations (runtime objects, not on-disk). */
struct kmem_cache *hifs_inode_cache = NULL;

/* On-disk bitmap IO helpers (kernel side). */
static int hifs_read_region(struct super_block *sb, u64 start, void *buf, size_t len)
{
    size_t done = 0;
    while (done < len) {
        unsigned int blksz = sb->s_blocksize;
        sector_t block = (start + done) / blksz;
        unsigned int off = (start + done) % blksz;
        struct buffer_head *bh = sb_bread(sb, block);
        size_t copy;
        if (!bh)
            return -EIO;
        copy = min_t(size_t, len - done, (size_t)blksz - off);
        memcpy((u8 *)buf + done, bh->b_data + off, copy);
        brelse(bh);
        done += copy;
    }
    return 0;
}

/* Write an arbitrary byte region to the block device and flush buffers. */
static int hifs_write_region(struct super_block *sb, u64 start, const void *buf, size_t len)
{
    size_t done = 0;
    while (done < len) {
        unsigned int blksz = sb->s_blocksize;
        sector_t block = (start + done) / blksz;
        unsigned int off = (start + done) % blksz;
        struct buffer_head *bh = sb_bread(sb, block);
        size_t copy;
        if (!bh)
            return -EIO;
        copy = min_t(size_t, len - done, (size_t)blksz - off);
        memcpy(bh->b_data + off, (const u8 *)buf + done, copy);
        mark_buffer_dirty(bh);
        sync_dirty_buffer(bh);
        brelse(bh);
        done += copy;
    }
    return 0;
}

/* Allocate a hifs_inode from the slab cache. */
struct hifs_inode *cache_get_inode(void)
{
	struct hifs_inode *hii;

	if (WARN_ON(!hifs_inode_cache))
		return NULL;

	hii = kmem_cache_alloc(hifs_inode_cache, GFP_KERNEL);
	printk(KERN_INFO "#: hifs cache_get_inode : di=%p\n", hii);

	return hii;
}

/* Free a hifs_inode back to the slab cache and null the caller's pointer. */
void cache_put_inode(struct hifs_inode **hii)
{
	printk(KERN_INFO "#: hifs cache_put_inode : di=%p\n", *hii);
	kmem_cache_free(hifs_inode_cache, *hii);
	*hii = NULL;
}

/* Helper: fetch per-mount state (hifs_sb_info) from VFS super_block. */
static inline struct hifs_sb_info *sbinfo(struct super_block *sb)
{
    return sb ? (struct hifs_sb_info *)sb->s_fs_info : NULL;
}

/* Shared cache context (singleton for now) */

enum hifs_cache_resource {
    HIFS_CACHE_RESOURCE_BLOCK,
    HIFS_CACHE_RESOURCE_DIRENT,
    HIFS_CACHE_RESOURCE_INODE,
};

struct hifs_cache_entry {
    u64 id;
    u64 last_used;
    u32 use_count;
};

struct hifs_cache_fifo {
    struct hifs_cache_entry *entries;
    u32 capacity;
    u32 count;
};

struct hifs_cache_ctx {
    struct hifs_cache_bitmap *inode_bmp;
    struct hifs_cache_bitmap *dirent_bmp;
    struct hifs_cache_bitmap *block_bmp;
    struct hifs_cache_bitmap *dirty_bmp;
    spinlock_t inode_lock;
    spinlock_t dirent_lock;
    spinlock_t block_lock;
    spinlock_t dirty_lock;
    atomic_t refcnt;
    struct hifs_cache_fifo block_fifo;
    struct hifs_cache_fifo dirent_fifo;
    struct hifs_cache_fifo inode_fifo;
    u64 block_bytes_total;
    u64 dirent_bytes_total;
    u64 inode_bytes_total;
    u64 block_bytes_used;
    u64 dirent_bytes_used;
    u64 inode_bytes_used;
    u32 block_unit_bytes;
    u32 dirent_unit_bytes;
    u32 inode_unit_bytes;
};

static struct hifs_cache_ctx *g_cache_ctx;

static inline u64 hifs_cache_now(void)
{
    return ktime_get_coarse_ns();
}

static struct hifs_cache_fifo *hifs_cache_fifo_get(struct hifs_cache_ctx *ctx,
                                                   enum hifs_cache_resource res)
{
    switch (res) {
    case HIFS_CACHE_RESOURCE_BLOCK:
        return &ctx->block_fifo;
    case HIFS_CACHE_RESOURCE_DIRENT:
        return &ctx->dirent_fifo;
    case HIFS_CACHE_RESOURCE_INODE:
        return &ctx->inode_fifo;
    }
    return NULL;
}

static struct hifs_cache_bitmap *hifs_cache_bitmap_get(struct hifs_cache_ctx *ctx,
                                                       enum hifs_cache_resource res)
{
    switch (res) {
    case HIFS_CACHE_RESOURCE_BLOCK:
        return ctx->block_bmp;
    case HIFS_CACHE_RESOURCE_DIRENT:
        return ctx->dirent_bmp;
    case HIFS_CACHE_RESOURCE_INODE:
        return ctx->inode_bmp;
    }
    return NULL;
}

static u64 *hifs_cache_bytes_used_ptr(struct hifs_cache_ctx *ctx,
                                      enum hifs_cache_resource res)
{
    switch (res) {
    case HIFS_CACHE_RESOURCE_BLOCK:
        return &ctx->block_bytes_used;
    case HIFS_CACHE_RESOURCE_DIRENT:
        return &ctx->dirent_bytes_used;
    case HIFS_CACHE_RESOURCE_INODE:
        return &ctx->inode_bytes_used;
    }
    return NULL;
}

static u64 hifs_cache_bytes_total(struct hifs_cache_ctx *ctx,
                                  enum hifs_cache_resource res)
{
    switch (res) {
    case HIFS_CACHE_RESOURCE_BLOCK:
        return ctx->block_bytes_total;
    case HIFS_CACHE_RESOURCE_DIRENT:
        return ctx->dirent_bytes_total;
    case HIFS_CACHE_RESOURCE_INODE:
        return ctx->inode_bytes_total;
    }
    return 0;
}

static u32 hifs_cache_unit_bytes(struct hifs_cache_ctx *ctx,
                                 enum hifs_cache_resource res)
{
    switch (res) {
    case HIFS_CACHE_RESOURCE_BLOCK:
        return ctx->block_unit_bytes;
    case HIFS_CACHE_RESOURCE_DIRENT:
        return ctx->dirent_unit_bytes;
    case HIFS_CACHE_RESOURCE_INODE:
        return ctx->inode_unit_bytes;
    }
    return 0;
}

static int hifs_cache_fifo_find(const struct hifs_cache_fifo *fifo, u64 id)
{
    u32 i;

    for (i = 0; i < fifo->count; ++i) {
        if (fifo->entries[i].id == id)
            return (int)i;
    }
    return -1;
}

static void hifs_cache_fifo_promote(struct hifs_cache_fifo *fifo, u32 idx)
{
    struct hifs_cache_entry entry;

    if (idx >= fifo->count)
        return;

    entry = fifo->entries[idx];
    entry.last_used = hifs_cache_now();
    entry.use_count++;

    if (idx == 0) {
        fifo->entries[0] = entry;
        return;
    }

    memmove(&fifo->entries[1], &fifo->entries[0],
            idx * sizeof(struct hifs_cache_entry));
    fifo->entries[0] = entry;
}

static void hifs_cache_fifo_insert_front(struct hifs_cache_fifo *fifo, u64 id)
{
    struct hifs_cache_entry entry = {
        .id = id,
        .last_used = hifs_cache_now(),
        .use_count = 1,
    };

    if (fifo->capacity == 0)
        return;

    if (fifo->count > 0 && fifo->count <= fifo->capacity)
        memmove(&fifo->entries[1], &fifo->entries[0],
                min(fifo->count, fifo->capacity - 1) *
                sizeof(struct hifs_cache_entry));
    if (fifo->count < fifo->capacity)
        fifo->count++;
    fifo->entries[0] = entry;
}

static bool hifs_cache_fifo_evict_tail(struct hifs_cache_fifo *fifo, u64 *evicted_id)
{
    if (!fifo->count)
        return false;
    fifo->count--;
    if (evicted_id)
        *evicted_id = fifo->entries[fifo->count].id;
    return true;
}

static bool hifs_cache_fifo_remove_id(struct hifs_cache_fifo *fifo, u64 id)
{
    int idx = hifs_cache_fifo_find(fifo, id);

    if (idx < 0)
        return false;

    if ((u32)idx < fifo->count - 1)
        memmove(&fifo->entries[idx], &fifo->entries[idx + 1],
                (fifo->count - idx - 1) * sizeof(struct hifs_cache_entry));
    fifo->count--;
    return true;
}

static void hifs_cache_account_add(struct hifs_cache_ctx *ctx,
                                   enum hifs_cache_resource res)
{
    u64 *used = hifs_cache_bytes_used_ptr(ctx, res);
    u64 total = hifs_cache_bytes_total(ctx, res);
    u32 unit = hifs_cache_unit_bytes(ctx, res);

    if (!used || !unit)
        return;
    if (*used + unit > total)
        *used = total;
    else
        *used += unit;
}

static void hifs_cache_account_sub(struct hifs_cache_ctx *ctx,
                                   enum hifs_cache_resource res)
{
    u64 *used = hifs_cache_bytes_used_ptr(ctx, res);
    u32 unit = hifs_cache_unit_bytes(ctx, res);

    if (!used || !unit)
        return;
    if (*used >= unit)
        *used -= unit;
    else
        *used = 0;
}

struct hifs_cache_replace_result {
    bool replaced;
    u64 id;
    bool bitmap_cleared;
    bool dirty_cleared;
};

static struct hifs_cache_replace_result
hifs_cache_replace_least_used(struct hifs_cache_ctx *ctx,
                              enum hifs_cache_resource type)
{
    struct hifs_cache_replace_result result = { 0 };
    struct hifs_cache_fifo *fifo = hifs_cache_fifo_get(ctx, type);
    struct hifs_cache_bitmap *bmp = hifs_cache_bitmap_get(ctx, type);

    if (!fifo || !fifo->capacity || fifo->count < fifo->capacity)
        return result;

    if (!hifs_cache_fifo_evict_tail(fifo, &result.id))
        return result;

    result.replaced = true;
    hifs_cache_account_sub(ctx, type);

    if (bmp && result.id < bmp->cache_block_count &&
        __bitmap_test_byte(bmp->bitmap, result.id)) {
        __bitmap_set_byte(bmp->bitmap, result.id, false);
        result.bitmap_cleared = true;
    }

    if (type == HIFS_CACHE_RESOURCE_BLOCK && ctx->dirty_bmp &&
        result.id < ctx->dirty_bmp->cache_block_count) {
        unsigned long dflags;
        spin_lock_irqsave(&ctx->dirty_lock, dflags);
        if (__bitmap_test_byte(ctx->dirty_bmp->bitmap, result.id)) {
            __bitmap_set_byte(ctx->dirty_bmp->bitmap, result.id, false);
            result.dirty_cleared = true;
        }
        spin_unlock_irqrestore(&ctx->dirty_lock, dflags);
    }

    return result;
}

static void hifs_cache_free_ctx(struct hifs_cache_ctx *ctx)
{
    if (!ctx)
        return;
    if (ctx->inode_bmp) {
        kvfree(ctx->inode_bmp->bitmap);
        kfree(ctx->inode_bmp);
    }
    if (ctx->dirent_bmp) {
        kvfree(ctx->dirent_bmp->bitmap);
        kfree(ctx->dirent_bmp);
    }
    if (ctx->block_bmp) {
        kvfree(ctx->block_bmp->bitmap);
        kfree(ctx->block_bmp);
    }
    if (ctx->dirty_bmp) {
        kvfree(ctx->dirty_bmp->bitmap);
        kfree(ctx->dirty_bmp);
    }
    kvfree(ctx->block_fifo.entries);
    kvfree(ctx->dirent_fifo.entries);
    kvfree(ctx->inode_fifo.entries);
    kfree(ctx);
}

static void hifs_cache_sync_workfn(struct work_struct *work)
{
    struct hifs_sb_info *info = container_of(to_delayed_work(work),
                                             struct hifs_sb_info,
                                             cache_sync_work);
    struct super_block *sb = READ_ONCE(info->sb);
    int dirty;

    if (!sb)
        return;

    dirty = atomic_xchg(&info->cache_dirty, 0);
    if (!dirty)
        return;

    if (hifs_cache_save_ctx(sb))
        atomic_set(&info->cache_dirty, 1);

    hifs_flush_dirty_cache_items();

    if (atomic_read(&info->cache_dirty))
        queue_delayed_work(system_long_wq, &info->cache_sync_work,
                           info->cache_flush_interval);
}

void hifs_cache_sync_init(struct super_block *sb, struct hifs_sb_info *info)
{
    if (!sb || !info)
        return;

    info->sb = sb;
    atomic_set(&info->cache_dirty, 0);
    info->cache_flush_interval = HIFS_CACHE_FLUSH_INTERVAL_DEFAULT;
    INIT_DELAYED_WORK(&info->cache_sync_work, hifs_cache_sync_workfn);
}

void hifs_cache_sync_shutdown(struct hifs_sb_info *info)
{
    if (!info)
        return;
    cancel_delayed_work_sync(&info->cache_sync_work);
}

void hifs_cache_request_flush(struct super_block *sb, bool immediate)
{
    struct hifs_sb_info *info = sbinfo(sb);
    unsigned long delay;

    if (!info)
        return;

    atomic_set(&info->cache_dirty, 1);
    delay = immediate ? 0 : info->cache_flush_interval;
    mod_delayed_work(system_long_wq, &info->cache_sync_work, delay);
}

/* Copy bitmap under spinlock and flush it to disk without holding the lock. */
static int hifs_cache_flush_bitmap(struct super_block *sb, spinlock_t *lock,
                                   const struct hifs_cache_bitmap *bmp,
                                   u64 offset, u32 blocksize)
{
    unsigned long flags;
    size_t len;
    u8 *tmp;
    int ret;

    if (!bmp || !bmp->bitmap)
        return -EINVAL;

    len = ALIGN(bmp->size, blocksize);
    tmp = kvmalloc(len, GFP_KERNEL);
    if (!tmp)
        return -ENOMEM;

    spin_lock_irqsave(lock, flags);
    memcpy(tmp, bmp->bitmap, bmp->size);
    if (len > bmp->size)
        memset(tmp + bmp->size, 0, len - bmp->size);
    spin_unlock_irqrestore(lock, flags);

    ret = hifs_write_region(sb, offset, tmp, len);
    kvfree(tmp);
    return ret;
}

/* Allocate and load present/dirty cache bitmaps from their on-disk regions. */
int hifs_cache_attach(struct super_block *sb, struct hifs_sb_info *info)
{
    static const u64 chunk_bytes = 4096ULL * 1024ULL;
    struct hifs_cache_ctx *ctx;
    u32 blocks, bsize;
    size_t sz, aligned;
    u32 chunk_blocks;
    u32 dir_blocks;
    u32 dir_entries_per_block;
    u32 inode_capacity;
    u32 first_data_block;
    u32 prealloc_chunks;
    u64 data_blocks;
    int ret = 0;

    if (!info)
        return -EINVAL;

    if (g_cache_ctx) {
        info->cache = g_cache_ctx;
        atomic_inc(&g_cache_ctx->refcnt);
        return 0;
    }

    bsize = info->s_blocksize ? info->s_blocksize : HIFS_DEFAULT_BLOCK_SIZE;
    blocks = le32_to_cpu(info->disk.s_blocks_count);
    sz = (blocks + 7) / 8; /* bytes */
    aligned = ALIGN(sz, bsize);

    ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
    if (!ctx)
        return -ENOMEM;

    spin_lock_init(&ctx->inode_lock);
    spin_lock_init(&ctx->dirent_lock);
    spin_lock_init(&ctx->block_lock);
    spin_lock_init(&ctx->dirty_lock);
    atomic_set(&ctx->refcnt, 1);

    ctx->inode_bmp = kzalloc(sizeof(*ctx->inode_bmp), GFP_KERNEL);
    ctx->dirent_bmp = kzalloc(sizeof(*ctx->dirent_bmp), GFP_KERNEL);
    ctx->block_bmp = kzalloc(sizeof(*ctx->block_bmp), GFP_KERNEL);
    ctx->dirty_bmp = kzalloc(sizeof(*ctx->dirty_bmp), GFP_KERNEL);
    if (!ctx->inode_bmp || !ctx->dirent_bmp || !ctx->block_bmp || !ctx->dirty_bmp) {
        ret = -ENOMEM;
        goto err_free;
    }

    /* Block bitmaps sized by total blocks */
    ctx->block_bmp->size = sz;
    ctx->dirty_bmp->size = sz;
    ctx->block_bmp->cache_block_size = bsize;
    ctx->dirty_bmp->cache_block_size = bsize;
    ctx->block_bmp->cache_block_count = blocks;
    ctx->dirty_bmp->cache_block_count = blocks;
    ctx->block_bmp->bitmap = kvmalloc(sz, GFP_KERNEL);
    ctx->dirty_bmp->bitmap = kvmalloc(sz, GFP_KERNEL);
    if (!ctx->block_bmp->bitmap || !ctx->dirty_bmp->bitmap) {
        ret = -ENOMEM;
        goto err_free;
    }

    /* Inode and dirent bitmaps sized by HIFS_MAX_CACHE_INODES */
    ctx->inode_bmp->cache_block_size = bsize;
    ctx->dirent_bmp->cache_block_size = bsize;
    ctx->inode_bmp->cache_block_count = HIFS_MAX_CACHE_INODES;
    ctx->dirent_bmp->cache_block_count = HIFS_MAX_CACHE_INODES;
    ctx->inode_bmp->size = (HIFS_MAX_CACHE_INODES + 7) / 8;
    ctx->dirent_bmp->size = (HIFS_MAX_CACHE_INODES + 7) / 8;
    ctx->inode_bmp->bitmap = kvmalloc(ctx->inode_bmp->size, GFP_KERNEL);
    ctx->dirent_bmp->bitmap = kvmalloc(ctx->dirent_bmp->size, GFP_KERNEL);
    if (!ctx->inode_bmp->bitmap || !ctx->dirent_bmp->bitmap) {
        ret = -ENOMEM;
        goto err_free;
    }

    /* Load on-disk regions */
    ret = hifs_read_region(sb, HIFS_INODE_BITMAP_OFFSET, ctx->inode_bmp->bitmap,
                           ALIGN(ctx->inode_bmp->size, bsize));
    if (ret)
        goto err_free;
    ret = hifs_read_region(sb, HIFS_DIRENT_BITMAP_OFFSET, ctx->dirent_bmp->bitmap,
                           ALIGN(ctx->dirent_bmp->size, bsize));
    if (ret)
        goto err_free;
    ret = hifs_read_region(sb, HIFS_BLOCK_BITMAP_OFFSET, ctx->block_bmp->bitmap, aligned);
    if (ret)
        goto err_free;
    ret = hifs_read_region(sb, HIFS_DIRTY_TABLE_OFFSET, ctx->dirty_bmp->bitmap, aligned);
    if (ret)
        goto err_free;

    /* Derive cache capacities */
    dir_blocks = info->disk.s_prealloc_dir_blocks;
    if (!dir_blocks)
        dir_blocks = 50;

    first_data_block = le32_to_cpu(info->disk.s_first_data_block);
    data_blocks = (blocks > first_data_block) ? (u64)blocks - first_data_block : 0;
    if (data_blocks > dir_blocks)
        data_blocks -= dir_blocks;
    else
        data_blocks = 0;
    chunk_blocks = max_t(u32, (u32)div_u64(chunk_bytes, (u64)bsize), 1U);
    prealloc_chunks = info->disk.s_preallocblocks_;
    if (!prealloc_chunks && data_blocks) {
        u64 chunks = div_u64(data_blocks * (u64)bsize + chunk_bytes - 1, chunk_bytes);
        if (chunks > 255)
            chunks = 255;
        prealloc_chunks = chunks ? (u32)chunks : 1;
    }
    ctx->block_fifo.capacity = (u32)min_t(u64,
                                          (u64)prealloc_chunks * chunk_blocks,
                                          data_blocks);
    if (!ctx->block_fifo.capacity && data_blocks)
        ctx->block_fifo.capacity = (u32)min_t(u64, data_blocks, (u64)chunk_blocks);
    if (ctx->block_fifo.capacity) {
        ctx->block_fifo.entries = kvmalloc_array(ctx->block_fifo.capacity,
                                                 sizeof(*ctx->block_fifo.entries),
                                                 GFP_KERNEL);
        if (!ctx->block_fifo.entries) {
            ret = -ENOMEM;
            goto err_free;
        }
    }

    dir_entries_per_block = bsize / sizeof(struct hifs_dir_entry);
    if (!dir_entries_per_block)
        dir_entries_per_block = 1;
    ctx->dirent_fifo.capacity =
        (u32)min_t(u64, (u64)dir_blocks * dir_entries_per_block,
                   (u64)HIFS_MAX_CACHE_INODES);
    if (ctx->dirent_fifo.capacity) {
        ctx->dirent_fifo.entries = kvmalloc_array(ctx->dirent_fifo.capacity,
                                                  sizeof(*ctx->dirent_fifo.entries),
                                                  GFP_KERNEL);
        if (!ctx->dirent_fifo.entries) {
            ret = -ENOMEM;
            goto err_free;
        }
    }

    inode_capacity = HIFS_MAX_CACHE_INODES;
    ctx->inode_fifo.capacity = inode_capacity;
    if (ctx->inode_fifo.capacity) {
        ctx->inode_fifo.entries = kvmalloc_array(ctx->inode_fifo.capacity,
                                                 sizeof(*ctx->inode_fifo.entries),
                                                 GFP_KERNEL);
        if (!ctx->inode_fifo.entries) {
            ret = -ENOMEM;
            goto err_free;
        }
    }

    ctx->block_unit_bytes = bsize;
    ctx->dirent_unit_bytes = sizeof(struct hifs_dir_entry);
    ctx->inode_unit_bytes = sizeof(struct hifs_inode);
    ctx->block_bytes_total = (u64)ctx->block_unit_bytes * ctx->block_fifo.capacity;
    ctx->dirent_bytes_total = (u64)ctx->dirent_unit_bytes * ctx->dirent_fifo.capacity;
    ctx->inode_bytes_total = (u64)ctx->inode_unit_bytes * ctx->inode_fifo.capacity;

    g_cache_ctx = ctx;
    info->cache = ctx;
    return 0;

err_free:
    hifs_cache_free_ctx(ctx);
    info->cache = NULL;
    return ret;
}

/* Persist in-memory cache bitmaps back to their on-disk regions. */
int hifs_cache_save_ctx(struct super_block *sb)
{
    struct hifs_sb_info *info = sbinfo(sb);
    struct hifs_cache_ctx *ctx = info ? info->cache : NULL;
    int ret = 0;

    if (!ctx || !ctx->inode_bmp || !ctx->dirent_bmp || !ctx->block_bmp || !ctx->dirty_bmp)
        return -EINVAL;

    /* Save inode + dirent + block + dirty regions */
    ret = hifs_cache_flush_bitmap(sb, &ctx->inode_lock, ctx->inode_bmp,
                                  HIFS_INODE_BITMAP_OFFSET, info->s_blocksize);
    if (ret)
        return ret;

    ret = hifs_cache_flush_bitmap(sb, &ctx->dirent_lock, ctx->dirent_bmp,
                                  HIFS_DIRENT_BITMAP_OFFSET, info->s_blocksize);
    if (ret)
        return ret;

    ret = hifs_cache_flush_bitmap(sb, &ctx->block_lock, ctx->block_bmp,
                                  HIFS_BLOCK_BITMAP_OFFSET, info->s_blocksize);
    if (ret)
        return ret;

    return hifs_cache_flush_bitmap(sb, &ctx->dirty_lock, ctx->dirty_bmp,
                                   HIFS_DIRTY_TABLE_OFFSET, info->s_blocksize);
}

/* Free in-memory cache bitmap buffers and clear pointers. */
void hifs_cache_detach(struct hifs_sb_info *info)
{
    struct hifs_cache_ctx *ctx;
    if (!info || !info->cache)
        return;
    ctx = info->cache;
    info->cache = NULL;
    info->sb = NULL;
    if (atomic_dec_and_test(&ctx->refcnt)) {
        hifs_cache_free_ctx(ctx);
        g_cache_ctx = NULL;
    }
}

/* Mark a cache block as present in the local cache. */
void hifs_cache_mark_present(struct super_block *sb, uint64_t block)
{
    struct hifs_sb_info *info = sbinfo(sb);
    struct hifs_cache_ctx *ctx = info ? info->cache : NULL;
    unsigned long flags;
    bool bitmap_changed = false;
    bool dirty_changed = false;
    struct hifs_cache_replace_result rep = { 0 };

    if (!ctx || !ctx->block_bmp || block >= ctx->block_bmp->cache_block_count)
        return;

    spin_lock_irqsave(&ctx->block_lock, flags);

    if (ctx->block_fifo.capacity) {
        int idx = hifs_cache_fifo_find(&ctx->block_fifo, block);

        if (idx >= 0) {
            hifs_cache_fifo_promote(&ctx->block_fifo, idx);
        } else {
            rep = hifs_cache_replace_least_used(ctx, HIFS_CACHE_RESOURCE_BLOCK);
            hifs_cache_fifo_insert_front(&ctx->block_fifo, block);
            hifs_cache_account_add(ctx, HIFS_CACHE_RESOURCE_BLOCK);
        }
    }

    if (rep.bitmap_cleared)
        bitmap_changed = true;
    if (rep.dirty_cleared)
        dirty_changed = true;

    if (!__bitmap_test_byte(ctx->block_bmp->bitmap, block)) {
        __bitmap_set_byte(ctx->block_bmp->bitmap, block, true);
        bitmap_changed = true;
    }

    spin_unlock_irqrestore(&ctx->block_lock, flags);

    if (bitmap_changed || dirty_changed)
        hifs_cache_request_flush(sb, false);
}

/* Clear the present bit for a cache block. */
void hifs_cache_clear_present(struct super_block *sb, uint64_t block)
{
    struct hifs_sb_info *info = sbinfo(sb);
    struct hifs_cache_ctx *ctx = info ? info->cache : NULL;
    unsigned long flags;
    bool changed = false;
    bool dirty_changed = false;

    if (!ctx || !ctx->block_bmp || block >= ctx->block_bmp->cache_block_count)
        return;

    spin_lock_irqsave(&ctx->block_lock, flags);
    if (ctx->block_fifo.capacity &&
        hifs_cache_fifo_remove_id(&ctx->block_fifo, block))
        hifs_cache_account_sub(ctx, HIFS_CACHE_RESOURCE_BLOCK);
    if (__bitmap_test_byte(ctx->block_bmp->bitmap, block)) {
        __bitmap_set_byte(ctx->block_bmp->bitmap, block, false);
        changed = true;
    }
    spin_unlock_irqrestore(&ctx->block_lock, flags);

    if (ctx->dirty_bmp && block < ctx->dirty_bmp->cache_block_count) {
        unsigned long dflags;
        spin_lock_irqsave(&ctx->dirty_lock, dflags);
        if (__bitmap_test_byte(ctx->dirty_bmp->bitmap, block)) {
            __bitmap_set_byte(ctx->dirty_bmp->bitmap, block, false);
            dirty_changed = true;
        }
        spin_unlock_irqrestore(&ctx->dirty_lock, dflags);
    }

    if (changed || dirty_changed)
        hifs_cache_request_flush(sb, false);
}

/* Query whether a cache block is present locally. */
bool hifs_cache_test_present(struct super_block *sb, uint64_t block)
{
    struct hifs_cache_ctx *ctx = sbinfo(sb) ? sbinfo(sb)->cache : NULL;
    if (!ctx || !ctx->block_bmp || block >= ctx->block_bmp->cache_block_count)
        return false;
    return __bitmap_test_byte(ctx->block_bmp->bitmap, block);
}

/* Mark a cache block as dirty (needs persistence to backing store). */
void hifs_cache_mark_dirty(struct super_block *sb, uint64_t block)
{
    struct hifs_cache_ctx *ctx = sbinfo(sb) ? sbinfo(sb)->cache : NULL;
    unsigned long flags;
    bool changed = false;
    if (!ctx || !ctx->dirty_bmp || block >= ctx->dirty_bmp->cache_block_count)
        return;
    spin_lock_irqsave(&ctx->dirty_lock, flags);
    if (!__bitmap_test_byte(ctx->dirty_bmp->bitmap, block)) {
        __bitmap_set_byte(ctx->dirty_bmp->bitmap, block, true);
        changed = true;
    }
    spin_unlock_irqrestore(&ctx->dirty_lock, flags);
    if (changed)
        hifs_cache_request_flush(sb, false);
}

/* Clear the dirty bit for a cache block. */
void hifs_cache_clear_dirty(struct super_block *sb, uint64_t block)
{
    struct hifs_cache_ctx *ctx = sbinfo(sb) ? sbinfo(sb)->cache : NULL;
    unsigned long flags;
    bool changed = false;
    if (!ctx || !ctx->dirty_bmp || block >= ctx->dirty_bmp->cache_block_count)
        return;
    spin_lock_irqsave(&ctx->dirty_lock, flags);
    if (__bitmap_test_byte(ctx->dirty_bmp->bitmap, block)) {
        __bitmap_set_byte(ctx->dirty_bmp->bitmap, block, false);
        changed = true;
    }
    spin_unlock_irqrestore(&ctx->dirty_lock, flags);
    if (changed)
        hifs_cache_request_flush(sb, false);
}

/* Query whether a cache block is marked dirty. */
bool hifs_cache_test_dirty(struct super_block *sb, uint64_t block)
{
    struct hifs_cache_ctx *ctx = sbinfo(sb) ? sbinfo(sb)->cache : NULL;
    if (!ctx || !ctx->dirty_bmp || block >= ctx->dirty_bmp->cache_block_count)
        return false;
    return __bitmap_test_byte(ctx->dirty_bmp->bitmap, block);
}

void hifs_cache_mark_inode(struct super_block *sb, uint64_t ino)
{
    struct hifs_sb_info *info = sbinfo(sb);
    struct hifs_cache_ctx *ctx = info ? info->cache : NULL;
    unsigned long flags;
    bool changed = false;
    struct hifs_cache_replace_result rep = { 0 };

    if (!ctx || !ctx->inode_bmp || ino >= ctx->inode_bmp->cache_block_count)
        return;

    spin_lock_irqsave(&ctx->inode_lock, flags);
    if (ctx->inode_fifo.capacity) {
        int idx = hifs_cache_fifo_find(&ctx->inode_fifo, ino);

        if (idx >= 0) {
            hifs_cache_fifo_promote(&ctx->inode_fifo, idx);
        } else {
            rep = hifs_cache_replace_least_used(ctx, HIFS_CACHE_RESOURCE_INODE);
            hifs_cache_fifo_insert_front(&ctx->inode_fifo, ino);
            hifs_cache_account_add(ctx, HIFS_CACHE_RESOURCE_INODE);
        }
    }

    if (rep.bitmap_cleared)
        changed = true;

    if (!__bitmap_test_byte(ctx->inode_bmp->bitmap, ino)) {
        __bitmap_set_byte(ctx->inode_bmp->bitmap, ino, true);
        changed = true;
    }
    spin_unlock_irqrestore(&ctx->inode_lock, flags);
    if (changed)
        hifs_cache_request_flush(sb, false);
}

void hifs_cache_clear_inode(struct super_block *sb, uint64_t ino)
{
    struct hifs_sb_info *info = sbinfo(sb);
    struct hifs_cache_ctx *ctx = info ? info->cache : NULL;
    unsigned long flags;
    bool changed = false;
    if (!ctx || !ctx->inode_bmp || ino >= ctx->inode_bmp->cache_block_count)
        return;
    spin_lock_irqsave(&ctx->inode_lock, flags);
    if (ctx->inode_fifo.capacity &&
        hifs_cache_fifo_remove_id(&ctx->inode_fifo, ino))
        hifs_cache_account_sub(ctx, HIFS_CACHE_RESOURCE_INODE);
    if (__bitmap_test_byte(ctx->inode_bmp->bitmap, ino)) {
        __bitmap_set_byte(ctx->inode_bmp->bitmap, ino, false);
        changed = true;
    }
    spin_unlock_irqrestore(&ctx->inode_lock, flags);
    if (changed)
        hifs_cache_request_flush(sb, false);
}

bool hifs_cache_test_inode(struct super_block *sb, uint64_t ino)
{
    struct hifs_cache_ctx *ctx = sbinfo(sb) ? sbinfo(sb)->cache : NULL;
    if (!ctx || !ctx->inode_bmp || ino >= ctx->inode_bmp->cache_block_count)
        return false;
    return __bitmap_test_byte(ctx->inode_bmp->bitmap, ino);
}

void hifs_cache_mark_dirent(struct super_block *sb, uint64_t dent)
{
    struct hifs_sb_info *info = sbinfo(sb);
    struct hifs_cache_ctx *ctx = info ? info->cache : NULL;
    unsigned long flags;
    bool changed = false;
    struct hifs_cache_replace_result rep = { 0 };

    if (!ctx || !ctx->dirent_bmp || dent >= ctx->dirent_bmp->cache_block_count)
        return;

    spin_lock_irqsave(&ctx->dirent_lock, flags);
    if (ctx->dirent_fifo.capacity) {
        int idx = hifs_cache_fifo_find(&ctx->dirent_fifo, dent);

        if (idx >= 0) {
            hifs_cache_fifo_promote(&ctx->dirent_fifo, idx);
        } else {
            rep = hifs_cache_replace_least_used(ctx, HIFS_CACHE_RESOURCE_DIRENT);
            hifs_cache_fifo_insert_front(&ctx->dirent_fifo, dent);
            hifs_cache_account_add(ctx, HIFS_CACHE_RESOURCE_DIRENT);
        }
    }

    if (rep.bitmap_cleared)
        changed = true;

    if (!__bitmap_test_byte(ctx->dirent_bmp->bitmap, dent)) {
        __bitmap_set_byte(ctx->dirent_bmp->bitmap, dent, true);
        changed = true;
    }
    spin_unlock_irqrestore(&ctx->dirent_lock, flags);
    if (changed)
        hifs_cache_request_flush(sb, false);
}

void hifs_cache_clear_dirent(struct super_block *sb, uint64_t dent)
{
	struct hifs_sb_info *info = sbinfo(sb);
	struct hifs_cache_ctx *ctx = info ? info->cache : NULL;
	unsigned long flags;
	bool changed = false;

	if (!ctx || !ctx->dirent_bmp || dent >= ctx->dirent_bmp->cache_block_count)
		return;

	spin_lock_irqsave(&ctx->dirent_lock, flags);
	if (ctx->dirent_fifo.capacity &&
	    hifs_cache_fifo_remove_id(&ctx->dirent_fifo, dent))
		hifs_cache_account_sub(ctx, HIFS_CACHE_RESOURCE_DIRENT);
	if (__bitmap_test_byte(ctx->dirent_bmp->bitmap, dent)) {
		__bitmap_set_byte(ctx->dirent_bmp->bitmap, dent, false);
		changed = true;
	}
	spin_unlock_irqrestore(&ctx->dirent_lock, flags);
	if (changed)
		hifs_cache_request_flush(sb, false);
}

bool hifs_cache_test_dirent(struct super_block *sb, uint64_t dent)
{
	struct hifs_cache_ctx *ctx = sbinfo(sb) ? sbinfo(sb)->cache : NULL;

	if (!ctx || !ctx->dirent_bmp || dent >= ctx->dirent_bmp->cache_block_count)
		return false;

	return __bitmap_test_byte(ctx->dirent_bmp->bitmap, dent);
}

void hifs_sort_most_recent_cache_used(struct super_block *sb)
{
	struct hifs_sb_info *info = sbinfo(sb);
	struct hifs_cache_ctx *ctx = info ? info->cache : NULL;

	if (!ctx)
		return;

	/* TODO: sort cache tracking arrays by usage frequency and most recently read/write.
     * weight heavier on number of uses above 3 times.
      */
}

int hifs_fetch_block(struct super_block *sb, uint64_t block)
{
    int ret;

    if (!sb)
        return -EINVAL;

    if (hifs_cache_test_present(sb, block)) {
        hifs_debug("cache hit block %llu", (unsigned long long)block);
        return 0;
    }

    hifs_debug("cache miss block %llu", (unsigned long long)block);
    ret = hifs_publish_block(sb, block, NULL, 0, true);
    if (ret < 0)
        return ret;

    if (!hifs_cache_test_present(sb, block))
        return -ENOENT;

    return 0;
}

int hifs_push_block(struct super_block *sb, uint64_t block,
                    const void *data, u32 data_len)
{
    if (!sb || !data)
        return -EINVAL;

    hifs_cache_mark_dirty(sb, block);
    hifs_cache_mark_present(sb, block);
    return hifs_publish_block(sb, block, data, data_len, false);
}
/* Volume table helpers */
static int hifs_read_volume_entry(struct super_block *sb, u32 index, struct hifs_volume_entry *ve)
{
    u64 off = HIFS_VOLUME_TABLE_OFFSET + (u64)index * sizeof(*ve);
    return hifs_read_region(sb, off, ve, sizeof(*ve));
}

static int hifs_write_volume_entry(struct super_block *sb, u32 index, const struct hifs_volume_entry *ve)
{
    u64 off = HIFS_VOLUME_TABLE_OFFSET + (u64)index * sizeof(*ve);
    return hifs_write_region(sb, off, ve, sizeof(*ve));
}

static int hifs_find_volume_index(struct super_block *sb, uint64_t volume_id,
                                  struct hifs_volume_entry *ve_out,
                                  u32 *idx_out, u32 *free_idx_out)
{
    u32 i;
    int ret;
    struct hifs_volume_entry ve;
    u32 free_idx = (u32)-1;
    for (i = 0; i < HIFS_VOLUME_TABLE_MAX; i++) {
        ret = hifs_read_volume_entry(sb, i, &ve);
        if (ret)
            return ret;
        if (ve.volume_id == 0 && free_idx == (u32)-1)
            free_idx = i;
        if (le64_to_cpu(ve.volume_id) == volume_id) {
            if (ve_out)
                *ve_out = ve;
            if (idx_out)
                *idx_out = i;
            if (free_idx_out)
                *free_idx_out = free_idx;
            return 0;
        }
    }
    if (ve_out)
        memset(ve_out, 0, sizeof(*ve_out));
    if (idx_out)
        *idx_out = (u32)-1;
    if (free_idx_out)
        *free_idx_out = free_idx;
    return 1; /* not found */
}

int hifs_volume_load(struct super_block *sb, struct hifs_sb_info *info, bool create)
{
    struct hifs_volume_entry ve;
    u32 idx, free_idx;
    int ret;
    if (!sb || !info)
        return -EINVAL;
    ret = hifs_find_volume_index(sb, info->volume_id, &ve, &idx, &free_idx);
    if (ret == 0) {
        info->vol_super = ve.vsb;
        info->root_dentry = ve.root;
        return 0;
    }
    if (ret > 0 && create) {
        /* Create a new entry with zeroed vsb */
        if (free_idx == (u32)-1)
            return -ENOSPC;
        memset(&ve, 0, sizeof(ve));
        ve.volume_id = cpu_to_le64(info->volume_id);
        ve.vsb.s_rev_level = cpu_to_le32(0);
        ve.vsb.s_wtime = cpu_to_le32(0);
        ve.vsb.s_flags = cpu_to_le32(0);
        memset(&ve.root, 0, sizeof(ve.root));
        ret = hifs_write_volume_entry(sb, free_idx, &ve);
        if (ret)
            return ret;
        info->vol_super = ve.vsb;
        info->root_dentry = ve.root;
        return 0;
    }
    return ret < 0 ? ret : -ENOENT;
}

int hifs_volume_save(struct super_block *sb, const struct hifs_sb_info *info)
{
    struct hifs_volume_entry ve;
    u32 idx, free_idx;
    int ret;
    if (!sb || !info)
        return -EINVAL;
    ret = hifs_find_volume_index(sb, info->volume_id, &ve, &idx, &free_idx);
    if (ret)
        return ret < 0 ? ret : -ENOENT;
    ve.volume_id = cpu_to_le64(info->volume_id);
    ve.vsb = info->vol_super;
    ve.root = info->root_dentry;
    return hifs_write_volume_entry(sb, idx, &ve);
}
