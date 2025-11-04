/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

#include "hifs.h"

#include <linux/byteorder/generic.h>
#include <linux/math64.h>
#include <linux/mount.h>
#include <linux/time64.h>
#include <linux/limits.h>
#include <linux/string.h>

static u64 hifs_merge_lohi(__le32 lo, __le32 hi)
{
	return (u64)le32_to_cpu(lo) |
	       ((u64)le32_to_cpu(hi) << 32);
}

/* Prepare the per-volume logical superblock for exchange with the cluster. */
static void hifs_prepare_volume_super(struct super_block *sb,
				      struct hifs_sb_info *info)
{
	u64 blocks, free_blocks;
	u64 maxbytes = (u64)MAX_LFS_FILESIZE;
	char label[sizeof(info->disk.s_volume_name)];

	if (!sb || !info)
		return;

	blocks = hifs_merge_lohi(info->disk.s_blocks_count,
				 info->disk.s_blocks_count_hi);
	free_blocks = hifs_merge_lohi(info->disk.s_free_blocks_count,
				      info->disk.s_free_blocks_hi);

	info->vol_super.s_magic = cpu_to_le32(info->s_magic);
	info->vol_super.s_blocksize = cpu_to_le32(info->s_blocksize);
	info->vol_super.s_blocksize_bits = cpu_to_le32(sb->s_blocksize_bits);
	info->vol_super.s_blocks_count = cpu_to_le64(blocks);
	info->vol_super.s_free_blocks = cpu_to_le64(free_blocks);
	info->vol_super.s_inodes_count =
		cpu_to_le64(le32_to_cpu(info->disk.s_inodes_count));
	info->vol_super.s_free_inodes =
		cpu_to_le64(le32_to_cpu(info->disk.s_free_inodes_count));
	info->vol_super.s_maxbytes = cpu_to_le64(maxbytes);

	info->vol_super.s_feature_compat = info->disk.s_feature_compat;
	info->vol_super.s_feature_ro_compat = info->disk.s_feature_ro_compat;
	info->vol_super.s_feature_incompat = info->disk.s_feature_incompat;
	memcpy(info->vol_super.s_uuid, info->disk.s_uuid,
	       sizeof(info->vol_super.s_uuid));

	if (!info->vol_super.s_rev_level)
		info->vol_super.s_rev_level = info->disk.s_rev_level;
	if (!info->vol_super.s_wtime)
		info->vol_super.s_wtime = info->disk.s_wtime;
	if (!info->vol_super.s_flags)
		info->vol_super.s_flags = info->disk.s_flags;

	memset(label, 0, sizeof(label));
	memcpy(label, info->disk.s_volume_name, sizeof(label));
	if (!info->vol_super.s_volume_name[0])
		strscpy(info->vol_super.s_volume_name, label,
			sizeof(info->vol_super.s_volume_name));
}

/* Capture root dentry metadata for remote reconciliation. */
static void hifs_prepare_root_dentry(struct hifs_sb_info *info,
				     const struct hifs_inode *root_inode)
{
	size_t len;

	if (!info || !root_inode)
		return;

	info->root_dentry.rd_inode = cpu_to_le64(root_inode->i_ino);
	info->root_dentry.rd_mode = cpu_to_le32(root_inode->i_mode);
	info->root_dentry.rd_uid = cpu_to_le32(root_inode->i_uid);
	info->root_dentry.rd_gid = cpu_to_le32(root_inode->i_gid);
	info->root_dentry.rd_flags = cpu_to_le32(root_inode->i_flags);
	info->root_dentry.rd_size = cpu_to_le64(root_inode->i_size);
	info->root_dentry.rd_blocks = cpu_to_le64(root_inode->i_blocks);
	info->root_dentry.rd_atime = cpu_to_le32(root_inode->i_atime);
	info->root_dentry.rd_mtime = cpu_to_le32(root_inode->i_mtime);
	info->root_dentry.rd_ctime = cpu_to_le32(root_inode->i_ctime);
	info->root_dentry.rd_links = cpu_to_le32(root_inode->i_hrd_lnk);

	len = strnlen(root_inode->i_name, sizeof(root_inode->i_name));
	if (len == 0) {
		info->root_dentry.rd_name[0] = '/';
		memset(info->root_dentry.rd_name + 1, 0,
		       sizeof(info->root_dentry.rd_name) - 1);
		info->root_dentry.rd_name_len = cpu_to_le32(1);
	} else {
		size_t copy_len = len;
		if (copy_len > sizeof(info->root_dentry.rd_name))
			copy_len = sizeof(info->root_dentry.rd_name);
		memset(info->root_dentry.rd_name, 0,
		       sizeof(info->root_dentry.rd_name));
		memcpy(info->root_dentry.rd_name, root_inode->i_name, copy_len);
		info->root_dentry.rd_name_len = cpu_to_le32((u32)copy_len);
	}
}

static void hifs_apply_root_metadata(struct hifs_inode *inode,
				     const struct hifs_volume_root_dentry *root)
{
	u64 ino;
	u32 mode, uid32, gid32, flags, links, name_len;
	u64 size, blocks;

	if (!inode || !root)
		return;

	ino = le64_to_cpu(root->rd_inode);
	mode = le32_to_cpu(root->rd_mode);
	uid32 = le32_to_cpu(root->rd_uid);
	gid32 = le32_to_cpu(root->rd_gid);
	flags = le32_to_cpu(root->rd_flags);
	size = le64_to_cpu(root->rd_size);
	blocks = le64_to_cpu(root->rd_blocks);
	links = le32_to_cpu(root->rd_links);
	name_len = le32_to_cpu(root->rd_name_len);

	inode->i_ino = ino ? ino : HIFS_ROOT_INODE;
	inode->i_mode = mode;
	inode->i_uid = (u16)((uid32 > USHRT_MAX) ? USHRT_MAX : uid32);
	inode->i_gid = (u16)((gid32 > USHRT_MAX) ? USHRT_MAX : gid32);
	inode->i_flags = (u8)((flags > U8_MAX) ? U8_MAX : flags);
	inode->i_size = (u32)((size > U32_MAX) ? U32_MAX : size);
	inode->i_blocks = (u32)((blocks > U32_MAX) ? U32_MAX : blocks);
	inode->i_bytes = inode->i_size;
	inode->i_hrd_lnk = (u16)((links > USHRT_MAX) ? USHRT_MAX : links);
	inode->i_links = (u8)((links > U8_MAX) ? U8_MAX : links);
	inode->i_atime = le32_to_cpu(root->rd_atime);
	inode->i_mtime = le32_to_cpu(root->rd_mtime);
	inode->i_ctime = le32_to_cpu(root->rd_ctime);

	if (name_len > sizeof(inode->i_name))
		name_len = sizeof(inode->i_name);
	memset(inode->i_name, 0, sizeof(inode->i_name));
	memcpy(inode->i_name, root->rd_name, name_len);
}
/* Read a block at an arbitrary byte offset from the block device. */
static struct buffer_head *hifs_bread_at(struct super_block *sb, u64 byte_offset,
					 unsigned int *intra_block_offset)
{
	sector_t block = div64_u64(byte_offset, HIFS_DEFAULT_BLOCK_SIZE);
	unsigned int offset = byte_offset % HIFS_DEFAULT_BLOCK_SIZE;

	if (intra_block_offset)
		*intra_block_offset = offset;

	return sb_bread(sb, block);
}

/* Parse volume ID from mount data (if any). */
static uint64_t hifs_parse_volume_id(void *data)
{
    uint64_t id = HIFS_VOLUME_CACHE_ID;
    const char *s = data ? (const char *)data : NULL;
    char *end = NULL;

    if (!s || !*s)
        return id;

    if (!strncasecmp(s, "cache", strlen("cache")))
        return HIFS_VOLUME_CACHE_ID;

    if (!strncmp(s, "volume=", strlen("volume=")))
        s += strlen("volume=");
    else if (!strncmp(s, "remote=", strlen("remote=")))
        s += strlen("remote=");

    if (!*s)
        return id;

    {
        unsigned long long v = simple_strtoull(s, &end, 0);
        if (end && end != s)
            id = v;
    }
    return id;
}

/* Load the cache's superblock from disk and initialize the in-memory structures. */
int hifs_get_super(struct super_block *sb, void *data, int silent)
{
	struct hifs_disk_superblock *disk_sb;
	struct hifs_sb_info *sb_info = NULL;
	struct buffer_head *bh = NULL;
	struct inode *root_inode = NULL;
	struct hifs_inode *root_hifsinode = NULL;
	unsigned int offset;
	int ret = 0;
	struct timespec64 now;

	// Whether this is a remote mount or local mount can be determined from 'data' parameter.
	// Either way, we always load the on-disk superblock to initialize our in-memory structures.
	// It doesn't matter to us whether it's local or remote, cache only updates on local mount.
	bh = hifs_bread_at(sb, HIFS_SUPER_OFFSET, &offset);
	if (!bh) {
		ret = -EIO;
		goto out;
	}

	disk_sb = (struct hifs_disk_superblock *)(bh->b_data + offset);
    sb_info = kzalloc(sizeof(*sb_info), GFP_KERNEL);
    if (!sb_info) {
        ret = -ENOMEM;
        goto out;
    }
	sb_info->s_magic = le16_to_cpu(disk_sb->s_magic);
	sb_info->s_version = le32_to_cpu(disk_sb->s_rev_level);
	sb_info->s_blocksize = 1024U << le32_to_cpu(disk_sb->s_log_block_size);
	sb_info->s_block_olt = HIFS_INODE_TABLE_OFFSET / sb_info->s_blocksize;
	sb_info->s_inode_cnt = le32_to_cpu(disk_sb->s_inodes_count);
	sb_info->s_last_blk = HIFS_CACHE_SPACE_START / sb_info->s_blocksize;

	memcpy(&sb_info->disk, disk_sb, sizeof(*disk_sb));
	sb_info->disk.s_magic = cpu_to_le16(sb_info->s_magic);
	sb_info->disk.s_rev_level = cpu_to_le32(sb_info->s_version);

	{
		u32 size = sb_info->s_blocksize >> 10;
		u32 log = 0;
		while (size > 1) {
			size >>= 1;
			log++;
		}
		sb_info->disk.s_log_block_size = cpu_to_le32(log);
	}
	sb_info->disk.s_inodes_count = cpu_to_le32(sb_info->s_inode_cnt);

	ret = hifs_dedupe_init(sb_info);
	if (ret)
		goto out;

	brelse(bh);
	bh = NULL;

	if (!sb_set_blocksize(sb, sb_info->s_blocksize)) {
		ret = -EINVAL;
		goto out;
	}

    sb->s_magic = sb_info->s_magic;
    sb->s_op = &hifs_sb_operations;
    sb->s_fs_info = sb_info;
    hifs_cache_sync_init(sb, sb_info);

    /* Attach shared cache bitmaps from disk (singleton) only if a local cache mount.*/
        /* Remote mounts do not modify the cache yet, so no need to modify it for them. */
    ret = hifs_cache_attach(sb, sb_info);
    if (ret)
        goto out;

    /* Determine volume id (from mount data, if provided) and load/create entry */
	/* This structure holds the local cache volume and all virtual volumes equally.*/
    sb_info->volume_id = hifs_parse_volume_id(data);
    sb_info->is_cache_volume = (sb_info->volume_id == HIFS_VOLUME_CACHE_ID);
    sb_info->is_remote_volume = !sb_info->is_cache_volume;
    ret = hifs_volume_load(sb, sb_info, true);
    if (ret)
        goto out;

    hifs_prepare_volume_super(sb, sb_info);

    /* Read the on-disk root inode from the inode table, not the root directory block. */
    bh = hifs_bread_at(sb, HIFS_INODE_TABLE_OFFSET, &offset);
	if (!bh) {
		ret = -EIO;
		goto out;
	}

	root_hifsinode = cache_get_inode();
	if (!root_hifsinode) {
		ret = -ENOMEM;
		goto out;
	}

	memcpy(root_hifsinode, bh->b_data + offset, sizeof(*root_hifsinode));
	hifs_prepare_root_dentry(sb_info, root_hifsinode);

	if (sb_info->is_remote_volume) {
		int sync_ret = hifs_handshake_superblock(sb);
		if (sync_ret < 0)
			hifs_warning("Remote super/root handshake failed for volume %llu: %d",
				     (unsigned long long)sb_info->volume_id, sync_ret);
	}

	{
		int save_ret = hifs_volume_save(sb, sb_info);
		if (save_ret)
			hifs_warning("Failed to persist per-volume metadata: %d",
				     save_ret);
	}

	if (sb_info->is_remote_volume)
		hifs_apply_root_metadata(root_hifsinode, &sb_info->root_dentry);

	root_inode = new_inode(sb);
	if (!root_inode) {
		ret = -ENOMEM;
		goto out;
	}

	inode_init_owner(&nop_mnt_idmap, root_inode, NULL,
			 root_hifsinode->i_mode);
	root_inode->i_flags = root_hifsinode->i_flags;
	root_inode->i_ino = root_hifsinode->i_ino ? root_hifsinode->i_ino : HIFS_ROOT_INODE;
    if (sb_info->is_cache_volume) {
        root_inode->i_op = &hifs_cache_root_inode_ops;
        root_inode->i_fop = &hifs_cache_dir_operations;
    } else {
        root_inode->i_op = &hifs_inode_operations;
        root_inode->i_fop = &hifs_dir_operations;
    }
	root_inode->i_private = root_hifsinode;
	root_inode->i_blocks = root_hifsinode->i_blocks;
	root_inode->i_size = root_hifsinode->i_size;

	if (sb_info->is_remote_volume) {
		struct timespec64 atime = {
			.tv_sec = root_hifsinode->i_atime,
			.tv_nsec = 0,
		};
		struct timespec64 mtime = {
			.tv_sec = root_hifsinode->i_mtime,
			.tv_nsec = 0,
		};
		struct timespec64 ctime = {
			.tv_sec = root_hifsinode->i_ctime,
			.tv_nsec = 0,
		};
		inode_set_atime_to_ts(root_inode, atime);
		inode_set_mtime_to_ts(root_inode, mtime);
		inode_set_ctime_to_ts(root_inode, ctime);
	} else {
		now = current_time(root_inode);
		inode_set_atime_to_ts(root_inode, now);
		inode_set_mtime_to_ts(root_inode, now);
		inode_set_ctime_to_ts(root_inode, now);
	}
	set_nlink(root_inode,
		  root_hifsinode->i_hrd_lnk ? root_hifsinode->i_hrd_lnk : 1);

	sb->s_root = d_make_root(root_inode);
	if (!sb->s_root) {
		ret = -ENOMEM;
		goto out;
	}

out:
    if (bh)
        brelse(bh);
    if (ret) {
        if (sb_info) {
            hifs_dedupe_shutdown(sb_info);
            hifs_cache_sync_shutdown(sb_info);
            sb_info->sb = NULL;
        }
        if (root_inode)
            iput(root_inode);
        else if (root_hifsinode)
            cache_put_inode(&root_hifsinode);
        kfree(sb_info);
		sb->s_fs_info = NULL;
	}
	return ret;
}

/* Mount a "virtual" or local cache filesystem. Everybody's treated equal now.
 **/
struct dentry *hifs_mount(struct file_system_type *fs_type, int flags, const char *dev_name, void *data)
{
	struct dentry *ret;
    ret = mount_bdev(fs_type, flags, dev_name, data, hifs_get_super);
	printk(KERN_INFO "#: Mounting hifs \n");
	
	if (IS_ERR(ret))
		printk(KERN_ERR "Error mounting hifs.\n");
	else
		printk(KERN_INFO "hivefs is succesfully mounted on: %s\n",
			dev_name);
	
	return ret;
}


/* Teardown the virtual or cache filesystem. */
void hifs_kill_superblock(struct super_block *sb)
{
	printk(KERN_INFO "#: hivefs. Unmount succesful.\n");
	kill_block_super(sb);
}

/* Save the in-memory cache superblock back to disk. */
void hifs_save_sb(struct super_block *sb)
{
	struct buffer_head *bh;
	struct hifs_sb_info *info = sb->s_fs_info;
	unsigned int offset;

	if (!info)
		return;
		

	bh = hifs_bread_at(sb, HIFS_SUPER_OFFSET, &offset);
	if (!bh)
		return;

	memcpy(bh->b_data + offset, &info->disk, sizeof(info->disk));
	mark_buffer_dirty(bh);
	sync_dirty_buffer(bh);
	brelse(bh);
}


/* VFS calls this to teardown the superblocks before dismounting a filesystem. We follow the
 * same process for both local cache mounts and remote mounts. Bu tthe remote cache must be 
 * the last one to go down, after all local mounts are gone.
 * */
void hifs_put_super(struct super_block *sb) 
{
    struct hifs_sb_info *info = sb ? (struct hifs_sb_info *)sb->s_fs_info : NULL;

    if (info)
        hifs_cache_sync_shutdown(info);

    /* Persist latest metadata before teardown. */
    if (sb && sb->s_fs_info) {
        hifs_save_sb(sb);
        hifs_cache_save_ctx(sb);
    }

    /* Ensure any dirty buffers hit the device. */
    if (sb && sb->s_bdev) {
        sync_blockdev(sb->s_bdev);
    }

	/* Then flush the dirty cache items in cache related to that virtual FS to user space */
	hifs_flush_dirty_cache_items();

	/* Finally, free the superblock info structure. */
	hifs_dedupe_shutdown(info);

    hifs_cache_detach(info);
    kfree(info);
    sb->s_fs_info = NULL;
}
