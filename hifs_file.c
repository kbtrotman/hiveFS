/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

#include "hifs.h"
#include <linux/uaccess.h>
#include <linux/math64.h>


ssize_t hifs_get_loffset(struct hifs_inode *hii, loff_t off)
{
	ssize_t ret = HIFS_EMPTY_ENTRY;
	loff_t add = 0;
	u32 i = 0;

	if (off > HIFS_DEFAULT_BLOCK_SIZE)
		add += HIFS_DEFAULT_BLOCK_SIZE % off;

	for (i = 0; i < HIFS_INODE_TSIZE; ++i) {
		if (hii->i_addrb[i] + off > hii->i_addre[i]) {
			off -= (hii->i_addre[i] - hii->i_addrb[i]);
		} else {
			ret = hii->i_addrb[i] + off;
			break;
		}
	}

	BUG_ON(ret == 0xdeadbeef);

	return ret;
}

ssize_t hifs_read(struct kiocb *iocb, struct iov_iter *to)
{
	struct super_block *sb;
	struct inode *inode;
	struct hifs_inode *hiinode;
	struct buffer_head *bh;
	char *buffer;
	void *buf = to->__iov->iov_base;
	int nbytes;
	int dedupe_ret;
	size_t count = iov_iter_count(to);
	loff_t off = iocb->ki_pos;
	//loff_t end = off + count;
	size_t blk = 0;
	uint8_t block_hash[HIFS_BLOCK_HASH_SIZE];


	inode = iocb->ki_filp->f_path.dentry->d_inode;
	sb = inode->i_sb;
	hiinode = inode->i_private;
	if (off) {
		return 0;
	}

	/* calculate datablock number here */
	blk = hifs_get_loffset(hiinode, off);
	if (hifs_fetch_block(sb, blk) < 0)
		return 0;
	bh = sb_bread(sb, blk);
	if (!bh) {
        printk(KERN_ERR "Failed to read data block %lu\n", blk);
		return 0;
	}

	dedupe_ret = hifs_rehydrate_reads(sb, blk, bh->b_data,
					 sb->s_blocksize, block_hash, NULL);
	if (dedupe_ret)
		hifs_warning("dedupe rehydrate returned %d for block %lu",
			     dedupe_ret, (unsigned long)blk);

	buffer = (char *)bh->b_data + (off % HIFS_DEFAULT_BLOCK_SIZE);
	nbytes = min((size_t)(hiinode->i_size - off), count);

	if (copy_to_user(buf, buffer, nbytes)) {
		brelse(bh);
		printk(KERN_ERR
		"Error copying file content to userspace buffer\n");
		return -EFAULT;
	}

	brelse(bh);
	iocb->ki_pos += nbytes;

	return nbytes;
}
/*
ssize_t hifs_alloc_if_necessary(struct hifs_sb_info *sb, struct hifs_inode *di, loff_t off, size_t cnt)
{
	// Mock it until using bitmap
	return 0;
}
*/
ssize_t hifs_write(struct kiocb *iocb, struct iov_iter *from)
{
	struct super_block *sb;
	struct inode *inode;
	struct hifs_inode *dinode;
	struct buffer_head *bh;
	void *buf = from->__iov->iov_base; 
	loff_t off = iocb->ki_pos;
	size_t count = iov_iter_count(from);
	size_t boff = 0;
	char *buffer;
	int dedupe_ret;
	uint8_t block_hash[HIFS_BLOCK_HASH_SIZE];
	enum hifs_hash_algorithm hash_algo = HIFS_HASH_ALGO_NONE;
	u64 block_index;
	size_t hash_slot;
	//int ret;

	inode = iocb->ki_filp->f_path.dentry->d_inode;
	sb = inode->i_sb;
	dinode = inode->i_private;
	
	//ret = generic_write_checks(iocb, from);
	//if (ret <= 0) {
	//    printk(KERN_INFO "HiveFS: generic_write_checks Failed: %d", ret); 
	//    return ret;
	//}
	
	/* calculate datablock to write alloc if necessary */
	//blk = hifs_alloc_if_necessary(dsb, dinode, off, count);
	/* files are contigous so offset can be easly calculated */
	boff = hifs_get_loffset(dinode, off);
	if (hifs_fetch_block(sb, boff) < 0) {
		bh = sb_bread(sb, boff);
		if (!bh)
		    return -EIO;
		memset(bh->b_data, 0, HIFS_DEFAULT_BLOCK_SIZE);
	} else {
		bh = sb_bread(sb, boff);
	}
	if (!bh) {
	    printk(KERN_ERR "Failed to read data block %zu\n", boff);
	    return 0;
	}
	
	buffer = (char *)bh->b_data + (off % HIFS_DEFAULT_BLOCK_SIZE);
	if (copy_from_user(buffer, buf, count)) {
	    brelse(bh);
	    printk(KERN_ERR "Error copying file content from userspace buffer to kernel space\n");
	    return -EFAULT;
	}
	
	iocb->ki_pos += count; 

	hash_algo = HIFS_HASH_ALGO_NONE;
	dedupe_ret = hifs_dedupe_writes(sb, boff, bh->b_data,
					HIFS_DEFAULT_BLOCK_SIZE, block_hash,
					&hash_algo);
	if (dedupe_ret)
		hifs_warning("dedupe placeholder returned %d for block %zu", dedupe_ret, boff);

	hifs_cache_mark_present(sb, boff);
	hifs_cache_mark_dirty(sb, boff);
	hifs_cache_mark_inode(sb, dinode->i_ino);

	block_index = div64_u64((u64)off, HIFS_DEFAULT_BLOCK_SIZE);
	if (block_index >= HIFS_MAX_BLOCK_HASHES) {
		hifs_warning("block index %llu exceeds hash capacity for inode %llu",
			     (unsigned long long)block_index,
			     (unsigned long long)dinode->i_ino);
		hash_slot = HIFS_MAX_BLOCK_HASHES - 1;
	} else {
		hash_slot = (size_t)block_index;
	}

	{
		struct hifs_block_fingerprint *fp = &dinode->i_block_fingerprints[hash_slot];
		memset(fp, 0, sizeof(*fp));
		if (boff > U32_MAX)
			hifs_warning("block %zu overflows fingerprint storage", boff);
		fp->block_no = (uint32_t)boff;
		memcpy(fp->hash, block_hash, HIFS_BLOCK_HASH_SIZE);
		fp->hash_algo = (uint8_t)hash_algo;
	}
	dinode->i_hash_count = max_t(uint16_t, dinode->i_hash_count,
				 (uint16_t)(hash_slot + 1));

	hifs_cache_mark_dirent(sb, dinode->i_ino);

	mark_buffer_dirty(bh);
	sync_dirty_buffer(bh);
	hifs_push_block(sb, boff, bh->b_data, HIFS_DEFAULT_BLOCK_SIZE);
	brelse(bh);

	{
		loff_t new_size = max_t(loff_t, (loff_t)dinode->i_size, off + count);
		dinode->i_size = (uint32_t)new_size;
	}

	hifs_store_inode(sb, dinode);
	hifs_publish_inode(sb, dinode, false);

	return count;
}

int hifs_open_file(struct inode *inode, struct file *filp)
{

	return 0;
}

int hifs_release_file(struct inode *inode, struct file *filp)
{

	return 0;
}
