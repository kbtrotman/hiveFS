#ifndef _KERN_HIVEFS_H
#define _KERN_HIVEFS_H

#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/namei.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/version.h>
#include <linux/magic.h>


/* Definitions specific only to the module in this file! */

// Definitions Here:
#include "hive_fs_defs.h"
// Definitions Here:


// Prototypes Here:
/* hi_superblock.c */
static int hifs_fill_super(struct super_block *sb, void *data, int silent);
void hifs_put_super(struct super_block *sb);
void hifs_kill_superblock(struct super_block *sb);
struct dentry *hifs_mount(struct file_system_type *ft, int f, const char *dev, void *d);

/* hi_file.c */
ssize_t hifs_read(struct kiocb *iocb, struct iov_iter *to);
ssize_t hifs_write(struct kiocb *iocb, struct iov_iter *from);
int hifs_open_file(struct inode *inode, struct file *filp);
int hifs_release_file(struct inode *inode, struct file filp);

/* hi_dir.c */
int hifs_readdir(struct file *filp, struct dir_context *ctx);

/* hi_inode.c */
int isave_intable(struct super_block *sb, struct hifs_inode *dmi, u32 i_block);
void hifs_destroy_inode(struct inode *inode);
void hifs_fill_inode(struct super_block *sb, struct inode *des, struct hifs_inode *src);
int hifs_create_inode(struct inode *dir, struct dentry *dentry, umode_t mode);
int hifs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl);
void hifs_store_inode(struct super_block *sb, struct hifs_inode *dmi);
struct dentry *hifs_lookup(struct inode *dir, struct dentry *child_dentry, unsigned int flags);
int hifs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode);
int hifs_rmdir(struct inode *dir, struct dentry *dentry);

/* inode cache */
struct hifs_inode *cache_get_inode(void);
void cache_put_inode(struct hifs_inode **di);
// Prototypes Here:


// Globals Here:
/** 
 *Filesystem definition 
 **/
extern static struct file_system_type hifs_type = 

struct hifs_superblock 
{
	uint32_t	s_magic;    	/* magic number */
	uint32_t	s_version;    	/* fs version */
	uint32_t	s_blocksize;	/* fs block size */
	uint32_t	s_block_olt;	/* Object location table block */
	uint32_t	s_inode_cnt;	/* number of inodes in inode table */
	uint32_t	s_last_blk;	    /* just move forward with allocation */
};

/**
 * Object Location Table
 **/
struct hifs_olt 
{
	uint32_t	inode_table;		/* inode_table block location */
	uint32_t	inode_cnt;	     	/* number of inodes */
	uint64_t	inode_bitmap;		/* inode bitmap block */
};

/**
 * Operations Tables
 **/
extern struct file_operations hifs_file_operations;
extern struct inode_operations hifs_inode_operations;
extern struct file_operations hifs_dir_operations;
extern struct super_operations hifs_sb_operations;
extern kmem_cache *hifs_inode_cache = NULL;
// Globals Here:

#endif /* _KERN_HIVEFS_H */  