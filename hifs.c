/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

// Shout Out!!!
// Note that the original code is adapted from a Dublin Linux User's Group presentation
// given by Maciej Grochowski on the topic of "Writing Linux Filesystems for Fun"
// available at:  https://www.youtube.com/watch?v=sLR17lUjTpc
// He's brillian and it's a good way to understand the VFS layer, which is difficult
// to breech without some helpful documentation. It's a great intro to dev'ing simple
// superblock/inode/dir/file structures when beginning a filesystem.

// This is the entry point for the entire FS in this file.
// Though entry and mount point are different, they are both seperate functions.


#include <linux/module.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/namei.h>
#include <linux/stddef.h>

#include "hifs.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kevin Trotman");
MODULE_DESCRIPTION("HiveFS - A Hive Mind Filesystem");
MODULE_VERSION("0:0.01-001")

static struct file_system_type hifs_type = {
    .name = "hifs",
    .mount = hifs_mount,
    .kill_sb = kill_block_super,
};

static int hifs_mount(struct file_system_type *fs_type, int flags, const char *dev_name, void *data)
{
    // Allocate a superblock structure
    struct super_block *sb = get_sb_nodev(fs_type, flags, data, hifs_fill_super);
    if (IS_ERR(sb)) {
        return PTR_ERR(sb);
    }

    // Create a root inode for the filesystem
    struct inode *root_inode = new_inode(sb);
    if (!root_inode) {
        return -ENOMEM;
    }

    // Initialize the root inode
    root_inode->i_ino = 1;
    root_inode->i_sb = sb;
    root_inode->i_op = &hifs_inode_operations;
    root_inode->i_fop = &hifs_dir_operations;
    root_inode->i_mode = S_IFDIR | 0755;

    // Create a dentry for the root directory
    struct dentry *root_dentry = d_make_root(root_inode);
    if (!root_dentry) {
        iput(root_inode);
        return -ENOMEM;
    }

    // Set the root dentry for the superblock
    sb->s_root = root_dentry;

    return 0;
}

static struct file_operations hifs_file_operations = {
    .read = hifs_read_file,
    .write = hifs_write_file,
    .open = hifs_open_file,
    .release = hifs_release_file,
};

static struct inode_operations hifs_inode_operations = {
    .create = hifs_create,
    .lookup = hifs_lookup,
    .mkdir = hifs_mkdir,
    .rmdir = hifs_rmdir,
};

static struct file_operations hifs_dir_operations = {
    .readdir = hifs_readdir,
    .mkdir = hifs_mkdir,
    .rmdir = hifs_rmdir,
};

const struct super_operations hifs_sb_operationss = {
	.destroy_inode = hifs_destroy_inode,
	.put_super = hifs_put_super,
};

static int __init hifs_init(void)
{

    int ret;

    ret = genl_register_family(&doc_exmpl_gnl_family);
    if (ret != 0)
        goto failure;

    int ret = register_filesystem(&hifs_type);
    if (ret != 0) {
        printk(KERN_ERR "hifs: Failed to register filesystem\n");
        return ret;
    }

    printk(KERN_INFO "hifs: Filesystem registered\n");

failure:
    return 0;
}

static void __exit hifs_exit(void)
{
    int ret = genl_unregister_family(&doc_exmpl_gnl_family);
    if (ret != 0)
        printk("unregister failed: %i\n",ret);

    ret = unregister_filesystem(&hifs_type);
    if (ret != 0) {
        printk(KERN_ERR "hifs: Failed to unregister filesystem\n");
    }

    printk(KERN_INFO "hifs: Filesystem unregistered\n");
}

module_init(hifs_init);
module_exit(hifs_exit);