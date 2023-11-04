#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/uio.h>

#include "hfs.h"

#define FOREACH_BLK_IN_EXT(dmi, blk)					\
u32 _ix = 0, b = 0, e = 0;						\
for (_ix = 0, b = dmi->i_addrb[0], e = dmi->i_addre[0], blk = b-1;	\
_ix < DM_INODE_TSIZE;							\
++_ix, b = dmi->i_addrb[_ix], e = dmi->i_addre[_ix], blk = b-1)		\
	while (++blk < e)

void dump_dminode(struct hfs_inode *dmi)
{
	printk(KERN_INFO "----------dump_hfs_inode-------------");
	printk(KERN_INFO "hfs_inode addr: %p", dmi);
	printk(KERN_INFO "hfs_inode->i_version: %u", dmi->i_version);
	printk(KERN_INFO "hfs_inode->i_flags: %u", dmi->i_flags);
	printk(KERN_INFO "hfs_inode->i_mode: %u", dmi->i_mode);
	printk(KERN_INFO "hfs_inode->i_ino: %u", dmi->i_ino);
	printk(KERN_INFO "hfs_inode->i_hrd_lnk: %u", dmi->i_hrd_lnk);
	printk(KERN_INFO "hfs_inode->i_addrb[0]: %u", dmi->i_addrb[0]);
	printk(KERN_INFO "hfs_inode->i_addre[0]: %u", dmi->i_addre[0]);
	printk(KERN_INFO "----------[end of dump]-------------");
}

struct hfs_inode *cache_get_inode(void)
{
	struct hfs_inode *di;

	di = kmem_cache_alloc(hfs_inode_cache, GFP_KERNEL);
	printk(KERN_INFO "#: hfs cache_get_inode : di=%p\n", di);

	return di;
}

void cache_put_inode(struct hfs_inode **di)
{
	printk(KERN_INFO "#: hfs cache_put_inode : di=%p\n", *di);
	kmem_cache_free(hfs_inode_cache, *di);
	*di = NULL;
}

void hfs_destroy_inode(struct inode *inode) {
	struct hfs_inode *di = inode->i_private;

	printk(KERN_INFO "#: hfs freeing private data of inode %p (%lu)\n",
		di, inode->i_ino);
	cache_put_inode(&di);
}

void hfs_store_inode(struct super_block *sb, struct hfs_inode *dmi)
{
	struct buffer_head *bh;
	struct hfs_inode *in_core;
	uint32_t blk = dmi->i_addrb[0] - 1;

	/* put in-core inode */
	/* Change me: here we just use fact that current allocator is cont.
	 * With smarter allocator the position should be found from itab
	 */
	bh = sb_bread(sb, blk);
	BUG_ON(!bh);

	in_core = (struct hfs_inode *)(bh->b_data);
	memcpy(in_core, dmi, sizeof(*in_core));

	mark_buffer_dirty(bh);
	sync_dirty_buffer(bh);
	brelse(bh);
}

/* Here introduce allocation for directory... */
int hfs_add_dir_record(struct super_block *sb, struct inode *dir, struct dentry *dentry, struct inode *inode)
{
	struct buffer_head *bh;
	struct dm_inode *parent, *dmi;
	struct dm_dir_entry *dir_rec;
	u32 blk, j;

	parent = dir->i_private;
	dmi = parent;

	// Find offset, in dir in extends
	FOREACH_BLK_IN_EXT(parent, blk) {
		bh = sb_bread(sb, blk);
		BUG_ON(!bh);
		dir_rec = (struct hfs_dir_entry *)(bh->b_data);
		for (j = 0; j < sb->s_blocksize; ++j) {
			/* We found free space */
			if (dir_rec->inode_nr == HFS_EMPTY_ENTRY) {
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

int alloc_inode(struct super_block *sb, struct dm_inode *dmi)
{
	struct hfs_superblock *dsb;
	u32 i;

	dsb = sb->s_fs_info;
	dsb->s_inode_cnt += 1;
	dmi->i_ino = dsb->s_inode_cnt;
	dmi->i_version = HFS_LAYOUT_VER;
	dmi->i_flags = 0;
	dmi->i_mode = 0;
	dmi->i_size = 0;

	/* TODO: check if there is any space left on the device */
	/* First block is allocated for in-core inode struct */
	/* Then 4 block for extends: that mean dmi struct is in i_addrb[0]-1 */
	dmi->i_addrb[0] = dsb->s_last_blk + 1;
	dmi->i_addre[0] = dsb->s_last_blk += 4;
	for (i = 1; i < DM_INODE_TSIZE; ++i) {
		dmi->i_addre[i] = 0;
		dmi->i_addrb[i] = 0;
	}

	hfs_store_inode(sb, dmi);
	isave_intable(sb, dmi, (dmi->i_addrb[0] - 1));
	/* TODO: update inode block bitmap */

	return 0;
}

struct inode *hfs_new_inode(struct inode *dir, struct dentry *dentry, umode_t mode)
{
	struct super_block *sb;
	struct hfs_superblock *dsb;
	struct hfs_inode *di;
	struct inode *inode;
	int ret;

	sb = dir->i_sb;
	dsb = sb->s_fs_info;

	di = cache_get_inode();

	/* allocate space:
 	 * sb has last block on it just use it
 	 */
	ret = alloc_inode(sb, di);

	if (ret) {
		cache_put_inode(&di);
		printk(KERN_ERR "Unable to allocate disk space for inode");
		return NULL;
	}
	di->i_mode = mode;

	BUG_ON(!S_ISREG(mode) && !S_ISDIR(mode));

	/* Create VFS inode */
	inode = new_inode(sb);

	hfs_fill_inode(sb, inode, di);

	/* Add new inode to parent dir */
	ret = hfs_add_dir_record(sb, dir, dentry, inode);

	return inode;
}

int hfs_add_ondir(struct inode *inode, struct inode *dir, struct dentry *dentry, umode_t mode)
{
	inode_init_owner(inode, dir, mode);
	d_add(dentry, inode);

	return 0;
}

int hfs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl)
{
	return hfs_create_inode(dir, dentry, mode);
}

int hfs_create_inode(struct inode *dir, struct dentry *dentry, umode_t mode)
{
	struct inode *inode;

	/* allocate space
	 * create incore inode
	 * create VFS inode
	 * finally ad inode to parent dir
	 */
	inode = hfs_new_inode(dir, dentry, mode);

	if (!inode)
		return -ENOSPC;

	/* add new inode to parent dir */
	return hfs_add_ondir(inode, dir, dentry, mode);
}

int hfs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode)
{
	int ret = 0;

	ret = hfs_create_inode(dir, dentry,  S_IFDIR | mode);

	if (ret) {
		printk(KERN_ERR "Unable to allocate dir.");
		return -ENOSPC;
	}

	dir->i_op = &hfs_inode_ops;
	dir->i_fop = &hfs_dir_ops;

	return 0;
}

void hfs_put_inode(struct inode *inode)
{
	struct hfs_inode *ip = inode->i_private;

	cache_put_inode(&ip);
}

int isave_intable(struct super_block *sb, struct dm_inode *dmi, u32 i_block)
{
	struct buffer_head *bh;
	struct hfs_inode *itab;
	u32 blk = 0;
	u32 *ptr;

	/* get inode table 'file' */
	bh = sb_bread(sb, HFS_INODE_TABLE_OFFSET);
	itab = (struct hfs_inode*)(bh->b_data);
	/* right now we just allocated one itable extend for files */
	blk = itab->i_addrb[0];
	bforget(bh);

	bh = sb_bread(sb, blk);
	/* Get block of ino inode*/
	ptr = (u32 *)(bh->b_data);
	/* inodes starts from index 1: -2 offset */
	*(ptr + dmi->i_ino - 2) = i_block;

	mark_buffer_dirty(bh);
	sync_dirty_buffer(bh);
	brelse(bh);

	return 0;
}

struct hfs_inode *hfs_iget(struct super_block *sb, ino_t ino)
{
	struct buffer_head *bh;
	struct hfs_inode *ip;
	struct hfs_inode *dinode;
	struct hfs_inode *itab;
	u32 blk = 0;
	u32 *ptr;

	/* get inode table 'file' */
	bh = sb_bread(sb, HFS_INODE_TABLE_OFFSET);
	itab = (struct hfs_inode*)(bh->b_data);
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
	ip = (struct hfs_inode*)bh->b_data;
	if (ip->i_ino == HFS_EMPTY_ENTRY)
		return NULL;
	dinode = cache_get_inode();
	memcpy(dinode, ip, sizeof(*ip));
	bforget(bh);

	return dinode;
}

void hfs_fill_inode(struct super_block *sb, struct inode *des, struct dm_inode *src)
{
	des->i_mode = src->i_mode;
	des->i_flags = src->i_flags;
	des->i_sb = sb;
	des->i_atime = des->i_ctime = des->i_mtime = current_time(des);
	des->i_ino = src->i_ino;
	des->i_private = src;
	des->i_op = &hfs_inode_ops;

	if (S_ISDIR(des->i_mode))
		des->i_fop = &hfs_dir_ops;
	else if (S_ISREG(des->i_mode))
		des->i_fop = &hfs_file_ops;
	else {
		des->i_fop = NULL;
	}

	WARN_ON(!des->i_fop);
}

struct dentry *hfs_lookup(struct inode *dir, struct dentry *child_dentry, unsigned int flags)
{
	struct dm_inode *dparent = dir->i_private;
	struct dm_inode *dchild;
	struct super_block *sb = dir->i_sb;
	struct buffer_head *bh;
	struct dm_dir_entry *dir_rec;
	struct inode *ichild;
	u32 j = 0, i = 0;

	/* Here we should use cache instead but dummyfs is doing stuff in dummy way.. */
	for (i = 0; i < HFS_INODE_TSIZE; ++i) {
		u32 b = dparent->i_addrb[i] , e = dparent->i_addre[i];
		u32 blk = b;
		while (blk < e) {

			bh = sb_bread(sb, blk);
			BUG_ON(!bh);
			dir_rec = (struct hfs_dir_entry *)(bh->b_data);

			for (j = 0; j < sb->s_blocksize; ++j) {
				if (dir_rec->inode_nr == DM_EMPTY_ENTRY) {
					break;
				}

				if (0 == strcmp(dir_rec->name, child_dentry->d_name.name)) {
					dchild = dm_iget(sb, dir_rec->inode_nr);
					ichild = new_inode(sb);
					if (!dchild) {
						return NULL;
					}
					dm_fill_inode(sb, ichild, dchild);
					inode_init_owner(ichild, dir, dchild->i_mode);
					d_add(child_dentry, ichild);

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