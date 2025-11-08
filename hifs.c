/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

//****************************       Shout Out!!!       ***********************************
//* Note that the original code is adapted from a Dublin Linux User's Group presentation
//* given by Maciej Grochowski on the topic of "Writing Linux Filesystems for Fun"
//* available at:  https://www.youtube.com/watch?v=sLR17lUjTpc
//*****************************************************************************************

#include "hifs_shared_defs.h"
#include "hifs.h"

// *******************      HiveFS Entry     *******************
// This is the entry point for the entire FS in this file.
// *******************      HiveFS Entry     *******************

struct file_system_type hifs_type = 
{
    .name = "hivefs",
    .mount = hifs_mount,
    .kill_sb = kill_block_super,
    .owner = THIS_MODULE,
};

const struct file_operations hifs_file_operations = 
{
    .read_iter = hifs_read,
    .write_iter = hifs_write,
    .open = hifs_open_file,
	.release = hifs_release_file,
};

const struct inode_operations hifs_inode_operations = 
{
    .create = hifs_create,
    .lookup = hifs_lookup,
    .mkdir = hifs_mkdir,
    .rmdir = hifs_rmdir,
};

const struct file_operations hifs_dir_operations = 
{
    .iterate_shared = hifs_readdir,
};

/* Cache-only mounts should not be traversed directly by users or tools. */
#define HIFS_CACHE_ACCESS_MSG \
    "This is a cache filesystem. Use hiveFS commands to access it for printing cache data and statistics."

static void hifs_cache_emit_notice(const char *op)
{
    if (op)
        hifs_info("%s: %s\n", op, HIFS_CACHE_ACCESS_MSG);
    else
        hifs_info("%s\n", HIFS_CACHE_ACCESS_MSG);
}

static int hifs_cache_open(struct inode *inode, struct file *file)
{
    hifs_cache_emit_notice("open");
    return -EOPNOTSUPP;
}

static int hifs_cache_readdir(struct file *file, struct dir_context *ctx)
{
    hifs_cache_emit_notice("readdir");
    return -EOPNOTSUPP;
}

static struct dentry *hifs_cache_lookup(struct inode *dir, struct dentry *dentry,
                                        unsigned int flags)
{
    hifs_cache_emit_notice("lookup");
    return ERR_PTR(-EOPNOTSUPP);
}

static int hifs_cache_permission(struct mnt_idmap *idmap, struct inode *inode, int mask)
{
    hifs_cache_emit_notice("permission");
    return -EOPNOTSUPP;
}

static int hifs_cache_getattr(struct mnt_idmap *idmap, const struct path *path,
                              struct kstat *stat, u32 request_mask, unsigned int flags)
{
    hifs_cache_emit_notice("getattr");
    return -EOPNOTSUPP;
}

const struct file_operations hifs_cache_dir_operations =
{
    .owner = THIS_MODULE,
    .open = hifs_cache_open,
    .iterate_shared = hifs_cache_readdir,
};

const struct inode_operations hifs_cache_root_inode_ops =
{
    .lookup = hifs_cache_lookup,
    .permission = hifs_cache_permission,
    .getattr = hifs_cache_getattr,
};

static int hifs_sync_fs(struct super_block *sb, int wait)
{
    struct hifs_sb_info *info = sb ? (struct hifs_sb_info *)sb->s_fs_info : NULL;
    int ret = 0;

    (void)wait;

    if (!info)
        return 0;

    cancel_delayed_work_sync(&info->cache_sync_work);

    ret = hifs_cache_save_ctx(sb);
    if (!ret)
        atomic_set(&info->cache_dirty, 0);

    if (atomic_read(&info->cache_dirty))
        hifs_cache_request_flush(sb, false);

    return ret;
}

const struct super_operations hifs_sb_operations = 
{
	.destroy_inode = hifs_destroy_inode,
	.put_super = hifs_put_super,
	.sync_fs = hifs_sync_fs,
};
// *******************      HiveFS Entry     *******************


static int __init hifs_init(void)
{
    int ret;

    ret = register_filesystem(&hifs_type);
    if (ret != 0) {
        hifs_err("Failed to register filesystem\n");
        goto failure;
    } else {
        hifs_info("Filesystem registered to kernel\n");
    }

    hifs_inode_cache = kmem_cache_create("hifs_inode",
                                         sizeof(struct hifs_inode),
                                         0, SLAB_HWCACHE_ALIGN, NULL);
    if (!hifs_inode_cache) {
        hifs_err("Failed to create inode slab cache\n");
        ret = -ENOMEM;
        goto err_unregister_fs;
    }

    ret = hifs_fifo_init();
    if (ret != 0) {
        hifs_err("Failed to initialise communications interface\n");
        goto err_destroy_cache;
    } else {
        hifs_info("Communication control device registered\n");
    }

    ret = hifs_start_queue_thread();
    if (ret != 0) {
        hifs_err("Failed to start hivefs comms management routine\n");
        goto err_fifo_exit;
    }

    hifs_info("hive-fs ringbuffer communication manager thread started successful\n");
    return 0;

err_fifo_exit:
    hifs_fifo_exit();
err_destroy_cache:
    kmem_cache_destroy(hifs_inode_cache);
    hifs_inode_cache = NULL;
err_unregister_fs:
    unregister_filesystem(&hifs_type);
failure:
    hifs_err("There were errors when attempting to register the filesystem\n");
    return ret;
}

static void __exit hifs_exit(void)
{
    int ret;

    /* Ensure any pending cache items are flushed before teardown. */
    hifs_flush_dirty_cache_items();

    hifs_stop_queue_thread();
    hifs_fifo_exit();

    if (!hlist_empty(&hifs_type.fs_supers))
        hifs_warning("hivefs: filesystem still mounted during module unload");

    if (hifs_inode_cache) {
        kmem_cache_shrink(hifs_inode_cache);
        kmem_cache_destroy(hifs_inode_cache);
        hifs_inode_cache = NULL;
    }

    ret = unregister_filesystem(&hifs_type);
    if (ret != 0) {
        hifs_err("Failed to unregister filesystem\n");
        goto failure;
    }
    hifs_info("hiveFS unregistered\n");
    return;

failure:
    hifs_err("There were errors when attempting to unregister the filesystem\n");
}

module_init(hifs_init);
module_exit(hifs_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kevin Trotman");
MODULE_DESCRIPTION("HiveFS - A Hive Mind Filesystem");
MODULE_VERSION("0:0.01-001");
