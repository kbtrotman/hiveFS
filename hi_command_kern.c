/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

#include "hifs.h"

// Netlink didn't fit what we're trying to do here. We're going to use a memory-mapped file 
// instead to communicate between the kernel and the user-space. Netlink_Generic is too tied 
// to its policy and message format. Even if that weren't a major show-stopper, eventually 
// the comms would overwhelm the kernel with negenl messages. This requires a more direct & 
// flexible communication method.


// Test Data

atomic_t new_command_flag;
int major;
struct inode *shared_inode;
struct buffer_head *shared_block;
char *shared_cmd;

static struct file_operations inode_mmap_fops = {
    .owner = THIS_MODULE,
    .open = mmap_open,
    .release = mmap_close,
    .mmap = inode_mmap,
};

static struct file_operations block_mmap_fops = {
    .owner = THIS_MODULE,
    .open = mmap_open,
    .release = mmap_close,
    .mmap = block_mmap,
};

static struct file_operations cmd_mmap_fops = {
    .owner = THIS_MODULE,
    .open = mmap_open,
    .release = mmap_close,
    .mmap = cmd_mmap,
};


struct inode *create_test_inode(void) {
    struct inode *inode;

    // Allocate memory for the inode
    inode = new_inode(current->fs->nsproxy->mnt_ns);
    if (!inode)
        return NULL;

    // Populate inode fields with test data
    inode->i_mode = S_IFREG | 0644;  // Regular file with read and write permissions
    inode->i_uid.val = "000001";
    inode->i_gid.val = "010101";
    inode->i_blocks = 99;  // Number of 512-byte blocks
    inode->i_atime = inode->i_mtime = inode->i_ctime = (int) (GET_TIME());
    inode->i_bytes = 512;  // File size in bytes
    inode->i_size = 512;  // File size in bytes
    inode->i_ino = 1;  // Inode number

    return inode;
}

void destroy_test_inode(struct inode *inode) {
    iput(inode);
}


int register_all_comm_queues(void)
{
    major = register_chrdev(0, DEVICE_FILE_INODE, &inode_mmap_fops);
    if (major < 0) {
        pr_err("Failed to register inode character device\n");
        return major;
    }

    shared_inode = kmalloc(sizeof(struct inode), GFP_KERNEL);
    if (!shared_inode) {
        unregister_chrdev(major, DEVICE_FILE_INODE);
        pr_err("Failed to allocate inode memory\n");
        return -ENOMEM;
    }

    major = register_chrdev(0, DEVICE_FILE_BLOCK "_block", &block_mmap_fops);
    if (major < 0) {
        pr_err("Failed to register block character device\n");
        kfree(shared_inode);
        return major;
    }

    shared_block = kmalloc(sizeof(struct buffer_head), GFP_KERNEL);
    if (!shared_block) {
        unregister_chrdev(major, DEVICE_FILE_BLOCK "_block");
        kfree(shared_inode);
        pr_err("Failed to allocate block memory\n");
        return -ENOMEM;
    }

    major = register_chrdev(0, DEVICE_FILE_CMDS, &cmd_mmap_fops);
    if (major < 0) {
        pr_err("Failed to register block character device\n");
        kfree(shared_inode);
        kfree(shared_block);
        return major;
    }

    shared_cmd = kmalloc(30, GFP_KERNEL);
    if (!shared_cmd) {
        unregister_chrdev(major, DEVICE_FILE_CMDS);
        kfree(shared_inode);
        kfree(shared_block);
        pr_err("Failed to allocate block memory\n");
        return -ENOMEM;
    }
    return 0;
}

void unregister_all_comm_queues(void)
{
    unregister_chrdev(major, DEVICE_FILE_INODE);     
    unregister_chrdev(major, DEVICE_FILE_BLOCK "_block");
    unregister_chrdev(major, DEVICE_FILE_CMDS "_block");
    kfree(shared_inode);
    kfree(shared_block);
    kfree(shared_cmd);
    return 0;
}

int inode_mmap_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
    unsigned long offset = vmf->pgoff << PAGE_SHIFT;
    struct page *page;

    if (offset >= PAGE_SIZE)
        return VM_FAULT_SIGBUS;

    page = virt_to_page(shared_inode + offset);
    get_page(page);
    vmf->page = page;

    return 0;
}

struct vm_operations_struct inode_mmap_vm_ops = {
    .fault = inode_mmap_fault,
};

static int block_mmap_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
    unsigned long offset = vmf->pgoff << PAGE_SHIFT;
    struct page *page;

    if (offset >= PAGE_SIZE)
        return VM_FAULT_SIGBUS;

    page = virt_to_page(shared_block + offset);
    get_page(page);
    vmf->page = page;

    return 0;
}

struct vm_operations_struct block_mmap_vm_ops = {
    .fault = block_mmap_fault,
};

static int cmd_mmap_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
    unsigned long offset = vmf->pgoff << PAGE_SHIFT;
    struct page *page;

    if (offset >= PAGE_SIZE)
        return VM_FAULT_SIGBUS;

    page = virt_to_page(shared_block + offset);
    get_page(page);
    vmf->page = page;

    return 0;
}

static struct vm_operations_struct cmd_mmap_vm_ops = {
    .fault = cmd_mmap_fault,
};

static int mmap_open(struct inode *inode, struct file *filp)
{
    filp->private_data = inode->i_private;
    return 0;
}

static int mmap_close(struct inode *inode, struct file *filp)
{
    return 0;
}

static int inode_mmap(struct file *filp, struct vm_area_struct *vma)
{
    vma->vm_ops = &inode_mmap_vm_ops;
    vma->vm_flags |= VM_DONTEXPAND | VM_DONTDUMP;
    vma->vm_private_data = filp->private_data;
    inode_mmap_fault(vma, NULL);  // Populate the initial page
    return 0;
}

static int block_mmap(struct file *filp, struct vm_area_struct *vma)
{
    vma->vm_ops = &block_mmap_vm_ops;
    vma->vm_flags |= VM_DONTEXPAND | VM_DONTDUMP;
    vma->vm_private_data = filp->private_data;
    block_mmap_fault(vma, NULL);  // Populate the initial page
    return 0;
}

static int cmd_mmap(struct file *filp, struct vm_area_struct *vma)
{
    vma->vm_ops = &cmd_mmap_vm_ops;
    vma->vm_flags |= VM_DONTEXPAND | VM_DONTDUMP;
    vma->vm_private_data = filp->private_data;
    block_mmap_fault(vma, NULL);  // Populate the initial page
    return 0;
}


int hifs_genl_rcv_inode(struct sk_buff *skb, struct genl_info *info)
{
    char *iname;
    
    // Allocate memory for iname
    iname = kmalloc(50, GFP_KERNEL);
    if (iname == NULL) {
        pr_info(KERN_ERR "GENL: Error allocating memory for iname.\n");
        return -ENOMEM;
    }

    // Check if the acknowledgement attribute is present
    if () {
        nla_strscpy(, 50);
        pr_info(KERN_INFO "GENL: Received Genl INODE Command.\n");
        pr_info(KERN_INFO "GENL: i_name: %s\n", iname);
        kfree(iname); // Free the allocated memory
        return 0;
    } else {
        pr_info(KERN_ERR "GENL: Error processing Genl INODE Command packet.\n");
        kfree(iname); // Free the allocated memory
        return -1;
    }   
}


int hifs_genl_link_up(struct sk_buff *skb, struct genl_info *info)
{
    pr_info(KERN_INFO "GENL: Received Genl Link_Up Command.\n");
    hifs_kern_link.state = HIFS_GENL_LINK_UP;
    pr_info(KERN_INFO "GENL: Netlink_generic link up'd at %ld seconds after hifs start.\n", (GET_TIME() - hifs_genl_link.clockstart));
    hifs_kern_link.last_check = 0;
    hifs_kern_link.last_state = HIFS_GENL_LINK_UP;
    return 0;
}