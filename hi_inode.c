/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

#include <linux/delay.h>
#include <linux/uio.h>

#include "hifs.h"

#define FOREAChi_BLK_IN_EXT(dmi, blk)	\
u32 _ix = 0, b = 0, e = 0;				\
for (_ix = 0, b = dmi->i_addrb[0], e = dmi->i_addre[0], blk = b-1;	\
_ix < HIFS_INODE_TSIZE;							\
++_ix, b = dmi->i_addrb[_ix], e = dmi->i_addre[_ix], blk = b-1)		\
	while (++blk < e)

void dump_hifsinode(struct hifs_inode *dmi)
{
	printk(KERN_INFO "----------dump_hifs_inode-------------");
	printk(KERN_INFO "hifs_inode addr: %p", dmi);
	printk(KERN_INFO "hifs_inode->i_version: %u", dmi->i_version);
	printk(KERN_INFO "hifs_inode->i_flags: %u", dmi->i_flags);
	printk(KERN_INFO "hifs_inode->i_mode: %u", dmi->i_mode);
	printk(KERN_INFO "hifs_inode->i_ino: %llu", dmi->i_ino);
	printk(KERN_INFO "hifs_inode->i_hrd_lnk: %u", dmi->i_hrd_lnk);
	printk(KERN_INFO "hifs_inode->i_addrb[0]: %u", dmi->i_addrb[0]);
	printk(KERN_INFO "hifs_inode->i_addre[0]: %u", dmi->i_addre[0]);
	printk(KERN_INFO "----------[end of dump]-------------");
}

void hifs_destroy_inode(struct hifs_inode *inode)
{
	struct hifs_inode *hii = inode->i_private;
	printk(KERN_INFO "#: hifs freeing private data of inode %p (%llu)\n",
		hii, inode->i_ino);
	cache_put_inode(&hii);
}

void hifs_store_inode(struct super_block *sb, struct hifs_inode *hii)
{
	struct buffer_head *bh;
	struct hifs_inode *in_core;
	uint32_t blk = hii->i_addrb[0] - 1;

	/* put in-core inode */
	/* Change me: here we just use fact that current allocator is cont.
	 * With smarter allocator the position should be found from itab
	 */
	bh = sb_bread(sb, blk);
	BUG_ON(!bh);

	in_core = (struct hifs_inode *)(bh->b_data);
	memcpy(in_core, hii, sizeof(*in_core));

	mark_buffer_dirty(bh);
	sync_dirty_buffer(bh);
	brelse(bh);
}

/* Here introduce allocation for directory... */
int hifs_add_dir_record(struct super_block *sb, struct hifs_inode *dir, struct dentry *dentry, struct hifs_inode *inode)
{
	struct buffer_head *bh;
	struct hifs_inode *parent, *hii;
	struct hifs_dir_entry *dir_rec;
	u32 blk, j;

	parent = dir->i_private;
	hii = parent;

	// Find offset, in dir in extends
	FOREAChi_BLK_IN_EXT(parent, blk) {
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
				mark_buffer_dirty(bh);
				sync_dirty_buffer(bh);
				brelse(bh);
				parent->i_size += sizeof(*dir_rec);
				return 0;
			}
			dir_rec++;
		}
		/* Move to another block */
		bforget(bh);
	}

	printk(KERN_ERR "Unable to put entry to directory");
	return -ENOSPC;
}

int alloc_inode(struct super_block *sb, struct hifs_inode *hii)
{
	struct hifs_superblock *hisb;
	u32 i;

	hisb = sb->s_fs_info;
	hisb->s_inode_cnt += 1;
	hii->i_ino = hisb->s_inode_cnt;
	hii->i_version = hifs_LAYOUT_VER;
	hii->i_flags = 0;
	hii->i_mode = 0;
	hii->i_size = 0;

	/* TODO: check if there is any space left on the device */
	/* First block is allocated for in-core inode struct */
	/* Then 4 block for extends: that mean dmi struct is in i_addrb[0]-1 */
	hii->i_addrb[0] = hisb->s_last_blk + 1;
	hii->i_addre[0] = hisb->s_last_blk += 4;
	for (i = 1; i < HIFS_INODE_TSIZE; ++i) {
		hii->i_addre[i] = 0;
		hii->i_addrb[i] = 0;
	}

	hifs_store_inode(sb, hii);
	isave_intable(sb, hii, (hii->i_addrb[0] - 1));
	/* TODO: update inode block bitmap */

	return 0;
}

struct hifs_inode *hifs_new_inode(struct hifs_inode *dir, struct dentry *dentry, umode_t mode)
{
	struct super_block *sb;
	struct hifs_superblock *hisb;
	struct hifs_inode *hii;
	struct hifs_inode *inode;
	int ret;

	sb = dir->i_sb;
	hisb = sb->s_fs_info;

	hii = cache_get_inode();

	/* allocate space:
 	 * sb has last block on it just use it
 	 */
	ret = alloc_inode(sb, hii);

	if (ret) {
		cache_put_inode(&hii);
		printk(KERN_ERR "Unable to allocate disk space for inode");
		return NULL;
	}
	hii->i_mode = mode;

	BUG_ON(!S_ISREG(mode) && !S_ISDIR(mode));

	/* Create VFS inode */
	//inode = new_inode(sb);

	hifs_fill_inode(sb, inode, hii);

	/* Add new inode to parent dir */
	ret = hifs_add_dir_record(sb, dir, dentry, inode);

	return inode;
}

int hifs_add_ondir(struct hifs_inode *inode, struct hifs_inode *dir, struct dentry *dentry, umode_t mode)
{
	//inode_init_owner(inode, dir, mode);
	//d_add(dentry, inode);

	return 0;
}

int hifs_create(struct hifs_inode *dir, struct dentry *dentry, umode_t mode, bool excl)
{
	return hifs_create_inode(dir, dentry, mode);
}

int hifs_create_inode(struct hifs_inode *dir, struct dentry *dentry, umode_t mode)
{
	struct hifs_inode *inode;

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

int hifs_mkdir(struct hifs_inode *dir, struct dentry *dentry, umode_t mode)
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

int hifs_rmdir(struct hifs_inode *dir, struct dentry *dentry)
{
	return 0;
}

void hifs_put_inode(struct hifs_inode *inode)
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
	blk = itab->i_addrb[0];
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

	/* get inode table 'file' */
	bh = sb_bread(sb, HIFS_INODE_TABLE_OFFSET);
	itab = (struct hifs_inode*)(bh->b_data);
	/* right now we just allocated one itable extend for files */
	blk = itab->i_addrb[0];
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
	bforget(bh);

	return dinode;
}

void hifs_fill_inode(struct super_block *sb, struct hifs_inode *des, struct hifs_inode *src)
{
	struct timespec ts;
	ktime_t kt;
	kt = ktime_get_real();
	ts = ktime_to_timespec(kt);

	des->i_mode = src->i_mode;
	des->i_flags = src->i_flags;
	des->i_sb = sb;
//	des->i_atime = des->i_ctime = des->i_mtime = ts;
	des->i_ino = src->i_ino;
	des->i_private = src;
	des->i_op = &hifs_inode_operations;

	if (S_ISDIR(des->i_mode))
		des->i_fop = &hifs_dir_operations;
	else if (S_ISREG(des->i_mode))
		des->i_fop = &hifs_file_operations;
	else {
		des->i_fop = NULL;
	}

	WARN_ON(!des->i_fop);
}

struct dentry *hifs_lookup(struct hifs_inode *dir, struct dentry *child_dentry, unsigned int flags)
{
	struct hifs_inode *dparent = dir->i_private;
	struct hifs_inode *dchild;
	struct super_block *sb = dir->i_sb;
	struct buffer_head *bh;
	struct hifs_dir_entry *dir_rec;
	struct hifs_inode *ichild;
	u32 j = 0, i = 0;

	/* Here we should use cache instead but dummyfs is doing stuff in dummy way.. */
	for (i = 0; i < HIFS_INODE_TSIZE; ++i) {
		u32 b = dparent->i_addrb[i] , e = dparent->i_addre[i];
		u32 blk = b;
		while (blk < e) {

			bh = sb_bread(sb, blk);
			BUG_ON(!bh);
			dir_rec = (struct hifs_dir_entry *)(bh->b_data);

			for (j = 0; j < sb->s_blocksize; ++j) {
				if (dir_rec->inode_nr == HIFS_EMPTY_ENTRY) {
					break;
				}

				if (0 == strcmp(dir_rec->name, child_dentry->d_name.name)) {
					dchild = hifs_iget(sb, dir_rec->inode_nr);
					//ichild = new_inode(sb);
					if (!dchild) {
						return NULL;
					}
					hifs_fill_inode(sb, ichild, dchild);
					//inode_init_owner(ichild, dir, dchild->i_mode);
					//d_add(child_dentry, ichild);

				}
				dir_rec++;
			}

			/* Move to another block */
			blk++;
			bforget(bh);
		}
	}
	return NULL;
}