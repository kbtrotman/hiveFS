/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */


#include "hifs.h"
#include <linux/dirent.h>
#include <linux/byteorder/generic.h>
#include <linux/uidgid.h>
#include <linux/limits.h>
#include <linux/errno.h>
#include <linux/minmax.h>
u32 _ix = 0, b = 0, e = 0;

static uint32_t hifs_inode_total_blocks(const struct hifs_inode *inode)
{
	uint32_t total = 0;
	size_t i;

	if (!inode)
		return 0;
	for (i = 0; i < HIFS_INODE_TSIZE; ++i)
		total += inode->extents[i].block_count;
	return total;
}

static struct hifs_extent *hifs_inode_last_extent(struct hifs_inode *inode, int *idx_out)
{
	int i;

	for (i = HIFS_INODE_TSIZE - 1; i >= 0; --i) {
		if (inode->extents[i].block_count) {
			if (idx_out)
				*idx_out = i;
			return &inode->extents[i];
		}
	}
	if (idx_out)
		*idx_out = -1;
	return NULL;
}

static int hifs_alloc_block_range(struct super_block *sb, uint32_t blocks,
				  uint32_t *start_out, uint32_t *prev_last_out)
{
	struct hifs_sb_info *hisb = sb ? sb->s_fs_info : NULL;
	const uint32_t blocksize = hifs_sb_block_size(sb);
	const uint32_t ring_base = blocksize ? (HIFS_CACHE_SPACE_START / blocksize) : 0;
	uint64_t total_blocks;
	uint32_t last;

	if (!hisb || blocks == 0)
		return -EINVAL;

	total_blocks = (uint64_t)le32_to_cpu(hisb->disk.s_blocks_count) |
		       ((uint64_t)le32_to_cpu(hisb->disk.s_blocks_count_hi) << 32);

	last = hisb->s_last_blk;
	if (last < ring_base)
		last = ring_base;

	if (total_blocks) {
		uint64_t next = (uint64_t)last + blocks;

		if (next >= total_blocks) {
			hifs_sort_most_recent_cache_used(sb);
			last = ring_base;
			next = (uint64_t)last + blocks;
			if (next >= total_blocks)
				return -ENOSPC;
		}
	}

	if (prev_last_out)
		*prev_last_out = hisb->s_last_blk;

	hisb->s_last_blk = last;
	if (start_out)
		*start_out = hisb->s_last_blk + 1;
	hisb->s_last_blk += blocks;
	hisb->disk.s_first_data_block = cpu_to_le32(hisb->s_last_blk);
	return 0;
}

static void hifs_restore_block_allocator(struct super_block *sb, uint32_t prev_last)
{
	struct hifs_sb_info *hisb = sb ? sb->s_fs_info : NULL;

	if (!hisb)
		return;
	hisb->s_last_blk = prev_last;
	hisb->disk.s_first_data_block = cpu_to_le32(hisb->s_last_blk);
}

static int hifs_inode_append_extent(struct super_block *sb,
				    struct hifs_inode *inode,
				    uint32_t blocks)
{
	uint32_t start = 0, prev_last = 0;
	struct hifs_extent *last;
	int last_idx = -1;
	int slot;
	int ret;

	if (!blocks)
		return 0;

	last = hifs_inode_last_extent(inode, &last_idx);
	slot = last ? last_idx + 1 : 0;
	if (slot >= HIFS_INODE_TSIZE && !(last && last->block_count))
		return -ENOSPC;

	ret = hifs_alloc_block_range(sb, blocks, &start, &prev_last);
	if (ret)
		return ret;

	last = hifs_inode_last_extent(inode, &last_idx);
	if (last && (last->block_start + last->block_count == start)) {
		last->block_count += blocks;
	} else {
		slot = last ? last_idx + 1 : 0;
		if (slot >= HIFS_INODE_TSIZE) {
			hifs_restore_block_allocator(sb, prev_last);
			return -ENOSPC;
		}
		inode->extents[slot].block_start = start;
		inode->extents[slot].block_count = blocks;
	}

	inode->i_blocks = hifs_inode_total_blocks(inode);
	return 0;
}

int hifs_inode_reserve_blocks(struct super_block *sb,
			      struct hifs_inode *inode,
			      uint32_t last_block_needed)
{
	uint32_t have;
	int ret;

	if (!inode)
		return -EINVAL;

	have = hifs_inode_total_blocks(inode);
	while (have <= last_block_needed) {
		uint32_t need = last_block_needed - have + 1;

		ret = hifs_inode_append_extent(sb, inode, need);
		if (ret)
			return ret;
		have = hifs_inode_total_blocks(inode);
	}

	inode->i_blocks = have;
	{
		uint64_t bytes = (uint64_t)have * hifs_sb_block_size(sb);
		inode->i_bytes = (bytes > U32_MAX) ? U32_MAX : (uint32_t)bytes;
	}
	return 0;
}

static u32 hifs_timespec_to_disk(const struct timespec64 *ts)
{
	time64_t sec = ts->tv_sec;

	if (sec < 0)
		return 0;
	if (sec > U32_MAX)
		return U32_MAX;
	return (u32)sec;
}

static struct timespec64 hifs_disk_to_timespec(u32 seconds)
{
	return (struct timespec64){
		.tv_sec = (time64_t)seconds,
		.tv_nsec = 0,
	};
}


void dump_hifsinode(struct hifs_inode *dmi)
{
	printk(KERN_INFO "----------dump_hifs_inode-------------");
	printk(KERN_INFO "hifs_inode addr: %p", dmi);
	printk(KERN_INFO "hifs_inode->i_version: %u", dmi->i_version);
	printk(KERN_INFO "hifs_inode->i_flags: %u", dmi->i_flags);
	printk(KERN_INFO "hifs_inode->i_mode: %u", dmi->i_mode);
	printk(KERN_INFO "hifs_inode->i_ino: %llu", dmi->i_ino);
	printk(KERN_INFO "hifs_inode->i_hrd_lnk: %u", dmi->i_hrd_lnk);
	printk(KERN_INFO "hifs_inode->extent[0].start: %u", dmi->extents[0].block_start);
	printk(KERN_INFO "hifs_inode->extent[0].count: %u", dmi->extents[0].block_count);
	printk(KERN_INFO "----------[end of dump]-------------");
}

void hifs_destroy_inode(struct inode *inode)
{
	struct hifs_inode *hii = inode->i_private;
	printk(KERN_INFO "#: hifs freeing private data of inode %p (%lu)\n",
		hii, inode->i_ino);
	cache_put_inode(&hii);
}

void hifs_store_inode(struct super_block *sb, struct hifs_inode *hii)
{
	struct buffer_head *bh;
	struct hifs_inode *in_core;
	uint32_t blk = 0;
	uint8_t inode_hash[HIFS_BLOCK_HASH_SIZE];
	enum hifs_hash_algorithm hash_algo = HIFS_HASH_ALGO_NONE;
	int dedupe_ret;

	/* put in-core inode */
	/* Change me: here we just use fact that current allocator is cont.
	 * With smarter allocator the position should be found from itab
	 */
	if (hii->extents[0].block_start == 0) {
		hifs_err("inode %llu missing extent[0] start", hii->i_ino);
		return;
	}
	blk = hii->extents[0].block_start - 1;

	bh = sb_bread(sb, blk);
	BUG_ON(!bh);

	in_core = (struct hifs_inode *)(bh->b_data);
	memcpy(in_core, hii, sizeof(*in_core));

	hifs_cache_mark_inode(sb, hii->i_ino);
	hifs_cache_mark_present(sb, blk);
	hifs_cache_mark_dirty(sb, blk);

	dedupe_ret = hifs_dedupe_writes(sb, blk, bh->b_data,
					sb->s_blocksize, inode_hash,
					&hash_algo);
	if (dedupe_ret)
		hifs_warning("dedupe placeholder returned %d for inode %llu block %u",
			     dedupe_ret, hii->i_ino, blk);
	else {
		struct hifs_block_fingerprint fp;

		memset(&fp, 0, sizeof(fp));
		if (blk > U32_MAX)
			hifs_warning("inode block %u exceeds fingerprint width", blk);
		fp.block_no = blk;
		memcpy(fp.hash, inode_hash, HIFS_BLOCK_HASH_SIZE);
		fp.hash_algo = (uint8_t)hash_algo;

		hii->i_block_fingerprints[0] = fp;
		in_core->i_block_fingerprints[0] = fp;
		hii->i_hash_count = max_t(uint16_t, hii->i_hash_count, 1);
		in_core->i_hash_count = hii->i_hash_count;
	}

	mark_buffer_dirty(bh);
	sync_dirty_buffer(bh);
	brelse(bh);
}

/* Here introduce allocation for directory... */
int hifs_add_dir_record(struct super_block *sb, struct inode *dir, struct dentry *dentry, struct inode *inode)
{
	struct buffer_head *bh;
	struct hifs_inode *parent, *hii;
	struct hifs_dir_entry *dir_rec;
	u32 blk, j;
	uint8_t block_hash[HIFS_BLOCK_HASH_SIZE];
	enum hifs_hash_algorithm hash_algo = HIFS_HASH_ALGO_NONE;
	size_t block_index;
	int dedupe_ret;
	u32 logical_block = 0;
	int i;

	parent = dir->i_private;
	hii = parent;

	for (i = 0; i < HIFS_INODE_TSIZE; ++i) {
		const struct hifs_extent *ext = &parent->extents[i];
		u32 blk_end = ext->block_start + ext->block_count;

		if (!ext->block_count)
			continue;

		for (blk = ext->block_start; blk < blk_end; ++blk, ++logical_block) {
			bh = sb_bread(sb, blk);
			BUG_ON(!bh);
			dir_rec = (struct hifs_dir_entry *)(bh->b_data);
			for (j = 0; j < sb->s_blocksize; ++j) {
			/* We found free space */
			if (dir_rec->inode_nr == HIFS_EMPTY_ENTRY) {
				dir_rec->inode_nr = inode->i_ino;
				dir_rec->name_len = strlen(dentry->d_name.name);
				memset(dir_rec->name, 0, 256);
				strcpy(dir_rec->name, dentry->d_name.name);

				hifs_cache_mark_present(sb, blk);
				hifs_cache_mark_dirty(sb, blk);

					block_index = logical_block;
					hifs_cache_mark_dirent(sb, dir_rec->inode_nr);

					hash_algo = HIFS_HASH_ALGO_NONE;
					dedupe_ret = hifs_dedupe_writes(sb, blk, bh->b_data,
									sb->s_blocksize, block_hash,
									&hash_algo);
					if (dedupe_ret)
						hifs_warning("dedupe placeholder returned %d for dir inode %lu block %u",
						     dedupe_ret, dir->i_ino, blk);

					if (block_index >= HIFS_MAX_BLOCK_HASHES) {
						hifs_warning("directory block index %zu exceeds hash capacity",
							     block_index);
						block_index = HIFS_MAX_BLOCK_HASHES - 1;
					}

					{
						struct hifs_block_fingerprint *fp =
							&parent->i_block_fingerprints[block_index];
						memset(fp, 0, sizeof(*fp));
						fp->block_no = blk;
						memcpy(fp->hash, block_hash, HIFS_BLOCK_HASH_SIZE);
						fp->hash_algo = (uint8_t)hash_algo;
					}
					parent->i_hash_count = max_t(uint16_t, parent->i_hash_count,
						     (uint16_t)(block_index + 1));

					mark_buffer_dirty(bh);
					sync_dirty_buffer(bh);
					brelse(bh);
					parent->i_size += sizeof(*dir_rec);
					parent->i_bytes = parent->i_size;
					hifs_cache_mark_inode(sb, parent->i_ino);
					hifs_store_inode(sb, parent);
					hifs_publish_inode(sb, parent, false);
				{
					struct super_block *sb = dir->i_sb;
					u32 type = DT_UNKNOWN;
					if (inode) {
						if (S_ISDIR(inode->i_mode))
							type = DT_DIR;
						else if (S_ISREG(inode->i_mode))
							type = DT_REG;
						else if (S_ISLNK(inode->i_mode))
							type = DT_LNK;
					} else if (dentry->d_inode) {
						umode_t mode = dentry->d_inode->i_mode;
						if (S_ISDIR(mode))
							type = DT_DIR;
						else if (S_ISREG(mode))
							type = DT_REG;
						else if (S_ISLNK(mode))
							type = DT_LNK;
					}
					hifs_publish_dentry(sb,
							    dir->i_ino,
							    dir_rec->inode_nr,
							    dentry->d_name.name,
							    dir_rec->name_len,
							    type,
							    false);
				}
				return 0;
			}
			dir_rec++;
		}
		bforget(bh);
	}
	}

	printk(KERN_ERR "Unable to put entry to directory");
	return -ENOSPC;
}

int alloc_inode(struct super_block *sb, struct hifs_inode *hii)
{
struct hifs_sb_info *hisb;
	u32 i;

	hisb = sb->s_fs_info;
	hisb->s_inode_cnt += 1;
	hisb->disk.s_inodes_count = cpu_to_le32(hisb->s_inode_cnt);
	hii->i_ino = hisb->s_inode_cnt;
	hii->i_version = HIFS_LAYOUT_VER;
	hii->i_flags = 0;
	hii->i_mode = 0;
	hii->i_size = 0;
	hii->i_hash_count = 0;
	hii->i_hash_reserved = 0;
	memset(hii->i_block_fingerprints, 0, sizeof(hii->i_block_fingerprints));

	{
		uint32_t start = 0;
		int ret_blocks = hifs_alloc_block_range(sb,
							HIFS_INODE_TSIZE + 1,
							&start,
							NULL);
		if (ret_blocks)
			return ret_blocks;
		/* First block is used for the inode itself, remaining are data */
		hii->extents[0].block_start = start + 1;
		hii->extents[0].block_count = HIFS_INODE_TSIZE;
		isave_intable(sb, hii, start);
	}
	for (i = 1; i < HIFS_INODE_TSIZE; ++i) {
		hii->extents[i].block_start = 0;
		hii->extents[i].block_count = 0;
	}
	hii->i_blocks = hifs_inode_total_blocks(hii);
	{
		uint64_t bytes = (uint64_t)hii->i_blocks * hifs_sb_block_size(sb);
		hii->i_bytes = (bytes > U32_MAX) ? U32_MAX : (uint32_t)bytes;
	}

	hifs_store_inode(sb, hii);
	/* TODO: update inode block bitmap */
	hifs_publish_inode(sb, hii, false);

	return 0;
}

struct inode *hifs_new_inode(struct inode *dir, struct dentry *dentry, umode_t mode)
{
	struct super_block *sb = dir->i_sb;
	struct hifs_sb_info *hisb = sb ? sb->s_fs_info : NULL;
	struct hifs_inode *hii;
	struct inode *inode;
	int ret;

	if (!hisb)
		return NULL;

	hii = cache_get_inode();
	if (!hii)
		return NULL;

	ret = alloc_inode(sb, hii);
	if (ret) {
		cache_put_inode(&hii);
		hifs_err("Unable to allocate disk space for inode\n");
		return NULL;
	}

	hii->i_mode = mode;
	hii->i_uid = from_kuid(&init_user_ns, current_fsuid());
	hii->i_gid = from_kgid(&init_user_ns, current_fsgid());
	hii->i_hrd_lnk = 1;

	if (!S_ISREG(mode) && !S_ISDIR(mode)) {
		cache_put_inode(&hii);
		return NULL;
	}

	inode = new_inode(sb);
	if (!inode) {
		cache_put_inode(&hii);
		return NULL;
	}

	inode_init_owner(&nop_mnt_idmap, inode, dir, mode);
	inode->i_ino = hii->i_ino;
	{
		struct timespec64 now = current_time(inode);
		u32 disk_now = hifs_timespec_to_disk(&now);

		inode_set_atime_to_ts(inode, now);
		inode_set_mtime_to_ts(inode, now);
		inode_set_ctime_to_ts(inode, now);

		hii->i_atime = disk_now;
		hii->i_mtime = disk_now;
		hii->i_ctime = disk_now;
	}
	set_nlink(inode, 1);

	hifs_fill_inode(sb, inode, hii);

	if (insert_inode_locked(inode) < 0) {
		iput(inode);
		return NULL;
	}
	mark_inode_dirty(inode);

	ret = hifs_add_dir_record(sb, dir, dentry, inode);
	if (ret) {
		iput(inode);
		return NULL;
	}

	return inode;
}

int hifs_add_ondir(struct inode *inode, struct inode *dir, struct dentry *dentry, umode_t mode)
{
	struct timespec64 now;

	if (!inode || !dir || !dentry)
		return -EINVAL;

	d_instantiate(dentry, inode);
	dget(dentry);
	unlock_new_inode(inode);

	now = current_time(dir);
	inode_set_mtime_to_ts(dir, now);
	inode_set_ctime_to_ts(dir, now);
	mark_inode_dirty(dir);

	return 0;
}

int hifs_create(struct mnt_idmap *idmap, struct inode *dir, struct dentry *dentry, umode_t mode, bool excl)

{
	return hifs_create_inode(dir, dentry, mode);
}

int hifs_create_inode(struct inode *dir, struct dentry *dentry, umode_t mode)
{
	struct inode *inode;

	/* allocate space
	 * create incore inode
	 * create VFS inode
	 * finally ad inode to parent dir
	 */
	inode = hifs_new_inode(dir, dentry, mode);

	if (!inode)
		return -ENOSPC;

	/* add new inode to parent dir */
	return hifs_add_ondir(inode, dir, dentry, mode);
}

int hifs_mkdir(struct mnt_idmap *idmap, struct inode *dir, struct dentry *dentry, umode_t mode)

{
	int ret = 0;

	ret = hifs_create_inode(dir, dentry,  S_IFDIR | mode);

	if (ret) {
		printk(KERN_ERR "Unable to allocate dir.");
		return -ENOSPC;
	}

	dir->i_op = &hifs_inode_operations;
	dir->i_fop = &hifs_dir_operations;

	return 0;
}

int hifs_rmdir(struct inode *dir, struct dentry *dentry)
{
	return 0;
}

void hifs_put_inode(struct inode *inode)
{
	struct hifs_inode *ip = inode->i_private;

	cache_put_inode(&ip);
}

int isave_intable(struct super_block *sb, struct hifs_inode *hii, u32 i_block)
{
	struct buffer_head *bh;
	struct hifs_inode *itab;
	u32 blk = 0;
	u32 *ptr;

	/* get inode table 'file' */
	bh = sb_bread(sb, HIFS_INODE_TABLE_OFFSET);
	itab = (struct hifs_inode*)(bh->b_data);
	/* right now we just allocated one itable extend for files */
	blk = itab->extents[0].block_start;
	bforget(bh);

	bh = sb_bread(sb, blk);
	/* Get block of ino inode*/
	ptr = (u32 *)(bh->b_data);
	/* inodes starts from index 1: -2 offset */
	*(ptr + hii->i_ino - 2) = i_block;

	mark_buffer_dirty(bh);
	sync_dirty_buffer(bh);
	brelse(bh);

	return 0;
}

struct hifs_inode *hifs_iget(struct super_block *sb, ino_t ino)
{
	struct buffer_head *bh;
	struct hifs_inode *ip;
	struct hifs_inode *dinode;
	struct hifs_inode *itab;
	u32 blk = 0;
	u32 *ptr;
	struct hifs_inode *request = NULL;

	if (!hifs_cache_test_inode(sb, ino)) {
		hifs_debug("inode %lu cache miss", (unsigned long)ino);
		request = kzalloc(sizeof(*request), GFP_KERNEL);
		if (!request)
			return NULL;
		request->i_ino = ino;
		if (hifs_publish_inode(sb, request, true) < 0) {
			kfree(request);
			return NULL;
		}
		kfree(request);
	}

	/* get inode table 'file' */
	bh = sb_bread(sb, HIFS_INODE_TABLE_OFFSET);
	itab = (struct hifs_inode*)(bh->b_data);
	/* right now we just allocated one itable extend for files */
	blk = itab->extents[0].block_start;
	bforget(bh);

	bh = sb_bread(sb, blk);
	/* Get block of ino inode*/
	ptr = (u32 *)(bh->b_data);
	/* inodes starts from index 1: -2 offset */
	blk = *(ptr + ino - 2);
	bforget(bh);

	bh = sb_bread(sb, blk);
	ip = (struct hifs_inode*)bh->b_data;
	if (ip->i_ino == HIFS_EMPTY_ENTRY)
		return NULL;
	dinode = cache_get_inode();
	memcpy(dinode, ip, sizeof(*ip));
	hifs_cache_mark_inode(sb, ino);
	bforget(bh);

	return dinode;
}

void hifs_fill_inode(struct super_block *sb, struct inode *des, struct hifs_inode *src)
{
	des->i_mode = src->i_mode;
	des->i_flags = src->i_flags;
	des->i_sb = sb;
	des->i_ino = src->i_ino;
	des->i_private = src;
	des->i_op = &hifs_inode_operations;

	/* Some remote entries may arrive without a type; default to regular file. */
	if (!S_ISDIR(src->i_mode) && !S_ISREG(src->i_mode) && !S_ISLNK(src->i_mode)) {
		hifs_warning("inode %llu missing type (mode %#o); defaulting to regular file",
			     (unsigned long long)src->i_ino, src->i_mode);
		src->i_mode = S_IFREG | 0644;
	}
	inode_set_atime_to_ts(des, hifs_disk_to_timespec(src->i_atime));
	inode_set_mtime_to_ts(des, hifs_disk_to_timespec(src->i_mtime));
	inode_set_ctime_to_ts(des, hifs_disk_to_timespec(src->i_ctime));

	if (S_ISDIR(des->i_mode))
		des->i_fop = &hifs_dir_operations;
	else if (S_ISREG(des->i_mode))
		des->i_fop = &hifs_file_operations;
	else {
		des->i_fop = NULL;
	}

	WARN_ON(!des->i_fop);
}

struct dentry *hifs_lookup(struct inode *dir, struct dentry *child_dentry, unsigned int flags)
{
	struct hifs_inode *dparent = dir->i_private;
	struct hifs_inode *dchild;
	struct super_block *sb = dir->i_sb;
	struct buffer_head *bh;
	struct hifs_dir_entry *dir_rec;
	struct inode *ichild;
	u32 j = 0, i = 0;
	bool retried = false;

retry:

	/* Here we should use cache instead but dumbfs is doing stuff in dummy way.. */
	for (i = 0; i < HIFS_INODE_TSIZE; ++i) {
		const struct hifs_extent *ext = &dparent->extents[i];
		u32 b = ext->block_start;
		u32 count = ext->block_count;
		u32 e = b + count;
		u32 blk = b;

		if (count == 0)
			continue;

		while (blk < e) {
			bool cache_hit = hifs_cache_test_present(sb, blk);
			if (hifs_fetch_block(sb, blk) < 0) {
				blk++;
				continue;
			}
			if (!cache_hit)
				hifs_debug("lookup fetched block %u", blk);

			bh = sb_bread(sb, blk);
			BUG_ON(!bh);
			dir_rec = (struct hifs_dir_entry *)(bh->b_data);

			for (j = 0; j < sb->s_blocksize; ++j) {
				if (dir_rec->inode_nr == HIFS_EMPTY_ENTRY) {
					break;
				}

				if (0 == strcmp(dir_rec->name, child_dentry->d_name.name)) {
					dchild = hifs_iget(sb, dir_rec->inode_nr);
					ichild = new_inode(sb);
					if (!dchild || !ichild) {
						bforget(bh);
						return NULL;
					}
					hifs_fill_inode(sb, ichild, dchild);
					d_add(child_dentry, ichild);
					bforget(bh);
					return child_dentry;
				}
				dir_rec++;
			}

			/* Move to another block */
			blk++;
			bforget(bh);
		}
	}

	if (!retried) {
		retried = true;
		hifs_publish_dentry(sb, dir->i_ino, 0,
				 child_dentry->d_name.name,
				 child_dentry->d_name.len,
				 DT_UNKNOWN,
				 true);
		goto retry;
	}
	d_add(child_dentry, NULL);
	return child_dentry;
}
