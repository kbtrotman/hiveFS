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
#include <linux/workqueue.h>
//#include <asm/uaccess.h>
#include <linux/fs_struct.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/device.h>

#include "hifs_shared_defs.h"

/* Definitions past this point should be specific only to the kernel-space module! */

struct hifs_sb_info;

/*

 Naming convension used:
   hifs_       =  refers to anything dealing with local cachefs or the filesystem as a whole.
   hicom_      =  refers to anything communication layer to talk to user space or remote/direct.
   hicom_prot_ =  refers to anything related to the remote direct comms protocol.

*/

#define hifs_fifo_len_locked(fifo, lock)                     \
({                                                           \
    unsigned long __flags;                                   \
    unsigned int __len;                                      \
    spin_lock_irqsave((lock), __flags);                      \
    __len = kfifo_len((fifo));                               \
    spin_unlock_irqrestore((lock), __flags);                 \
    __len;                                                   \
})

#define hifs_fifo_push_locked(fifo, lock, wq, msg)           \
({                                                           \
    unsigned long __flags;                                   \
    int __ret = 0;                                           \
    if (!(msg)) {                                            \
        __ret = -EINVAL;                                     \
    } else {                                                 \
        spin_lock_irqsave((lock), __flags);                  \
        if (!kfifo_is_full((fifo)))                          \
            kfifo_in((fifo), (msg), 1);                      \
        else                                                 \
            __ret = -ENOSPC;                                 \
        spin_unlock_irqrestore((lock), __flags);             \
        if (!__ret)                                          \
            wake_up_interruptible((wq));                     \
    }                                                        \
    __ret;                                                   \
})

/* Pop one element from a typed KFIFO with locking and optional blocking.
 * fifo:   &kfifo_var
 * lock:   &spinlock_var
 * wq:     wait_queue_head_t variable (not a pointer)
 * msg:    pointer to destination element (typed)
 * nonblock: boolean; if true, do not sleep and return -EAGAIN when empty
 * Returns 0 on success, or a negative errno.
 */
#define hifs_fifo_pop_locked(fifo, lock, wq, msg, nonblock)   \
({                                                           \
    int __ret;                                               \
    if (!(msg)) {                                            \
        __ret = -EINVAL;                                     \
    } else {                                                 \
        for (;;) {                                           \
            unsigned long __flags;                           \
            unsigned int __copied;                           \
            spin_lock_irqsave((lock), __flags);              \
            if (!kfifo_is_empty((fifo))) {                   \
                __copied = kfifo_out((fifo), (msg), 1);      \
                spin_unlock_irqrestore((lock), __flags);     \
                __ret = (__copied == 1) ? 0 : -EFAULT;       \
                break;                                       \
            }                                                \
            spin_unlock_irqrestore((lock), __flags);         \
            if ((nonblock)) {                                \
                __ret = -EAGAIN;                             \
                break;                                       \
            }                                                \
            __ret = wait_event_interruptible((wq),           \
                                !kfifo_is_empty((fifo)));    \
            if (__ret)                                       \
                break;                                       \
        }                                                    \
    }                                                        \
    __ret;                                                   \
})

// Prototypes Here:
/*hifs_kern.c*/
int hifs_thread_fn(void *data);
int hifs_start_queue_thread(void);
int hifs_stop_queue_thread(void);

/*hicom_kern_prot.c*/
int hifs_create_test_inode(void);
void hifs_comm_link_notify_online(void);
void hifs_comm_link_notify_offline(void);
void hicom_process_link_handshake(void);
int hifs_handshake_superblock(struct super_block *sb);
int hifs_handshake_rootdentry(struct super_block *sb);
int hifs_publish_dentry(struct super_block *sb, uint64_t parent_ino, uint64_t child_ino,
			const char *name, u32 name_len, u32 type, bool request_only);
int hifs_publish_inode(struct super_block *sb, const struct hifs_inode *hii,
                       bool request_only);
int hifs_publish_block(struct super_block *sb, uint64_t block_no,
		       const void *data, u32 data_len, bool request_only);
int hifs_fetch_block(struct super_block *sb, uint64_t block_no);
int hifs_push_block(struct super_block *sb, uint64_t block_no,
		    const void *data, u32 data_len);
int hifs_flush_dirty_cache_items(void);

/*hicom_kern_mm.c*/
int hifs_fifo_init(void);
void hifs_fifo_exit(void);
int hifs_cmd_fifo_out_push(const struct hifs_cmds *msg);
int hifs_cmd_fifo_out_push_cstr(const char *command);
int hifs_data_fifo_out_push_buf(const void *buf, size_t len);
int hifs_cmd_fifo_in_push(const struct hifs_cmds *msg);
int hifs_data_fifo_in_push_buf(const void *buf, size_t len);
int hifs_cmd_fifo_in_pop(struct hifs_cmds *msg, bool nonblock);
int hifs_data_fifo_in_pop(struct hifs_data_frame *frame, bool nonblock);

/* hifs_volume_superblock.c */
void hifs_save_sb(struct super_block *sb);
void hifs_put_super(struct super_block *sb);
void hifs_kill_superblock(struct super_block *sb);
int hifs_get_super(struct super_block *sb, void *data, int silent);
struct dentry *hifs_mount(struct file_system_type *fs_type, int flags, const char *dev_name, void *data);


/* hifs_inode.c */
void dump_hifsinode(struct hifs_inode *dmi);
void hifs_destroy_inode(struct inode *inode);
int hifs_create_inode(struct inode *dir, struct dentry *dentry, umode_t mode);
void hifs_store_inode(struct super_block *sb, struct hifs_inode *hii);
int hifs_add_dir_record(struct super_block *sb, struct inode *dir, struct dentry *dentry, struct inode *inode);
int alloc_inode(struct super_block *sb, struct hifs_inode *hii);
struct inode *hifs_new_inode(struct inode *dir, struct dentry *dentry, umode_t mode);
int hifs_add_ondir(struct inode *inode, struct inode *dir, struct dentry *dentry, umode_t mode);
int hifs_create(struct mnt_idmap *idmap, struct inode *dir, struct dentry *dentry, umode_t mode, bool excl);
int hifs_mkdir(struct mnt_idmap *idmap, struct inode *dir, struct dentry *dentry, umode_t mode);
int hifs_rmdir(struct inode *dir, struct dentry *dentry);
void hifs_put_inode(struct inode *inode);
int isave_intable(struct super_block *sb, struct hifs_inode *hii, u32 i_block);
struct hifs_inode *hifs_iget(struct super_block *sb, ino_t ino);
void hifs_fill_inode(struct super_block *sb, struct inode *des, struct hifs_inode *src);
struct dentry *hifs_lookup(struct inode *dir, struct dentry *child_dentry, unsigned int flags);

/* hifs_dir.c */
int hifs_readdir(struct file *filp, struct dir_context *ctx);

/* hifs_file.c */
ssize_t hifs_get_loffset(struct hifs_inode *hii, loff_t off);
ssize_t hifs_read(struct kiocb *iocb, struct iov_iter *to);
ssize_t hifs_write(struct kiocb *iocb, struct iov_iter *from);
int hifs_open_file(struct inode *inode, struct file *filp);
int hifs_release_file(struct inode *inode, struct file *filp);
//ssize_t hifs_alloc_if_necessary(struct hifs_volume_superblock *sb, struct hifs_inode *di, loff_t off, size_t cnt);

/* hifs_cache (shared cache context) */
struct hifs_cache_ctx; /* opaque shared cache context */
struct hifs_inode *cache_get_inode(void);
void cache_put_inode(struct hifs_inode **hii);
int hifs_cache_attach(struct super_block *sb, struct hifs_sb_info *info);
int hifs_cache_save_ctx(struct super_block *sb);
void hifs_cache_detach(struct hifs_sb_info *info);
void hifs_cache_sync_init(struct super_block *sb, struct hifs_sb_info *info);
void hifs_cache_sync_shutdown(struct hifs_sb_info *info);
void hifs_cache_request_flush(struct super_block *sb, bool immediate);
/* Per-volume logical super helpers */
int hifs_volume_load(struct super_block *sb, struct hifs_sb_info *info, bool create);
int hifs_volume_save(struct super_block *sb, const struct hifs_sb_info *info);
void hifs_cache_mark_present(struct super_block *sb, uint64_t block);
void hifs_cache_clear_present(struct super_block *sb, uint64_t block);
bool hifs_cache_test_present(struct super_block *sb, uint64_t block);
void hifs_cache_mark_dirty(struct super_block *sb, uint64_t block);
void hifs_cache_clear_dirty(struct super_block *sb, uint64_t block);
bool hifs_cache_test_dirty(struct super_block *sb, uint64_t block);
void hifs_cache_mark_inode(struct super_block *sb, uint64_t ino);
void hifs_cache_clear_inode(struct super_block *sb, uint64_t ino);
bool hifs_cache_test_inode(struct super_block *sb, uint64_t ino);
void hifs_cache_mark_dirent(struct super_block *sb, uint64_t dent);
void hifs_cache_clear_dirent(struct super_block *sb, uint64_t dent);
bool hifs_cache_test_dirent(struct super_block *sb, uint64_t dent);

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
extern const struct inode_operations hifs_cache_root_inode_ops;
extern const struct file_operations hifs_cache_dir_operations;
extern const struct super_operations hifs_sb_operations;


/**
 * Kernel Inode Cache
 **/
extern struct kmem_cache *hifs_inode_cache;

#define HIFS_CACHE_FLUSH_INTERVAL_DEFAULT (5 * HZ)


struct hifs_sb_info 
{
    struct hifs_disk_superblock disk;
	uint32_t	s_magic;    	/* magic number */
	uint32_t	s_version;    	/* fs version */
	uint32_t	s_blocksize;	/* fs block size */
	uint32_t	s_block_olt;	/* Object location table block */
	uint32_t	s_inode_cnt;	/* number of inodes in inode table */
	uint32_t	s_last_blk;	    /* just move forward with allocation */
	/* Shared cache context across mounts */
    struct hifs_cache_ctx *cache;
    /* Per-mount volume identifier within shared cache */
    uint64_t volume_id;
    /* Per-volume logical super (remote-facing minimal copy) */
    struct hifs_volume_superblock vol_super;
    /* Per-volume root dentry metadata */
    struct hifs_volume_root_dentry root_dentry;
    /* Periodic cache flush orchestration */
    struct super_block *sb;
    struct delayed_work cache_sync_work;
    unsigned long cache_flush_interval;
    atomic_t cache_dirty;
};

/* Return pointer to cached on-disk super in sb->s_fs_info (may be NULL). */
static inline const struct hifs_disk_superblock *hifs_get_cached_superblock(struct super_block *sb)
{
    struct hifs_sb_info *info = sb ? (struct hifs_sb_info *)sb->s_fs_info : NULL;
    return info ? &info->disk : NULL;
}
// END: Globals Here:


/***********************
 * Hive FS Log Functions
 ***********************/
#define HIFS_LOG_FMT(fmt) "%s:%d:%s: " fmt
#define HIFS_LOG_ARGS      __FILE__, __LINE__, __func__
