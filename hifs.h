/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */
#ifndef _KERN_HIVEFS_H
#define _KERN_HIVEFS_H

#include <linux/stddef.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/namei.h>
#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/ktime.h>
#include <linux/version.h>
#include <linux/parser.h>
#include <linux/magic.h>
#include <linux/types.h>
#include <net/netlink.h>
#include <net/genetlink.h>
#include <linux/types.h>
#include <linux/fs.h>

// In the definitions file, those items are common to hi_command in both kernel-space and
// in user-space.  The definitions file is included in both the kernel-space and user-space
// hi_command files.
// COMMON Definitions Here ONLY!
#include "hive_fs_defs.h"
// COMMON Definitions Here ONLY!


/* Definitions past this point should be specific only to the kernel-space module! */

// Prototypes Here:

/* hifs.c */
int hifs_init(void);
void hifs_exit(void);
int hifs_mkfs(struct file_system_type *fs_type, int flags, const char *dev_name, void *data);

/* hi_command_kern_netl.c */
void hifs_netl_send_command_req(char *hive_payload);
int hifs_netl_rcv_acknowledge_command(struct sk_buff *skb, struct genl_info *info);

/* hi_superblock.c */
void hifs_save_sb(struct super_block *sb);
void hifs_put_super(struct super_block *sb);
void hifs_kill_superblock(struct super_block *sb);
int hifs_fill_super(struct super_block *sb, void *data, int silent);
struct dentry *hifs_mount(struct file_system_type *fs_type, int flags, const char *dev_name, void *data);

/* hi_inode.c */
void dump_hifsinode(struct hifs_inode *dmi);
void hifs_destroy_inode(struct hifs_inode *inode);
void hifs_store_inode(struct super_block *sb, struct hifs_inode *hii);
int hifs_add_dir_record(struct super_block *sb, struct hifs_inode *dir, struct dentry *dentry, struct hifs_inode *inode);
int alloc_inode(struct super_block *sb, struct hifs_inode *hii);
struct hifs_inode *hifs_new_inode(struct hifs_inode *dir, struct dentry *dentry, umode_t mode);
int hifs_add_ondir(struct hifs_inode *inode, struct hifs_inode *dir, struct dentry *dentry, umode_t mode);
int hifs_create(struct hifs_inode *dir, struct dentry *dentry, umode_t mode, bool excl);
int hifs_create_inode(struct hifs_inode *dir, struct dentry *dentry, umode_t mode);
int hifs_mkdir(struct hifs_inode *dir, struct dentry *dentry, umode_t mode);
int hifs_rmdir(struct hifs_inode *dir, struct dentry *dentry);
void hifs_put_inode(struct hifs_inode *inode);
int isave_intable(struct super_block *sb, struct hifs_inode *hii, u32 i_block);
struct hifs_inode *hifs_iget(struct super_block *sb, ino_t ino);
void hifs_fill_inode(struct super_block *sb, struct hifs_inode *des, struct hifs_inode *src);
struct dentry *hifs_lookup(struct hifs_inode *dir, struct dentry *child_dentry, unsigned int flags);

/* hi_dir.c */
int hifs_readdir(struct file *filp, struct dir_context *ctx);

/* hi_file.c */
ssize_t hifs_get_loffset(struct hifs_inode *hii, loff_t off);
ssize_t hifs_read(struct kiocb *iocb, struct iov_iter *to);
ssize_t hifs_write(struct kiocb *iocb, struct iov_iter *from);
int hifs_open_file(struct hifs_inode *inode, struct file *filp);
int hifs_release_file(struct hifs_inode *inode, struct file filp);
//ssize_t hifs_alloc_if_necessary(struct hifs_superblock *sb, struct hifs_inode *di, loff_t off, size_t cnt);

/* hi_cache */
struct hifs_inode *cache_get_inode(void);
void cache_put_inode(struct hifs_inode **hii);

// END: Prototypes Here:





// Globals Here:

/** 
 *Filesystem definition 
 **/
extern struct file_system_type hifs_type;

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
extern const struct file_operations hifs_file_operations;
extern const struct inode_operations hifs_inode_operations;
extern const struct file_operations hifs_dir_operations;
extern const struct super_operations hifs_sb_operations;

/**
 * Kernel Inode Cache
 **/
extern struct kmem_cache *hifs_inode_cache;
// END: Globals Here:

#endif /* _KERN_HIVEFS_H */  