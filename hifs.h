/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */
#pragma once

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/atomic.h>
#include <linux/types.h>
#include <linux/namei.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/uio.h>
#include <linux/aio.h>
#include <linux/poll.h>
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/time.h>
#include <linux/ktime.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/kthread.h>
#include <linux/version.h>
#include <linux/vfs.h>
#include <linux/blkdev.h>
#include <linux/magic.h>
#include <linux/debugfs.h>
//#include <asm/uaccess.h>
#include <linux/fs_struct.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/device.h>

#include "hifs_shared_defs.h"

/* Definitions past this point should be specific only to the kernel-space module! */

extern struct class *inode_dev_class, *block_dev_class, *cmd_dev_class;
extern atomic_t kern_atomic_variable;
extern atomic_t user_atomic_variable;
extern struct device *kern_atomic_device;
extern struct device *user_atomic_device;
extern struct class *kern_atomic_class;
extern struct class *user_atomic_class;
extern wait_queue_head_t waitqueue;
extern wait_queue_head_t thread_wq;

extern bool hifs_poll_write;
extern bool hifs_poll_read;

/*hi_command_kern.c*/
int hifs_create_test_inode(void);
int hifs_thread_fn(void *data);
int hifs_comm_check_program_up( int program );
int hifs_comm_set_program_up( int program );
int hifs_comm_check_program_down( int program );
int hifs_start_queue_thread(void);
int hifs_stop_queue_thread(void);

/*hi_command_kern_memman.c*/
int hifs_atomic_init(void);
void hifs_atomic_exit(void);
int v_atomic_open(struct inode *inodep, struct file *filep);
int v_atomic_release(struct inode *inodep, struct file *filep);
ssize_t k_atomic_read(struct file *filep, char __user *buffer, size_t len, loff_t *offset);
ssize_t k_atomic_write(struct file *filep, const char __user *buffer, size_t len, loff_t *offset);
ssize_t u_atomic_read(struct file *filep, char __user *buffer, size_t len, loff_t *offset);
ssize_t u_atomic_write(struct file *filep, const char __user *buffer, size_t len, loff_t *offset);
int register_all_comm_queues(void);
void unregister_all_comm_queues(void);
int hifs_comm_device_release(struct inode *inode, struct file *filp);
ssize_t hi_comm_inode_device_read(struct file *filep, char __user *buf, size_t count, loff_t *offset);
ssize_t hi_comm_block_device_read(struct file *filep, char __user *buf, size_t count, loff_t *offset);
ssize_t hi_comm_cmd_device_read(struct file *filep, char __user *buf, size_t count, loff_t *offset);
ssize_t hi_comm_inode_device_write(struct file *filep, const char  __user *buffer, size_t count, loff_t *offset);
ssize_t hi_comm_block_device_write(struct file *filep, const char __user *buffer, size_t count, loff_t *offset);
ssize_t hi_comm_cmd_device_write(struct file *filep, const char __user *buffer, size_t count, loff_t *offset);
struct hifs_cmds_user *hifs_parse_cmd_struct( struct hifs_cmds *send_data);
struct hifs_inode_user *hifs_parse_inode_struct( struct hifs_inode *send_data);
struct hifs_blocks_user *hifs_parse_block_struct( struct hifs_blocks *send_data);
__poll_t hifs_inode_device_poll (struct file *filp, poll_table *wait);
__poll_t hifs_block_device_poll (struct file *filp, poll_table *wait);
__poll_t hifs_cmd_device_poll (struct file *filp, poll_table *wait);

/* hi_superblock.c */
void hifs_save_sb(struct super_block *sb);
void hifs_put_super(struct super_block *sb);
void hifs_kill_superblock(struct super_block *sb);
int hifs_fill_super(struct super_block *sb, void *data, int silent);
struct dentry *hifs_mount(struct file_system_type *fs_type, int flags, const char *dev_name, void *data);

/* hi_inode.c */
void dump_hifsinode(struct hifs_inode *dmi);
void hifs_destroy_inode(struct inode *inode);
void hifs_store_inode(struct super_block *sb, struct hifs_inode *hii);
int hifs_add_dir_record(struct super_block *sb, struct inode *dir, struct dentry *dentry, struct inode *inode);
int alloc_inode(struct super_block *sb, struct hifs_inode *hii);
struct inode *hifs_new_inode(struct inode *dir, struct dentry *dentry, umode_t mode);
int hifs_add_ondir(struct inode *inode, struct inode *dir, struct dentry *dentry, umode_t mode);
int hifs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl);
int hifs_create_inode(struct inode *dir, struct dentry *dentry, umode_t mode);
int hifs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode);
int hifs_rmdir(struct inode *dir, struct dentry *dentry);
void hifs_put_inode(struct hifs_inode *inode);
int isave_intable(struct super_block *sb, struct hifs_inode *hii, u32 i_block);
struct hifs_inode *hifs_iget(struct super_block *sb, ino_t ino);
void hifs_fill_inode(struct super_block *sb, struct inode *des, struct hifs_inode *src);
struct dentry *hifs_lookup(struct inode *dir, struct dentry *child_dentry, unsigned int flags);

/* hi_dir.c */
int hifs_readdir(struct file *filp, struct dir_context *ctx);

/* hi_file.c */
ssize_t hifs_get_loffset(struct hifs_inode *hii, loff_t off);
ssize_t hifs_read(struct kiocb *iocb, struct iov_iter *to);
ssize_t hifs_write(struct kiocb *iocb, struct iov_iter *from);
int hifs_open_file(struct inode *inode, struct file *filp);
int hifs_release_file(struct inode *inode, struct file *filp);
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

// END: Globals Here:


/***********************
 * Hive FS Log Functions
 ***********************/

#define hifs_emerg(f, a...)						\
		printk(KERN_EMERG "hive-fs: EMERGENCY (file: %s, line: %d): funct: %s:\n", __FILE__, __LINE__, __func__); 	    \
		printk( KERN_EMERG"hive-fs: EMERGENCY " f "\n", ## a);

#define hifs_alert(f, a...)	                    \
		printk(KERN_ALERT "hive-fs: ALERT (file: %s, line: %d): funct: %s:\n", __FILE__, __LINE__, __func__);		    \
		printk(KERN_ALERT "hive-fs: ALERT " f "\n", ## a);

#define hifs_crit(f, a...)	                    \
		printk(KERN_CRIT "hive-fs: CRITICAL (file: %s, line: %d): funct: %s:\n",	__FILE__, __LINE__, __func__);		\
		printk(KERN_CRIT "hive-fs: CRITICAL " f "\n", ##  a);

#define hifs_err(f, a...)	                    \
		printk(KERN_ERR "hive-fs: ERROR (file: %s, line: %d): funct: %s:\n",	__FILE__, __LINE__, __func__);			\
		printk(KERN_ERR "hive-fs: ERROR " f "\n", ## a);

#define hifs_warning(f, a...)	                \
		printk(KERN_WARNING "hive-fs: WARNING (file: %s, line: %d): funct: %s:\n", __FILE__, __LINE__, __func__);	    \
		printk(KERN_WARNING "hive-fs: WARNING " f "\n", ## a);

#define hifs_notice(f, a...)	                \
		printk(KERN_NOTICE "hive-fs: NOTICE (file: %s, line: %d): funct: %s:\n",	__FILE__, __LINE__, __func__);		\
		printk(KERN_NOTICE "hive-fs: NOTICE " f "\n", ## a);

#define hifs_info(f, a...)	                    \
		printk(KERN_INFO "hive-fs: INFO (file: %s, line: %d): funct: %s:\n",	__FILE__, __LINE__, __func__);			\
		printk(KERN_INFO "hive-fs: INFO " f "\n", ## a);

#define hifs_debug(f, a...)		                \
		printk(KERN_DEBUG "hive-fs: DEBUG (file: %s, line: %d): funct: %s:\n", __FILE__, __LINE__, __func__);		    \
		printk(KERN_DEBUG "hive-fs: DEBUG " f "\n", ## a)
