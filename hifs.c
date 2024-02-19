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
//* He's brilliant and it's a good way to understand the VFS layer, which is difficult
//* to breech without some helpful documentation. It's a great intro to dev'ing simple
//* superblock/inode/dir/file structures.
//*****************************************************************************************

// *********           HiveFS Entry          *********
// This is the entry point for the entire FS in this file.
// Entry and mount point are different, and they are both seperate functions.


#include "hifs.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kevin Trotman");
MODULE_DESCRIPTION("HiveFS - A Hive Mind Filesystem");
MODULE_VERSION("0:0.01-001");

extern atomic_t my_atomic_variable;
extern int major;
extern struct inode *shared_inode;
extern struct buffer_head *shared_block;
extern char *shared_cmd;
extern struct vm_operations_struct block_mmap_vm_ops;
extern struct vm_operations_struct inode_mmap_vm_ops;
extern struct vm_operations_struct cmd_mmap_vm_ops;


struct file_system_type hifs_type = 
{
    .name = "hifs",
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

/*
EXPORT_SYMBOL(hifs_file_operations);
EXPORT_SYMBOL(hifs_dir_operations);
EXPORT_SYMBOL(hifs_sb_operations);
EXPORT_SYMBOL(hifs_inode_cache);
EXPORT_SYMBOL(ktime_get_with_offset);
*/

static int hifs_mkfs(struct file_system_type *fs_type, int flags, const char *dev_name, void *data)
{
    /*
    struct inode *root_inode;
    struct dentry *root_dentry;
    // Allocate a superblock structure


    struct super_block sb = get_sb_nodev(fs_type, flags, data, hifs_fill_super);
    if (IS_ERR(sb)) {
        return PTR_ERR(sb);
    }

    // Create a root inode for the filesystem
    *root_inode = new_inode(sb);
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
    *root_dentry = d_make_root(root_inode);
    if (!root_dentry) {
        iput(root_inode);
        return -ENOMEM;
    }

    // Set the root dentry for the superblock
    sb->s_root = root_dentry;
    */
    return 0;
}

static int __init hifs_init(void)
{
    int ret;

    ret = register_filesystem(&hifs_type);
    if (ret != 0) {
        printk(KERN_ERR "hivefs: Failed to register filesystem\n");
        goto failure;
    } else {
        printk(KERN_INFO "hivefs: Filesystem registered to kernel\n");
    }

    ret = hifs_atomic_init();
    if (ret != 0) {
        printk(KERN_ERR "hivefs: Failed to register atomic sync variable(s)\n");
        goto failure;
    } else {
        printk(KERN_INFO "hivefs: HiFS atomic sync variable(s) registered in kernel\n");
    }

    ret = register_all_comm_queues();
    if (ret != 0) {
        printk(KERN_ERR "hivefs: Failed to register communication queues\n");
        goto failure;
    } else {
        printk(KERN_INFO "hivefs: Memory Mapped Communication queues registered to kernel\n");
    }
    return 0;

failure:
    return -1;
}

static void __exit hifs_exit(void)
{
    int ret;

    hifs_atomic_exit();
    unregister_all_comm_queues();
    ret = unregister_filesystem(&hifs_type);
    if (ret != 0) {
        printk(KERN_ERR "hivefs: Failed to unregister filesystem\n");
        goto failure;
    }

    printk(KERN_INFO "hivefs: hiveFS unregistered\n");

failure:
    printk(KERN_ERR "hivefs: There were errors when attempting to unregister the filesystem\n");
}

module_init(hifs_init);
module_exit(hifs_exit);