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

const struct super_operations hifs_sb_operations = 
{
	.destroy_inode = hifs_destroy_inode,
	.put_super = hifs_put_super,
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

    ret = hifs_fifo_init();
    if (ret != 0) {
        hifs_err("Failed to initialise communications interface\n");
        goto failure;
    } else {
        hifs_info("Communication control device registered\n");
    }

    ret = hifs_start_queue_thread();
    if (ret != 0) {
        hifs_err("Failed to start hivefs comms management routine\n");
    } else {
        hifs_info("hive-fs ringbuffer communication manager thread started successful\n");
    }
    return ret;

failure:
    hifs_err("There were errors when attempting to register the filesystem\n");
    return ret;
}

static void __exit hifs_exit(void)
{
    int ret;

    hifs_stop_queue_thread();
    hifs_fifo_exit();

    ret = unregister_filesystem(&hifs_type);
    if (ret != 0) {
        hifs_err("Failed to unregister filesystem\n");
        goto failure;
    }
    hifs_info("hiveFS unregistered\n");

failure:
    hifs_err("There were errors when attempting to unregister the filesystem\n");
}

module_init(hifs_init);
module_exit(hifs_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kevin Trotman");
MODULE_DESCRIPTION("HiveFS - A Hive Mind Filesystem");
MODULE_VERSION("0:0.01-001");
