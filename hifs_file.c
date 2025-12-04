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

struct hifs_write_desc {
	uint64_t block_no;
	uint32_t block_count;
	size_t bytes;
};

static int hifs_write_contiguous(struct inode *inode,
				   const struct hifs_write_desc *descs,
				   size_t desc_count);

#define HIFS_MAX_WRITE_DESCS 64

static void hifs_write_desc_record(struct hifs_write_desc *descs,
				   size_t *desc_count,
				   uint64_t block_no,
				   size_t bytes)
{
	if (!descs || !desc_count)
		return;

	if (*desc_count > 0) {
		struct hifs_write_desc *last = &descs[*desc_count - 1];
		if (last->block_no + last->block_count == block_no) {
			last->block_count++;
			last->bytes += bytes;
			return;
		}
	}

	if (*desc_count >= HIFS_MAX_WRITE_DESCS) {
		struct hifs_write_desc *last = &descs[*desc_count - 1];
		last->block_count++;
		last->bytes += bytes;
		return;
	}

	descs[*desc_count].block_no = block_no;
	descs[*desc_count].block_count = 1;
	descs[*desc_count].bytes = bytes;
	(*desc_count)++;
}


ssize_t hifs_get_loffset(struct hifs_inode *hii, loff_t block_index)
{
	uint32_t remaining;
	size_t i;

	if (!hii || block_index < 0)
		return HIFS_EMPTY_ENTRY;

	remaining = (uint32_t)block_index;
	for (i = 0; i < HIFS_INODE_TSIZE; ++i) {
		const struct hifs_extent *ext = &hii->extents[i];
		uint32_t len = ext->block_count;

		if (!len)
			continue;
		if (remaining < len)
			return ext->block_start + remaining;
		remaining -= len;
	}

	return HIFS_EMPTY_ENTRY;
}

ssize_t hifs_read(struct kiocb *iocb, struct iov_iter *to)
{
	struct super_block *sb;
	struct inode *inode;
	struct hifs_inode *hiinode;
	void *user_buf = to->__iov->iov_base;
	size_t count = iov_iter_count(to);
	size_t done = 0;
	loff_t off = iocb->ki_pos;
	uint32_t block_size;

	inode = iocb->ki_filp->f_path.dentry->d_inode;
	sb = inode->i_sb;
	block_size = hifs_sb_block_size(sb);
	hiinode = inode->i_private;

	if (!count)
		return 0;
	if (off >= hiinode->i_size)
		return 0;

	count = min_t(size_t, count, hiinode->i_size - off);

	while (done < count) {
		struct buffer_head *bh;
		uint64_t logical_block = div64_u64(off, block_size);
		uint32_t block_off = off % block_size;
		size_t chunk = min_t(size_t, block_size - block_off, count - done);
		ssize_t phys = hifs_get_loffset(hiinode, logical_block);
		int dedupe_ret;

		if (phys == HIFS_EMPTY_ENTRY)
			break;

		if (hifs_fetch_block(sb, phys) < 0)
			break;
		bh = sb_bread(sb, phys);
		if (!bh)
			break;

		dedupe_ret = hifs_rehydrate_reads(sb, phys, bh->b_data,
						  sb->s_blocksize, NULL, NULL);
		if (dedupe_ret)
			hifs_warning("dedupe rehydrate returned %d for block %llu",
				     dedupe_ret, (unsigned long long)phys);

		if (copy_to_user((char *)user_buf + done,
				 bh->b_data + block_off,
				 chunk)) {
			brelse(bh);
			return -EFAULT;
		}

		brelse(bh);
		done += chunk;
		off += chunk;
	}

	iocb->ki_pos += done;
	return done;
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
	size_t done = 0;
	char *buffer;
	int dedupe_ret;
	uint8_t block_hash[HIFS_BLOCK_HASH_SIZE];
	enum hifs_hash_algorithm hash_algo = HIFS_HASH_ALGO_NONE;
	u64 block_index;
	size_t hash_slot;
	uint32_t block_size;
	int ret;
	struct hifs_write_desc descs[HIFS_MAX_WRITE_DESCS] = {0};
	size_t desc_count = 0;
	size_t total_descs_flushed = 0;
	struct hifs_write_desc first_desc_snapshot = {0};
	bool have_desc_snapshot = false;

	inode = iocb->ki_filp->f_path.dentry->d_inode;
	sb = inode->i_sb;
	block_size = hifs_sb_block_size(sb);
	dinode = inode->i_private;
	if (count == 0)
		return 0;

	//ret = generic_write_checks(iocb, from);
	//if (ret <= 0) {
	//    printk(KERN_INFO "HiveFS: generic_write_checks Failed: %d", ret); 
	//    return ret;
	//}
	
	/* Ensure required blocks are allocated and map the logical block */
	{
		u64 logical_block = div64_u64(off, block_size);
		u64 end = off + count - 1;
		u64 last_block = div64_u64(end, block_size);

		ret = hifs_inode_reserve_blocks(sb, dinode, (uint32_t)last_block);
		if (ret)
			return ret;

		boff = hifs_get_loffset(dinode, logical_block);
		if (boff == HIFS_EMPTY_ENTRY)
			return -ENOSPC;

		while (count > 0) {
			size_t chunk;

			if (hifs_fetch_block(sb, boff) < 0) {
				bh = sb_bread(sb, boff);
			if (!bh)
				return -EIO;
			memset(bh->b_data, 0, block_size);
		} else {
			bh = sb_bread(sb, boff);
		}
		if (!bh) {
			printk(KERN_ERR "Failed to read data block %zu\n", boff);
			break;
		}

		chunk = min_t(size_t, block_size - (off % block_size), count);

		buffer = bh->b_data + (off % block_size);
		if (copy_from_user(buffer, (char *)buf + done, chunk)) {
			brelse(bh);
			return -EFAULT;
		}

		hash_algo = HIFS_HASH_ALGO_NONE;
		dedupe_ret = hifs_dedupe_writes(sb, boff, bh->b_data,
						block_size, block_hash,
						&hash_algo);
		if (dedupe_ret)
			hifs_warning("dedupe placeholder returned %d for block %zu", dedupe_ret, boff);

		hifs_cache_mark_present(sb, boff);
		hifs_cache_mark_dirty(sb, boff);
		hifs_cache_mark_inode(sb, dinode->i_ino);

		block_index = div64_u64((u64)off, block_size);
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
			fp->block_no = (uint32_t)boff;
			memcpy(fp->hash, block_hash, HIFS_BLOCK_HASH_SIZE);
			fp->hash_algo = (uint8_t)hash_algo;
		}
		dinode->i_hash_count = max_t(uint16_t, dinode->i_hash_count,
					     (uint16_t)(hash_slot + 1));

			mark_buffer_dirty(bh);
			sync_dirty_buffer(bh);
			brelse(bh);

			hifs_write_desc_record(descs, &desc_count, boff, chunk);

			done += chunk;
			off += chunk;
			count -= chunk;
			{
				bool flush_now = false;
				s64 next_boff = HIFS_EMPTY_ENTRY;
				u64 current_block = boff;

				if (count > 0) {
					logical_block = div64_u64(off, block_size);
					next_boff = hifs_get_loffset(dinode, logical_block);
					if (next_boff == HIFS_EMPTY_ENTRY)
						flush_now = true;
					else if (desc_count > 2 && next_boff != current_block + 1)
						flush_now = true;
					boff = next_boff;
				} else {
					flush_now = true;
				}

				if (flush_now && desc_count) {
					if (!have_desc_snapshot) {
						first_desc_snapshot = descs[0];
						have_desc_snapshot = true;
					}
					int flush_rc = hifs_write_contiguous(inode, descs, desc_count);
					if (flush_rc)
						hifs_warning("contiguous write flush returned %d", flush_rc);
					total_descs_flushed += desc_count;
					memset(descs, 0, desc_count * sizeof(*descs));
					desc_count = 0;
				}

				if (count > 0 && next_boff == HIFS_EMPTY_ENTRY)
					break;
			}
		}
	}
	if (desc_count) {
		int flush_rc = hifs_write_contiguous(inode, descs, desc_count);
		if (flush_rc)
			hifs_warning("contiguous write flush returned %d", flush_rc);
		total_descs_flushed += desc_count;
		if (!have_desc_snapshot && desc_count) {
			first_desc_snapshot = descs[0];
			have_desc_snapshot = true;
		}
	}
	if (total_descs_flushed && have_desc_snapshot) {
		hifs_debug("write gathered into %zu descriptors (first block %llu count %u)",
			   total_descs_flushed,
			   (unsigned long long)first_desc_snapshot.block_no,
			   first_desc_snapshot.block_count);
	}

	hifs_cache_mark_dirent(sb, dinode->i_ino);
	iocb->ki_pos += done;

	{
		loff_t new_size = max_t(loff_t, (loff_t)dinode->i_size, off);
		dinode->i_size = (uint32_t)new_size;
		dinode->i_bytes = dinode->i_size;
	}

	hifs_store_inode(sb, dinode);
	hifs_publish_inode(sb, dinode, false);

	return done;
}

int hifs_open_file(struct inode *inode, struct file *file)
{
	/* Basic sanity: ensure private data is present */
	if (!inode || !inode->i_private)
		return -EINVAL;

	/* Hook for future cluster/lease logic */
	{
		int ret = hifs_cluster_on_open(inode, file);
		if (ret)
			return ret;
	}

	file->private_data = inode->i_private;
	return 0;
}

int hifs_release_file(struct inode *inode, struct file *file)
{
	/* Placeholder: cluster/lease teardown when implemented */
	hifs_cluster_on_close(inode, file);
	return 0;
}

static int hifs_write_contiguous(struct inode *inode,
			    const struct hifs_write_desc *descs,
			    size_t desc_count)
{
	struct super_block *sb;
	uint32_t block_size;
	size_t i;

	if (!inode || !descs || !desc_count)
		return 0;

	sb = inode->i_sb;
	if (!sb)
		return -EINVAL;
	block_size = hifs_sb_block_size(sb);

	for (i = 0; i < desc_count; ++i) {
		const struct hifs_write_desc *desc = &descs[i];
		uint32_t blocks = desc->block_count ? desc->block_count : 1;
		uint32_t b;
		uint32_t pending = 0;
		bool started = false;

		for (b = 0; b < blocks; ++b) {
			u64 block_no = desc->block_no + b;
			if (hifs_dedupe_should_push(sb, block_no))
				++pending;
		}
		if (!pending)
			continue;

		for (b = 0; b < blocks; ++b) {
			u64 block_no = desc->block_no + b;
			u32 flags = 0;
			struct buffer_head *bh;
			int ret;

			if (!hifs_dedupe_should_push(sb, block_no))
				continue;
			bh = sb_bread(sb, block_no);
			if (!bh) {
				hifs_warning("failed to re-read block %llu for ring push",
					     (unsigned long long)block_no);
				continue;
			}
			if (!started) {
				flags |= HIFS_BLOCK_MSGF_CONTIG_START;
				started = true;
			}
			if (pending == 1)
				flags |= HIFS_BLOCK_MSGF_CONTIG_END;
			ret = hifs_push_block(sb, block_no, bh->b_data,
					      block_size, flags);
			if (ret)
				hifs_warning("hifs_push_block returned %d for block %llu",
					     ret, (unsigned long long)block_no);
			brelse(bh);
			--pending;
		}
	}

	return 0;
}
