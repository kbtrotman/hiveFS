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
#include <linux/uio.h>
#include <linux/limits.h>
#include <linux/string.h>

struct hifs_write_block_ref {
	struct buffer_head *bh;
	struct hifs_write_block_ref *next;
	uint8_t hash[HIFS_BLOCK_HASH_SIZE];
	enum hifs_hash_algorithm hash_algo;
};

struct hifs_write_desc {
	uint64_t block_no;
	uint32_t block_count;
	size_t bytes;
	struct hifs_write_block_ref *blocks;
	struct hifs_write_block_ref *tail;
};





static void hifs_write_desc_clear(struct hifs_write_desc *desc)
{
	struct hifs_write_block_ref *ref, *next;

	if (!desc)
		return;

	ref = desc->blocks;
	while (ref) {
		next = ref->next;
		if (ref->bh)
			brelse(ref->bh);
		kfree(ref);
		ref = next;
	}

	desc->blocks = NULL;
	desc->tail = NULL;
	desc->block_no = 0;
	desc->block_count = 0;
	desc->bytes = 0;
}

static int hifs_write_desc_record(struct hifs_write_desc *descs,
				  size_t *desc_count,
				  uint64_t block_no,
				  size_t bytes,
				  struct buffer_head *bh,
				  const uint8_t hash[HIFS_BLOCK_HASH_SIZE],
				  enum hifs_hash_algorithm hash_algo)
{
	struct hifs_write_block_ref *ref;

	if (!descs || !desc_count || !bh || !hash)
		return -EINVAL;

	ref = kmalloc(sizeof(*ref), GFP_KERNEL);
	if (!ref)
		return -ENOMEM;

	get_bh(bh);
	ref->bh = bh;
	ref->next = NULL;
	memcpy(ref->hash, hash, sizeof(ref->hash));
	ref->hash_algo = hash_algo;

	if (*desc_count > 0) {
		struct hifs_write_desc *last = &descs[*desc_count - 1];
		if (last->block_no + last->block_count == block_no) {
			last->block_count++;
			last->bytes += bytes;
			last->tail->next = ref;
			last->tail = ref;
			return 0;
		}
	}

	if (*desc_count >= HIFS_MAX_WRITE_DESCS) {
		struct hifs_write_desc *last = &descs[*desc_count - 1];
		last->block_count++;
		last->bytes += bytes;
		last->tail->next = ref;
		last->tail = ref;
		return 0;
	}

	descs[*desc_count].block_no = block_no;
	descs[*desc_count].block_count = 1;
	descs[*desc_count].bytes = bytes;
	descs[*desc_count].blocks = ref;
	descs[*desc_count].tail = ref;
	(*desc_count)++;
	return 0;
}

static void hifs_flush_descs(struct inode *inode,
			     struct hifs_write_desc *descs,
			     size_t *desc_count,
			     size_t *total_descs_flushed,
			     struct hifs_write_desc *first_desc_snapshot,
			     bool *have_desc_snapshot)
{
	size_t i;
	int flush_rc;

	if (!inode || !descs || !desc_count || !*desc_count)
		return;

	if (!*have_desc_snapshot) {
		*first_desc_snapshot = descs[0];
		*have_desc_snapshot = true;
	}

	flush_rc = hifs_write_contiguous(inode, descs, *desc_count);
	if (flush_rc)
		hifs_warning("contiguous write flush returned %d", flush_rc);

	if (total_descs_flushed)
		*total_descs_flushed += *desc_count;

	for (i = 0; i < *desc_count; ++i)
		hifs_write_desc_clear(&descs[i]);

	memset(descs, 0, (*desc_count) * sizeof(*descs));
	*desc_count = 0;
}

static bool hifs_inode_should_sync_now(struct hifs_inode *dinode, size_t delta)
{
#ifdef __KERNEL__
	unsigned long now = jiffies;

	if (!dinode)
		return true;

	dinode->i_runtime_dirty_bytes += delta;
	if (!dinode->i_runtime_last_sync)
		dinode->i_runtime_last_sync = now;

	if (dinode->i_runtime_dirty_bytes >= HIFS_INODE_SYNC_BYTES)
		return true;

	if (time_after(now, dinode->i_runtime_last_sync + HIFS_INODE_SYNC_INTERVAL))
		return true;

	return false;
#else
	return true;
#endif
}

static void hifs_inode_mark_synced(struct hifs_inode *dinode)
{
#ifdef __KERNEL__
	if (!dinode)
		return;

	dinode->i_runtime_dirty_bytes = 0;
	dinode->i_runtime_last_sync = jiffies;
#endif
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

static loff_t hifs_seek_extent_offset(struct inode *inode,
				      const struct hifs_inode *hii,
				      loff_t offset,
				      int whence)
{
	uint32_t block_size;
	loff_t logical = 0;
	loff_t size;
	size_t i;

	if (!inode || !hii)
		return -EINVAL;

	block_size = hifs_sb_block_size(inode->i_sb);
	if (!block_size)
		return -EINVAL;

	size = (loff_t)hii->i_size;
	if (offset < 0)
		return -EINVAL;
	if (offset > size)
		return -ENXIO;
	if (offset == size)
		return (whence == SEEK_HOLE) ? size : -ENXIO;
	if (!size)
		return (whence == SEEK_HOLE) ? 0 : -ENXIO;

	for (i = 0; i < HIFS_INODE_TSIZE && logical < size; ++i) {
		const struct hifs_extent *ext = &hii->extents[i];
		loff_t extent_bytes;
		loff_t logical_end;

		if (!ext->block_count)
			continue;

		extent_bytes = (loff_t)ext->block_count * block_size;
		if (!extent_bytes)
			continue;

		logical_end = logical + extent_bytes;
		if (logical_end > size)
			logical_end = size;

		if (whence == SEEK_DATA) {
			if (offset <= logical)
				return logical;
			if (offset < logical_end)
				return offset;
		} else {
			if (offset < logical)
				return offset;
			if (offset < logical_end)
				return logical_end;
		}

		logical = logical_end;
	}

	return (whence == SEEK_DATA) ? -ENXIO : size;
}

loff_t hifs_llseek(struct file *file, loff_t offset, int whence)
{
	struct inode *inode = file ? file_inode(file) : NULL;
	struct hifs_inode *hii = inode ? inode->i_private : NULL;

	if (!inode || !hii)
		return -EINVAL;

	if (whence == SEEK_DATA || whence == SEEK_HOLE) {
		loff_t new_pos = hifs_seek_extent_offset(inode, hii, offset, whence);

		if (new_pos < 0)
			return new_pos;
		file->f_pos = new_pos;
		return new_pos;
	}

	return generic_file_llseek(file, offset, whence);
}

ssize_t hifs_read(struct kiocb *iocb, struct iov_iter *to)
{
	struct super_block *sb;
	struct inode *inode;
	struct hifs_inode *hiinode;
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

		if (copy_to_iter(bh->b_data + block_off, chunk, to) != chunk) {
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

static inline bool hifs_need_sync(struct kiocb *iocb, struct inode *inode)
{
    /* refine this later; this is a sane starting point */
    if (iocb->ki_filp->f_flags & O_SYNC)
        return true;
    if (IS_SYNC(inode))
        return true;
    return false;
}

static void hifs_flush_bhs(struct buffer_head **bhs, size_t *nr, bool sync)
{
    size_t i;
    for (i = 0; i < *nr; i++) {
        if (!bhs[i])
            continue;

        /*
         * Option A (recommended): async writeback kick (non-blocking).
         * This submits I/O but does NOT wait.
         */
        write_dirty_buffer(bhs[i], 0);

        if (sync)
            sync_dirty_buffer(bhs[i]);

        brelse(bhs[i]);
        bhs[i] = NULL;
    }
    *nr = 0;
}

ssize_t hifs_write(struct kiocb *iocb, struct iov_iter *from)
{
    ...
    struct buffer_head *batch_bhs[HIFS_WRITEBATCH_MAX];
    size_t batch_nr = 0;
    bool do_sync = hifs_need_sync(iocb, inode);
    ...

    while (count > 0) {
        size_t chunk;
        uint32_t block_off = off % block_size;
        bool need_existing;
        bool zero_fill = false;

        chunk = min_t(size_t, block_size - block_off, count);
        need_existing = (block_off != 0) || (chunk != block_size);

        if (need_existing) {
            if (hifs_fetch_block(sb, boff) < 0)
                zero_fill = true;
            bh = sb_bread(sb, boff);
        } else {
            /*
             * Full-block overwrite: avoid read I/O.
             */
            bh = sb_getblk(sb, boff);
            if (bh) {
                lock_buffer(bh);
                if (!buffer_uptodate(bh)) {
                    /* we will fully overwrite; mark uptodate after copy */
                }
                unlock_buffer(bh);
            }
        }

        if (!bh) {
            printk(KERN_ERR "Failed to get data block %zu\n", boff);
            break;
        }

        if (zero_fill)
            memset(bh->b_data, 0, block_size);

        buffer = bh->b_data + block_off;
        if (!copy_from_iter_full(buffer, chunk, from)) {
            brelse(bh);
            /* flush pending async buffers before returning */
            hifs_flush_bhs(batch_bhs, &batch_nr, do_sync);
            return -EFAULT;
        }

        /*
         * Full block write: now that the data is written, we can mark uptodate.
         */
        if (!need_existing) {
            set_buffer_uptodate(bh);
        }

        /* Your dedupe hashing stays inline (fine for now) */
        hash_algo = HIFS_HASH_ALGO_NONE;
        dedupe_ret = hifs_dedupe_writes(sb, boff, bh->b_data,
                                       block_size, block_hash, &hash_algo);
        if (dedupe_ret) {
            hifs_warning("dedupe placeholder returned %d for block %zu", dedupe_ret, boff);
            memset(block_hash, 0, sizeof(block_hash));
            hash_algo = HIFS_HASH_ALGO_NONE;
        }

        hifs_cache_mark_present(sb, boff);
        hifs_cache_mark_dirty(sb, boff);
        hifs_cache_mark_inode(sb, dinode->i_ino);

        /* fingerprint bookkeeping unchanged */
		block_index = div64_u64((u64)off, block_size);

		if (block_index < HIFS_MAX_BLOCK_HASHES) {
			/* Before the ring is "full", keep direct indexing */
			hash_slot = (size_t)block_index;

			/* count grows up to max */
			if (dinode->i_hash_count < (uint16_t)(hash_slot + 1))
				dinode->i_hash_count = (uint16_t)(hash_slot + 1);

			/* once we hit max-1, initialize head to "next" */
			if (dinode->i_hash_count == HIFS_MAX_BLOCK_HASHES)
				dinode->i_hash_head = 0;
		} else {
			/*
			* Past the limit: overwrite in a ring.
			* i_hash_head points at the slot to overwrite next.
			*/
			hash_slot = dinode->i_hash_head;

			dinode->i_hash_head++;
			if (dinode->i_hash_head >= HIFS_MAX_BLOCK_HASHES)
				dinode->i_hash_head = 0;

			/* Track evictions (optional) */
			if (dinode->i_hash_reserved != UINT16_MAX)
				dinode->i_hash_reserved++;

			/* once we're in ring mode, count is always full */
			dinode->i_hash_count = HIFS_MAX_BLOCK_HASHES;
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

        /*
         * KEY CHANGE: do NOT sync here.
         * Just mark dirty and batch the bh for later write submission.
         */
        mark_buffer_dirty(bh);

        /* Keep your desc gathering */
        ret = hifs_write_desc_record(descs, &desc_count, boff, chunk, bh,
                                     block_hash, hash_algo);
        if (ret) {
            hifs_flush_descs(inode, descs, &desc_count,
                             &total_descs_flushed,
                             &first_desc_snapshot,
                             &have_desc_snapshot);
            ret = hifs_write_desc_record(descs, &desc_count, boff, chunk, bh,
                                         block_hash, hash_algo);
            if (ret) {
                hifs_flush_bhs(batch_bhs, &batch_nr, do_sync);
                brelse(bh);
                return ret;
            }
        }

        /* Batch bh’s; flush on run break or batch full */
        if (batch_nr < HIFS_WRITEBATCH_MAX) {
            batch_bhs[batch_nr++] = bh;
        } else {
            /* batch full: kick async writeout */
            hifs_flush_bhs(batch_bhs, &batch_nr, do_sync);
            batch_bhs[batch_nr++] = bh;
        }

        done += chunk;
        off += chunk;
        count -= chunk;

        /* your existing contiguous-run logic already computes flush_now */
        {
            bool flush_now = false;
            s64 next_boff = HIFS_EMPTY_ENTRY;
            u64 current_block = boff;

            if (count > 0) {
                u64 logical_block = div64_u64(off, block_size);
                next_boff = hifs_get_loffset(dinode, logical_block);
                if (next_boff == HIFS_EMPTY_ENTRY)
                    flush_now = true;
                else if (desc_count > 2 && next_boff != current_block + 1)
                    flush_now = true;
                boff = next_boff;
            } else {
                flush_now = true;
            }

            if (flush_now) {
                /* kick out async writes for this run */
                hifs_flush_bhs(batch_bhs, &batch_nr, do_sync);

                if (desc_count)
                    hifs_flush_descs(inode, descs, &desc_count,
                                     &total_descs_flushed,
                                     &first_desc_snapshot,
                                     &have_desc_snapshot);
            }

            if (count > 0 && next_boff == HIFS_EMPTY_ENTRY)
                break;
        }

        /* Optional: backpressure like other filesystems */
        balance_dirty_pages_ratelimited(inode->i_mapping);
    }

    /* flush anything left */
    hifs_flush_bhs(batch_bhs, &batch_nr, do_sync);

    if (desc_count)
        hifs_flush_descs(inode, descs, &desc_count,
                         &total_descs_flushed,
                         &first_desc_snapshot,
                         &have_desc_snapshot);

    iocb->ki_pos = off;
    if (done > 0) {
        loff_t new_size = max_t(loff_t, (loff_t)dinode->i_size, off);
        dinode->i_size = (uint32_t)new_size;
        dinode->i_bytes = dinode->i_size;
        i_size_write(inode, new_size);
        inode->i_blocks = dinode->i_blocks;
    }

    if (done > 0) {
        bool sync_now = hifs_inode_should_sync_now(dinode, done) || do_sync;

        if (sync_now) {
        hifs_store_inode(sb, dinode);
        hifs_publish_inode(sb, dinode, false);
        hifs_inode_mark_synced(dinode);
    }
    }

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
	struct hifs_inode *dinode = inode ? inode->i_private : NULL;
	struct super_block *sb = inode ? inode->i_sb : NULL;

	if (dinode && sb && dinode->i_runtime_dirty_bytes) {
		hifs_store_inode(sb, dinode);
		hifs_publish_inode(sb, dinode, false);
		hifs_inode_mark_synced(dinode);
	}

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
		const struct hifs_write_block_ref *ref;
		uint32_t b = 0;
		uint32_t pending = 0;
		bool started = false;

		for (ref = desc->blocks, b = 0; ref; ref = ref->next, ++b) {
			u64 block_no = desc->block_no + b;
			if (hifs_dedupe_should_push(sb, block_no))
				++pending;
		}
		if (!pending)
			continue;

		for (ref = desc->blocks, b = 0; ref; ref = ref->next, ++b) {
			u64 block_no = desc->block_no + b;
			u32 flags = 0;
			int ret;

			if (!hifs_dedupe_should_push(sb, block_no))
				continue;
			if (!started) {
				flags |= HIFS_BLOCK_MSGF_CONTIG_START;
				started = true;
			}
			if (pending == 1)
				flags |= HIFS_BLOCK_MSGF_CONTIG_END;
			ret = hifs_push_block(sb, block_no,
					      ref->bh ? ref->bh->b_data : NULL,
					      block_size, flags,
					      ref->hash, ref->hash_algo);
			if (ret)
				hifs_warning("hifs_push_block returned %d for block %llu",
					     ret, (unsigned long long)block_no);
			--pending;
		}
	}

	return 0;
}
