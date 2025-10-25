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

/* Allocate and load present/dirty cache bitmaps from their on-disk regions. */
int hifs_cache_load(struct super_block *sb)
{
    struct hifs_sb_info *info = sbinfo(sb);
    u32 blocks, bsize;
    size_t sz, aligned;
    int ret;

    if (!info)
        return -EINVAL;

    bsize = info->s_blocksize;
    blocks = le32_to_cpu(info->disk.s_blocks_count);
    sz = (blocks + 7) / 8; /* bytes */
    aligned = ALIGN(sz, bsize);

    info->inode_bmp = kzalloc(sizeof(*info->inode_bmp), GFP_KERNEL);
    info->dirent_bmp = kzalloc(sizeof(*info->dirent_bmp), GFP_KERNEL);
    info->block_bmp = kzalloc(sizeof(*info->block_bmp), GFP_KERNEL);
    info->dirty_bmp = kzalloc(sizeof(*info->dirty_bmp), GFP_KERNEL);
    if (!info->inode_bmp || !info->dirent_bmp || !info->block_bmp || !info->dirty_bmp)
        return -ENOMEM;

    /* Block bitmaps sized by total blocks */
    info->block_bmp->size = sz;
    info->dirty_bmp->size = sz;
    info->block_bmp->cache_block_size = bsize;
    info->dirty_bmp->cache_block_size = bsize;
    info->block_bmp->cache_block_count = blocks;
    info->dirty_bmp->cache_block_count = blocks;
    info->block_bmp->bitmap = kvmalloc(sz, GFP_KERNEL);
    info->dirty_bmp->bitmap = kvmalloc(sz, GFP_KERNEL);
    if (!info->block_bmp->bitmap || !info->dirty_bmp->bitmap)
        return -ENOMEM;

    /* Inode and dirent bitmaps sized by HIFS_MAX_CACHE_INODES */
    info->inode_bmp->cache_block_size = bsize;
    info->dirent_bmp->cache_block_size = bsize;
    info->inode_bmp->cache_block_count = HIFS_MAX_CACHE_INODES;
    info->dirent_bmp->cache_block_count = HIFS_MAX_CACHE_INODES;
    info->inode_bmp->size = (HIFS_MAX_CACHE_INODES + 7) / 8;
    info->dirent_bmp->size = (HIFS_MAX_CACHE_INODES + 7) / 8;
    info->inode_bmp->bitmap = kvmalloc(info->inode_bmp->size, GFP_KERNEL);
    info->dirent_bmp->bitmap = kvmalloc(info->dirent_bmp->size, GFP_KERNEL);
    if (!info->inode_bmp->bitmap || !info->dirent_bmp->bitmap)
        return -ENOMEM;

    /* Load on-disk regions */
    ret = hifs_read_region(sb, HIFS_INODE_BITMAP_OFFSET, info->inode_bmp->bitmap, ALIGN(info->inode_bmp->size, bsize));
    if (ret)
        return ret;
    ret = hifs_read_region(sb, HIFS_DIRENT_BITMAP_OFFSET, info->dirent_bmp->bitmap, ALIGN(info->dirent_bmp->size, bsize));
    if (ret)
        return ret;
    ret = hifs_read_region(sb, HIFS_BLOCK_BITMAP_OFFSET, info->block_bmp->bitmap, aligned);
    if (ret)
        return ret;
    ret = hifs_read_region(sb, HIFS_DIRTY_TABLE_OFFSET, info->dirty_bmp->bitmap, aligned);
    if (ret)
        return ret;

    spin_lock_init(&info->inode_bmp_lock);
    spin_lock_init(&info->dirent_bmp_lock);
    spin_lock_init(&info->block_bmp_lock);
    spin_lock_init(&info->dirty_bmp_lock);
    return 0;
}

/* Persist in-memory cache bitmaps back to their on-disk regions. */
int hifs_cache_save(struct super_block *sb)
{
    struct hifs_sb_info *info = sbinfo(sb);
    size_t aligned;
    unsigned long flags;
    int ret = 0;

    if (!info || !info->inode_bmp || !info->dirent_bmp || !info->block_bmp || !info->dirty_bmp)
        return -EINVAL;

    aligned = ALIGN(info->block_bmp->size, info->s_blocksize);

    /* Save inode + dirent + block + dirty regions */
    spin_lock_irqsave(&info->inode_bmp_lock, flags);
    ret = hifs_write_region(sb, HIFS_INODE_BITMAP_OFFSET, info->inode_bmp->bitmap, ALIGN(info->inode_bmp->size, info->s_blocksize));
    spin_unlock_irqrestore(&info->inode_bmp_lock, flags);
    if (ret)
        return ret;

    spin_lock_irqsave(&info->dirent_bmp_lock, flags);
    ret = hifs_write_region(sb, HIFS_DIRENT_BITMAP_OFFSET, info->dirent_bmp->bitmap, ALIGN(info->dirent_bmp->size, info->s_blocksize));
    spin_unlock_irqrestore(&info->dirent_bmp_lock, flags);
    if (ret)
        return ret;

    spin_lock_irqsave(&info->block_bmp_lock, flags);
    ret = hifs_write_region(sb, HIFS_BLOCK_BITMAP_OFFSET, info->block_bmp->bitmap, aligned);
    spin_unlock_irqrestore(&info->block_bmp_lock, flags);
    if (ret)
        return ret;

    spin_lock_irqsave(&info->dirty_bmp_lock, flags);
    ret = hifs_write_region(sb, HIFS_DIRTY_TABLE_OFFSET, info->dirty_bmp->bitmap, aligned);
    spin_unlock_irqrestore(&info->dirty_bmp_lock, flags);
    return ret;
}

/* Free in-memory cache bitmap buffers and clear pointers. */
void hifs_cache_free(struct super_block *sb)
{
    struct hifs_sb_info *info = sbinfo(sb);
    if (!info)
        return;
    if (info->inode_bmp) {
        kvfree(info->inode_bmp->bitmap);
        kfree(info->inode_bmp);
        info->inode_bmp = NULL;
    }
    if (info->dirent_bmp) {
        kvfree(info->dirent_bmp->bitmap);
        kfree(info->dirent_bmp);
        info->dirent_bmp = NULL;
    }
    if (info->block_bmp) {
        kvfree(info->block_bmp->bitmap);
        kfree(info->block_bmp);
        info->block_bmp = NULL;
    }
    if (info->dirty_bmp) {
        kvfree(info->dirty_bmp->bitmap);
        kfree(info->dirty_bmp);
        info->dirty_bmp = NULL;
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
    struct hifs_sb_info *info = sbinfo(sb);
    unsigned long flags;
    if (!info || !info->block_bmp || block >= info->block_bmp->cache_block_count)
        return;
    spin_lock_irqsave(&info->block_bmp_lock, flags);
    __bitmap_set_byte(info->block_bmp->bitmap, block, true);
    spin_unlock_irqrestore(&info->block_bmp_lock, flags);
}

/* Clear the present bit for a cache block. */
void hifs_cache_clear_present(struct super_block *sb, uint64_t block)
{
    struct hifs_sb_info *info = sbinfo(sb);
    unsigned long flags;
    if (!info || !info->block_bmp || block >= info->block_bmp->cache_block_count)
        return;
    spin_lock_irqsave(&info->block_bmp_lock, flags);
    __bitmap_set_byte(info->block_bmp->bitmap, block, false);
    spin_unlock_irqrestore(&info->block_bmp_lock, flags);
}

/* Query whether a cache block is present locally. */
bool hifs_cache_test_present(struct super_block *sb, uint64_t block)
{
    struct hifs_sb_info *info = sbinfo(sb);
    if (!info || !info->block_bmp || block >= info->block_bmp->cache_block_count)
        return false;
    return __bitmap_test_byte(info->block_bmp->bitmap, block);
}

/* Mark a cache block as dirty (needs persistence to backing store). */
void hifs_cache_mark_dirty(struct super_block *sb, uint64_t block)
{
    struct hifs_sb_info *info = sbinfo(sb);
    unsigned long flags;
    if (!info || !info->dirty_bmp || block >= info->dirty_bmp->cache_block_count)
        return;
    spin_lock_irqsave(&info->dirty_bmp_lock, flags);
    __bitmap_set_byte(info->dirty_bmp->bitmap, block, true);
    spin_unlock_irqrestore(&info->dirty_bmp_lock, flags);
}

/* Clear the dirty bit for a cache block. */
void hifs_cache_clear_dirty(struct super_block *sb, uint64_t block)
{
    struct hifs_sb_info *info = sbinfo(sb);
    unsigned long flags;
    if (!info || !info->dirty_bmp || block >= info->dirty_bmp->cache_block_count)
        return;
    spin_lock_irqsave(&info->dirty_bmp_lock, flags);
    __bitmap_set_byte(info->dirty_bmp->bitmap, block, false);
    spin_unlock_irqrestore(&info->dirty_bmp_lock, flags);
}

/* Query whether a cache block is marked dirty. */
bool hifs_cache_test_dirty(struct super_block *sb, uint64_t block)
{
    struct hifs_sb_info *info = sbinfo(sb);
    if (!info || !info->dirty_bmp || block >= info->dirty_bmp->cache_block_count)
        return false;
    return __bitmap_test_byte(info->dirty_bmp->bitmap, block);
}

void hifs_cache_mark_inode(struct super_block *sb, uint64_t ino)
{
    struct hifs_sb_info *info = sbinfo(sb);
    unsigned long flags;
    if (!info || !info->inode_bmp || ino >= info->inode_bmp->cache_block_count)
        return;
    spin_lock_irqsave(&info->inode_bmp_lock, flags);
    __bitmap_set_byte(info->inode_bmp->bitmap, ino, true);
    spin_unlock_irqrestore(&info->inode_bmp_lock, flags);
}

void hifs_cache_clear_inode(struct super_block *sb, uint64_t ino)
{
    struct hifs_sb_info *info = sbinfo(sb);
    unsigned long flags;
    if (!info || !info->inode_bmp || ino >= info->inode_bmp->cache_block_count)
        return;
    spin_lock_irqsave(&info->inode_bmp_lock, flags);
    __bitmap_set_byte(info->inode_bmp->bitmap, ino, false);
    spin_unlock_irqrestore(&info->inode_bmp_lock, flags);
}

bool hifs_cache_test_inode(struct super_block *sb, uint64_t ino)
{
    struct hifs_sb_info *info = sbinfo(sb);
    if (!info || !info->inode_bmp || ino >= info->inode_bmp->cache_block_count)
        return false;
    return __bitmap_test_byte(info->inode_bmp->bitmap, ino);
}

void hifs_cache_mark_dirent(struct super_block *sb, uint64_t dent)
{
    struct hifs_sb_info *info = sbinfo(sb);
    unsigned long flags;
    if (!info || !info->dirent_bmp || dent >= info->dirent_bmp->cache_block_count)
        return;
    spin_lock_irqsave(&info->dirent_bmp_lock, flags);
    __bitmap_set_byte(info->dirent_bmp->bitmap, dent, true);
    spin_unlock_irqrestore(&info->dirent_bmp_lock, flags);
}

void hifs_cache_clear_dirent(struct super_block *sb, uint64_t dent)
{
    struct hifs_sb_info *info = sbinfo(sb);
    unsigned long flags;
    if (!info || !info->dirent_bmp || dent >= info->dirent_bmp->cache_block_count)
        return;
    spin_lock_irqsave(&info->dirent_bmp_lock, flags);
    __bitmap_set_byte(info->dirent_bmp->bitmap, dent, false);
    spin_unlock_irqrestore(&info->dirent_bmp_lock, flags);
}

bool hifs_cache_test_dirent(struct super_block *sb, uint64_t dent)
{
    struct hifs_sb_info *info = sbinfo(sb);
    if (!info || !info->dirent_bmp || dent >= info->dirent_bmp->cache_block_count)
        return false;
    return __bitmap_test_byte(info->dirent_bmp->bitmap, dent);
}
