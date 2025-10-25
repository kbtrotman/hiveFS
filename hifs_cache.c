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
};

static struct hifs_cache_ctx *g_cache_ctx;

/* Allocate and load present/dirty cache bitmaps from their on-disk regions. */
int hifs_cache_attach(struct super_block *sb, struct hifs_sb_info *info)
{
    struct hifs_cache_ctx *ctx;
    u32 blocks, bsize;
    size_t sz, aligned;
    int ret;

    if (!info)
        return -EINVAL;

    if (g_cache_ctx) {
        info->cache = g_cache_ctx;
        atomic_inc(&g_cache_ctx->refcnt);
        return 0;
    }

    bsize = info->s_blocksize;
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
    if (!ctx->inode_bmp || !ctx->dirent_bmp || !ctx->block_bmp || !ctx->dirty_bmp)
        return -ENOMEM;

    /* Block bitmaps sized by total blocks */
    ctx->block_bmp->size = sz;
    ctx->dirty_bmp->size = sz;
    ctx->block_bmp->cache_block_size = bsize;
    ctx->dirty_bmp->cache_block_size = bsize;
    ctx->block_bmp->cache_block_count = blocks;
    ctx->dirty_bmp->cache_block_count = blocks;
    ctx->block_bmp->bitmap = kvmalloc(sz, GFP_KERNEL);
    ctx->dirty_bmp->bitmap = kvmalloc(sz, GFP_KERNEL);
    if (!ctx->block_bmp->bitmap || !ctx->dirty_bmp->bitmap)
        return -ENOMEM;

    /* Inode and dirent bitmaps sized by HIFS_MAX_CACHE_INODES */
    ctx->inode_bmp->cache_block_size = bsize;
    ctx->dirent_bmp->cache_block_size = bsize;
    ctx->inode_bmp->cache_block_count = HIFS_MAX_CACHE_INODES;
    ctx->dirent_bmp->cache_block_count = HIFS_MAX_CACHE_INODES;
    ctx->inode_bmp->size = (HIFS_MAX_CACHE_INODES + 7) / 8;
    ctx->dirent_bmp->size = (HIFS_MAX_CACHE_INODES + 7) / 8;
    ctx->inode_bmp->bitmap = kvmalloc(ctx->inode_bmp->size, GFP_KERNEL);
    ctx->dirent_bmp->bitmap = kvmalloc(ctx->dirent_bmp->size, GFP_KERNEL);
    if (!ctx->inode_bmp->bitmap || !ctx->dirent_bmp->bitmap)
        return -ENOMEM;

    /* Load on-disk regions */
    ret = hifs_read_region(sb, HIFS_INODE_BITMAP_OFFSET, ctx->inode_bmp->bitmap, ALIGN(ctx->inode_bmp->size, bsize));
    if (ret)
        return ret;
    ret = hifs_read_region(sb, HIFS_DIRENT_BITMAP_OFFSET, ctx->dirent_bmp->bitmap, ALIGN(ctx->dirent_bmp->size, bsize));
    if (ret)
        return ret;
    ret = hifs_read_region(sb, HIFS_BLOCK_BITMAP_OFFSET, ctx->block_bmp->bitmap, aligned);
    if (ret)
        return ret;
    ret = hifs_read_region(sb, HIFS_DIRTY_TABLE_OFFSET, ctx->dirty_bmp->bitmap, aligned);
    if (ret)
        return ret;

    g_cache_ctx = ctx;
    info->cache = ctx;
    return 0;
}

/* Persist in-memory cache bitmaps back to their on-disk regions. */
int hifs_cache_save_ctx(struct super_block *sb)
{
    struct hifs_sb_info *info = sbinfo(sb);
    struct hifs_cache_ctx *ctx = info ? info->cache : NULL;
    size_t aligned;
    unsigned long flags;
    int ret = 0;

    if (!ctx || !ctx->inode_bmp || !ctx->dirent_bmp || !ctx->block_bmp || !ctx->dirty_bmp)
        return -EINVAL;

    aligned = ALIGN(ctx->block_bmp->size, info->s_blocksize);

    /* Save inode + dirent + block + dirty regions */
    spin_lock_irqsave(&ctx->inode_lock, flags);
    ret = hifs_write_region(sb, HIFS_INODE_BITMAP_OFFSET, ctx->inode_bmp->bitmap, ALIGN(ctx->inode_bmp->size, info->s_blocksize));
    spin_unlock_irqrestore(&ctx->inode_lock, flags);
    if (ret)
        return ret;

    spin_lock_irqsave(&ctx->dirent_lock, flags);
    ret = hifs_write_region(sb, HIFS_DIRENT_BITMAP_OFFSET, ctx->dirent_bmp->bitmap, ALIGN(ctx->dirent_bmp->size, info->s_blocksize));
    spin_unlock_irqrestore(&ctx->dirent_lock, flags);
    if (ret)
        return ret;

    spin_lock_irqsave(&ctx->block_lock, flags);
    ret = hifs_write_region(sb, HIFS_BLOCK_BITMAP_OFFSET, ctx->block_bmp->bitmap, aligned);
    spin_unlock_irqrestore(&ctx->block_lock, flags);
    if (ret)
        return ret;

    spin_lock_irqsave(&ctx->dirty_lock, flags);
    ret = hifs_write_region(sb, HIFS_DIRTY_TABLE_OFFSET, ctx->dirty_bmp->bitmap, aligned);
    spin_unlock_irqrestore(&ctx->dirty_lock, flags);
    return ret;
}

/* Free in-memory cache bitmap buffers and clear pointers. */
void hifs_cache_detach(struct hifs_sb_info *info)
{
    struct hifs_cache_ctx *ctx;
    if (!info || !info->cache)
        return;
    ctx = info->cache;
    info->cache = NULL;
    if (atomic_dec_and_test(&ctx->refcnt)) {
        if (ctx->inode_bmp) { kvfree(ctx->inode_bmp->bitmap); kfree(ctx->inode_bmp); }
        if (ctx->dirent_bmp) { kvfree(ctx->dirent_bmp->bitmap); kfree(ctx->dirent_bmp); }
        if (ctx->block_bmp) { kvfree(ctx->block_bmp->bitmap); kfree(ctx->block_bmp); }
        if (ctx->dirty_bmp) { kvfree(ctx->dirty_bmp->bitmap); kfree(ctx->dirty_bmp); }
        kfree(ctx);
        g_cache_ctx = NULL;
    }
}

/* Set or clear a single bit in a byte-addressed bitmap. */
static inline void __bitmap_set_byte(uint8_t *bm, uint64_t bit, bool val)
{
    uint64_t byte = bit / 8;
    uint8_t mask = 1u << (bit % 8);
    if (val)
        bm[byte] |= mask;
    else
        bm[byte] &= ~mask;
}

/* Test a single bit in a byte-addressed bitmap. */
static inline bool __bitmap_test_byte(const uint8_t *bm, uint64_t bit)
{
    uint64_t byte = bit / 8;
    uint8_t mask = 1u << (bit % 8);
    return (bm[byte] & mask) != 0;
}

/* Mark a cache block as present in the local cache. */
void hifs_cache_mark_present(struct super_block *sb, uint64_t block)
{
    struct hifs_cache_ctx *ctx = sbinfo(sb) ? sbinfo(sb)->cache : NULL;
    unsigned long flags;
    if (!ctx || !ctx->block_bmp || block >= ctx->block_bmp->cache_block_count)
        return;
    spin_lock_irqsave(&ctx->block_lock, flags);
    __bitmap_set_byte(ctx->block_bmp->bitmap, block, true);
    spin_unlock_irqrestore(&ctx->block_lock, flags);
}

/* Clear the present bit for a cache block. */
void hifs_cache_clear_present(struct super_block *sb, uint64_t block)
{
    struct hifs_cache_ctx *ctx = sbinfo(sb) ? sbinfo(sb)->cache : NULL;
    unsigned long flags;
    if (!ctx || !ctx->block_bmp || block >= ctx->block_bmp->cache_block_count)
        return;
    spin_lock_irqsave(&ctx->block_lock, flags);
    __bitmap_set_byte(ctx->block_bmp->bitmap, block, false);
    spin_unlock_irqrestore(&ctx->block_lock, flags);
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
    if (!ctx || !ctx->dirty_bmp || block >= ctx->dirty_bmp->cache_block_count)
        return;
    spin_lock_irqsave(&ctx->dirty_lock, flags);
    __bitmap_set_byte(ctx->dirty_bmp->bitmap, block, true);
    spin_unlock_irqrestore(&ctx->dirty_lock, flags);
}

/* Clear the dirty bit for a cache block. */
void hifs_cache_clear_dirty(struct super_block *sb, uint64_t block)
{
    struct hifs_cache_ctx *ctx = sbinfo(sb) ? sbinfo(sb)->cache : NULL;
    unsigned long flags;
    if (!ctx || !ctx->dirty_bmp || block >= ctx->dirty_bmp->cache_block_count)
        return;
    spin_lock_irqsave(&ctx->dirty_lock, flags);
    __bitmap_set_byte(ctx->dirty_bmp->bitmap, block, false);
    spin_unlock_irqrestore(&ctx->dirty_lock, flags);
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
    struct hifs_cache_ctx *ctx = sbinfo(sb) ? sbinfo(sb)->cache : NULL;
    unsigned long flags;
    if (!ctx || !ctx->inode_bmp || ino >= ctx->inode_bmp->cache_block_count)
        return;
    spin_lock_irqsave(&ctx->inode_lock, flags);
    __bitmap_set_byte(ctx->inode_bmp->bitmap, ino, true);
    spin_unlock_irqrestore(&ctx->inode_lock, flags);
}

void hifs_cache_clear_inode(struct super_block *sb, uint64_t ino)
{
    struct hifs_cache_ctx *ctx = sbinfo(sb) ? sbinfo(sb)->cache : NULL;
    unsigned long flags;
    if (!ctx || !ctx->inode_bmp || ino >= ctx->inode_bmp->cache_block_count)
        return;
    spin_lock_irqsave(&ctx->inode_lock, flags);
    __bitmap_set_byte(ctx->inode_bmp->bitmap, ino, false);
    spin_unlock_irqrestore(&ctx->inode_lock, flags);
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
    struct hifs_cache_ctx *ctx = sbinfo(sb) ? sbinfo(sb)->cache : NULL;
    unsigned long flags;
    if (!ctx || !ctx->dirent_bmp || dent >= ctx->dirent_bmp->cache_block_count)
        return;
    spin_lock_irqsave(&ctx->dirent_lock, flags);
    __bitmap_set_byte(ctx->dirent_bmp->bitmap, dent, true);
    spin_unlock_irqrestore(&ctx->dirent_lock, flags);
}

void hifs_cache_clear_dirent(struct super_block *sb, uint64_t dent)
{
    struct hifs_cache_ctx *ctx = sbinfo(sb) ? sbinfo(sb)->cache : NULL;
    unsigned long flags;
    if (!ctx || !ctx->dirent_bmp || dent >= ctx->dirent_bmp->cache_block_count)
        return;
    spin_lock_irqsave(&ctx->dirent_lock, flags);
    __bitmap_set_byte(ctx->dirent_bmp->bitmap, dent, false);
    spin_unlock_irqrestore(&ctx->dirent_lock, flags);
}

bool hifs_cache_test_dirent(struct super_block *sb, uint64_t dent)
{
    struct hifs_cache_ctx *ctx = sbinfo(sb) ? sbinfo(sb)->cache : NULL;
    if (!ctx || !ctx->dirent_bmp || dent >= ctx->dirent_bmp->cache_block_count)
        return false;
    return __bitmap_test_byte(ctx->dirent_bmp->bitmap, dent);
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
        ret = hifs_write_volume_entry(sb, free_idx, &ve);
        if (ret)
            return ret;
        info->vol_super = ve.vsb;
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
    return hifs_write_volume_entry(sb, idx, &ve);
}
