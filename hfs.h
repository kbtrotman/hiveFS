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

#include "hive_fs_defs.h"

extern const struct super_operations hfs_sb_ops;
extern const struct inode_operations hfs_inode_ops;
extern const struct file_operations hfs_dir_ops;
extern const struct file_operations hfs_file_ops;

extern struct kmem_cache *hfs_inode_cache;

struct dentry *hfs_mount(struct file_system_type *ft, int f, const char *dev, void *d);
int hfs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode);
struct dentry *hfs_lookup(struct inode *dir, struct dentry *child_dentry, unsigned int flags);

/* h_file.c */
ssize_t hfs_read(struct kiocb *iocb, struct iov_iter *to);
ssize_t hfs_write(struct kiocb *iocb, struct iov_iter *from);

/* h_dir.c */
int hfs_readdir(struct file *filp, struct dir_context *ctx);

/* h_inode.c */
int isave_intable(struct super_block *sb, struct dm_inode *dmi, u32 i_block);
void hfs_destroy_inode(struct inode *inode);
void hfs_fill_inode(struct super_block *sb, struct inode *des, struct dm_inode *src);
int hfs_create_inode(struct inode *dir, struct dentry *dentry, umode_t mode);
int hfs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl);
void hfs_store_inode(struct super_block *sb, struct dm_inode *dmi);

/* inode cache */
struct hfs_inode *cache_get_inode(void);
void cache_put_inode(struct dm_inode **di);

/* h_superblock.c */
void hfs_put_super(struct super_block *sb);
void hfs_kill_superblock(struct super_block *sb);

#endif /* _KERN_HIVEFS_H */  