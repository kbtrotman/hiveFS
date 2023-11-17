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

// Definitions Here:
#include "hive_fs_defs.h"

// Prototypes Here:
extern const struct super_operations hifs_sb_operations;
extern const struct inode_operations hifs_inode_operations;
extern const struct file_operations hifs_dir_operations;
extern const struct file_operations hifs_file_operations;

extern struct kmem_cache *hifs_inode_cache;

int hifs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode);
struct dentry *hifs_lookup(struct inode *dir, struct dentry *child_dentry, unsigned int flags);

/* hi_superblock.c */
static int hifs_fill_super(struct super_block *sb, void *data, int silent);
void hifs_put_super(struct super_block *sb);
void hifs_kill_superblock(struct super_block *sb);
struct dentry *hifs_mount(struct file_system_type *ft, int f, const char *dev, void *d);

/* hi_file.c */
ssize_t hifs_read(struct kiocb *iocb, struct iov_iter *to);
ssize_t hifs_write(struct kiocb *iocb, struct iov_iter *from);

/* hi_dir.c */
int hifs_readdir(struct file *filp, struct dir_context *ctx);

/* hi_inode.c */
int isave_intable(struct super_block *sb, struct hifs_inode *dmi, u32 i_block);
void hifs_destroy_inode(struct inode *inode);
void hifs_fill_inode(struct super_block *sb, struct inode *des, struct hifs_inode *src);
int hifs_create_inode(struct inode *dir, struct dentry *dentry, umode_t mode);
int hifs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl);
void hifs_store_inode(struct super_block *sb, struct hifs_inode *dmi);

/* inode cache */
struct hifs_inode *cache_get_inode(void);
void cache_put_inode(struct hifs_inode **di);


/* Definitions specific only tot he module */

/** 
 *Filesystem definition 
 **/
static struct file_system_type hifs_type = {
    .name = "hifs",
    .mount = hifs_mount,
    .kill_sb = kill_block_super,
};

/**
 * The on-disk superblock - last 3 vars synced w/ hive queen.
 **/
struct hifs_superblock {
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
struct hifs_olt {
	uint32_t	inode_table;		/* inode_table block location */
	uint32_t	inode_cnt;	     	/* number of inodes */
	uint64_t	inode_bitmap;		/* inode bitmap block */
};

#endif /* _KERN_HIVEFS_H */  