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

#include "hifs.h"

static int hifs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct hifs_superblock *h_sb;
	struct buffer_head *bh;
	struct inode *root_inode;
	struct hifs_inode *root_hifsinode, *rbuf;
	int ret = 0;

	bh = sb_bread(sb, HIFS_SUPER_OFFSET);
	BUG_ON(!bh);
	h_sb = (struct hifs_superblock *)bh->b_data;

    sb->s_magic = h_sb->s_magic;
	sb->s_blocksize = h_sb->s_blocksize;
	sb->s_op = &hifs_sb_operations;
	sb->s_fs_info = h_sb;
	bforget(bh);

	bh = sb_bread(sb, HIFS_ROOT_INODE_OFFSET);
	BUG_ON(!bh);

	rbuf = (struct hifs_inode *)bh->b_data;
	root_hifsinode = cache_get_inode();
	memcpy(root_hifsinode, rbuf, sizeof(*rbuf));
	root_inode = new_inode(sb);

	/* Fill inode */
	root_inode->i_mode = root_hifsinode->i_mode;

	root_inode->i_flags = root_hifsinode->i_flags;
	root_inode->i_ino = root_hifsinode->i_ino;
	root_inode->i_sb = sb;
	root_inode->i_atime = current_time(root_inode);
	root_inode->i_ctime = current_time(root_inode);
	root_inode->i_mtime = current_time(root_inode);
	root_inode->i_ino = HIFS_ROOT_INO;
	root_inode->i_op = &hifs_inode_operations;
	root_inode->i_fop = &hifs_dir_operations;
	root_inode->i_private = root_hifsinode;

	sb->s_root = d_make_root(root_inode);
	if (!sb->s_root) {
		ret = -ENOMEM;
		goto release;
	}

release:
	brelse(bh);
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

void hifs_save_sb(struct super_block *sb) {
	struct buffer_head *bh;
	struct hifs_superblock *d_sb = sb->s_fs_info;

	bh = sb_bread(sb, HIFS_SUPER_OFFSET);
	BUG_ON(!bh);

	bh->b_data = (char *)d_sb;
	mark_buffer_dirty(bh);
	sync_dirty_buffer(bh);
	brelse(bh);
}

void hifs_put_super(struct super_block *sb) {
	return;
}