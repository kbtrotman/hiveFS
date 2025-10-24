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


static struct buffer_head *hifs_bread_at(struct super_block *sb, u64 byte_offset,
					 unsigned int *intra_block_offset)
{
	sector_t block = div64_u64(byte_offset, HIFS_DEFAULT_BLOCK_SIZE);
	unsigned int offset = byte_offset % HIFS_DEFAULT_BLOCK_SIZE;

	if (intra_block_offset)
		*intra_block_offset = offset;

	return sb_bread(sb, block);
}

int hifs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct hifs_disk_superblock *disk_sb;
	struct hifs_sb_info *sb_info = NULL;
	struct buffer_head *bh = NULL;
	struct inode *root_inode = NULL;
	struct hifs_inode *root_hifsinode = NULL;
	unsigned int offset;
	int ret = 0;
	struct timespec64 now;

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

	brelse(bh);
	bh = NULL;

	if (!sb_set_blocksize(sb, sb_info->s_blocksize)) {
		ret = -EINVAL;
		goto out;
	}

	sb->s_magic = sb_info->s_magic;
	sb->s_op = &hifs_sb_operations;
	sb->s_fs_info = sb_info;

	bh = hifs_bread_at(sb, HIFS_ROOT_DENTRY_OFFSET, &offset);
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

	root_inode = new_inode(sb);
	if (!root_inode) {
		ret = -ENOMEM;
		goto out;
	}

	inode_init_owner(&nop_mnt_idmap, root_inode, NULL,
			 root_hifsinode->i_mode);
	root_inode->i_flags = root_hifsinode->i_flags;
	root_inode->i_ino = HIFS_ROOT_INODE;
	root_inode->i_op = &hifs_inode_operations;
	root_inode->i_fop = &hifs_dir_operations;
	root_inode->i_private = root_hifsinode;
	root_inode->i_blocks = root_hifsinode->i_blocks;
	root_inode->i_size = root_hifsinode->i_size;

	now = current_time(root_inode);
	inode_set_atime_to_ts(root_inode, now);
	inode_set_mtime_to_ts(root_inode, now);
	inode_set_ctime_to_ts(root_inode, now);
	inc_nlink(root_inode);

	sb->s_root = d_make_root(root_inode);
	if (!sb->s_root) {
		ret = -ENOMEM;
		goto out;
	}

out:
	if (bh)
		brelse(bh);
	if (ret) {
		if (root_inode)
			iput(root_inode);
		else if (root_hifsinode)
			cache_put_inode(&root_hifsinode);
		kfree(sb_info);
		sb->s_fs_info = NULL;
	}
	return ret;
}

struct dentry *hifs_mount(struct file_system_type *fs_type, int flags, const char *dev_name, void *data)
{
	struct dentry *ret;
	ret = mount_bdev(fs_type, flags, dev_name, data, hifs_fill_super);
	printk(KERN_INFO "#: Mounting hifs \n");
	
	if (IS_ERR(ret))
		printk(KERN_ERR "Error mounting hifs.\n");
	else
		printk(KERN_INFO "hivefs is succesfully mounted on: %s\n",
			dev_name);
	
	return ret;
}

void hifs_kill_superblock(struct super_block *sb)
{
	printk(KERN_INFO "#: hivefs. Unmount succesful.\n");
	kill_block_super(sb);
}

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

void hifs_put_super(struct super_block *sb) 
{
	kfree(sb->s_fs_info);
	sb->s_fs_info = NULL;
}
