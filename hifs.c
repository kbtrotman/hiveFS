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

#include "hifs_shared_defs.h"
#include "hifs.h"

// *******************      HiveFS Entry     *******************
// This is the entry point for the entire FS in this file.
// *******************      HiveFS Entry     *******************


extern struct task_struct *task;
extern atomic_t kern_atomic_variable;
extern int major;
extern struct hifs_inode *shared_inode_outgoing;    // These six Doubly-Linked Lists are our
extern struct hifs_blocks *shared_block_outgoing;   // processing queues.
extern struct hifs_cmds *shared_cmd_outgoing;       
extern struct hifs_inode *shared_inode_incoming;    
extern struct hifs_blocks *shared_block_incoming;   
extern struct hifs_cmds *shared_cmd_incoming;       

extern struct list_head shared_inode_outgoing_lst;    
extern struct list_head shared_block_outgoing_lst;    
extern struct list_head shared_cmd_outgoing_lst;       
extern struct list_head shared_inode_incoming_lst;    
extern struct list_head shared_block_incoming_lst;   
extern struct list_head shared_cmd_incoming_lst; 

extern wait_queue_head_t waitqueue;

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
// *******************      HiveFS Entry     *******************


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
        hifs_err("Failed to register filesystem\n");
        goto failure;
    } else {
        hifs_info("Filesystem registered to kernel\n");
    }

    ret = hifs_atomic_init();
    if (ret != 0) {
        hifs_err("Failed to register atomic sync variable(s)\n");
        goto failure;
    } else {
        hifs_info("HiFS atomic sync variable(s) registered in kernel\n");
    }

    ret = register_all_comm_queues();
    if (ret != 0) {
        hifs_err("Failed to register communication queues\n");
        goto failure;
    } else {
        hifs_info("Memory Mapped Communication queues registered to kernel\n");
    }

    ret = hifs_start_queue_thread();
    if (ret != 0) {
        hifs_err("Failed to start hivefs management routine\n");
    } else {
        hifs_info("hivefs queue manager thread started\n");
    }
    return ret;

failure:
    hifs_err("There were errors when attempting to register the filesystem\n");
    return ret;
}

static void __exit hifs_exit(void)
{
    int ret;

    //hifs_stop_queue_thread();
    unregister_all_comm_queues();
    hifs_atomic_exit();
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