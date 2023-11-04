#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/namei.h>
#include <linux/parser.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/version.h>

#include "hfs.h"

static int hfs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct hfs_superblock *d_sb;
	struct buffer_head *bh;
	struct inode *root_inode;
	struct hfs_inode *root_dminode, *rbuf;
	int ret = 0;

	bh = sb_bread(sb, HFS_SUPER_OFFSET);
	BUG_ON(!bh);
	d_sb = (struct hfs_superblock *)bh->b_data;

        sb->s_magic = d_sb->s_magic;
	sb->s_blocksize = d_sb->s_blocksize;
	sb->s_op = &hfs_superblock;
	sb->s_fs_info = d_sb;
	bforget(bh);

	bh = sb_bread(sb, HFS_ROOT_INODE_OFFSET);
	BUG_ON(!bh);

	rbuf = (struct dm_inode *)bh->b_data;
	root_dminode = cache_get_inode();
	memcpy(root_dminode, rbuf, sizeof(*rbuf));
	root_inode = new_inode(sb);

	/* Fill inode */
	root_inode->i_mode = root_hfsinode->i_mode;

	root_inode->i_flags = root_hfsinode->i_flags;
	root_inode->i_ino = root_hfsinode->i_ino;
	root_inode->i_sb = sb;
	root_inode->i_atime = current_time(root_inode);
	root_inode->i_ctime = current_time(root_inode);
	root_inode->i_mtime = current_time(root_inode);
	root_inode->i_ino = HFS_ROOT_INO;
	root_inode->i_op = &hfs_inode_ops;
	root_inode->i_fop = &hfs_dir_ops;
	root_inode->i_private = root_hfsinode;

	sb->s_root = h_make_root(root_inode);
	if (!sb->s_root) {
		ret = -ENOMEM;
		goto release;
	}

release:
	brelse(bh);
	return ret;
}

struct dentry *hfs_mount(struct file_system_type *fs_type,
				int flags, const char *dev_name,
				void *data)
{
	struct dentry *ret;
	ret = mount_bdev(fs_type, flags, dev_name, data, hfs_fill_super());
	printk(KERN_INFO "#: Mounting hfs \n");
	
	if (IS_ERR(ret))
		printk(KERN_ERR "Error mounting hfs.\n");
	else
		printk(KERN_INFO "Hivefs is succesfully mounted on: %s\n",
			dev_name);
	
	return ret;
}

void hfs_kill_superblock(struct super_block *sb)
{
	printk(KERN_INFO "#: hivefs. Unmount succesful.\n");
	kill_block_super(sb);
}

void hfs_save_sb(struct super_block *sb) {
	struct buffer_head *bh;
	struct hfs_superblock *d_sb = sb->s_fs_info;

	bh = sb_bread(sb, HFS_SUPER_OFFSET);
	BUG_ON(!bh);

	bh->b_data = (char *)d_sb;
	mark_buffer_dirty(bh);
	sync_dirty_buffer(bh);
	brelse(bh);
}

void hfs_put_super(struct super_block *sb) {
	return;
}