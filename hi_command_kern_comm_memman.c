/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

#include "hifs.h"

// Globals
atomic_t my_atomic_variable = ATOMIC_INIT(0);
struct class *atomic_class = NULL;
struct device *atomic_device = NULL;
int major;
struct inode *shared_inode;
struct buffer_head *shared_block;
char *shared_cmd;


struct file_operations inode_mmap_fops = {
    .owner = THIS_MODULE,
    .open = mmap_open,
    .release = mmap_close,
    .mmap = inode_mmap,
};

struct file_operations block_mmap_fops = {
    .owner = THIS_MODULE,
    .open = mmap_open,
    .release = mmap_close,
    .mmap = block_mmap,
};

struct file_operations cmd_mmap_fops = {
    .owner = THIS_MODULE,
    .open = mmap_open,
    .release = mmap_close,
    .mmap = cmd_mmap,
};

struct file_operations faops = {
    .open = hifs_atomic_open,
    .read = atomic_read,
    .write = atomic_write,
    .release = hifs_atomic_release,
};

struct vm_operations_struct inode_mmap_vm_ops = {
    .fault = inode_mmap_fault,
};

struct vm_operations_struct block_mmap_vm_ops = {
    .fault = block_mmap_fault,
};

struct vm_operations_struct cmd_mmap_vm_ops = {
    .fault = cmd_mmap_fault,
};

int hifs_atomic_init(void) {
    major = register_chrdev(0, ATOMIC_DEVICE_NAME, &faops);
    if (major < 0) {
        pr_err("hivefs: Failed to register the atmomic character device\n");
        return major;
    }

    atomic_class = class_create(THIS_MODULE, ATOMIC_CLASS_NAME);
    if (IS_ERR(atomic_class)) {
        unregister_chrdev(major, ATOMIC_DEVICE_NAME);
        pr_err("hivefs: Failed to create atomic class\n");
        return PTR_ERR(atomic_class);
    }

    atomic_device = device_create(atomic_class, NULL, MKDEV(major, 0), NULL, ATOMIC_DEVICE_NAME);
    if (IS_ERR(atomic_device)) {
        class_destroy(atomic_class);
        unregister_chrdev(major, ATOMIC_DEVICE_NAME);
        pr_err("hivefs: Failed to create the final atomic device\n");
        return PTR_ERR(atomic_device);
    }

    pr_info("hivefs: Atomic variable module loaded in kernel\n");

    hifs_comm_link_init_change();

    // Start the new monitoring kernel thread
    task = kthread_run(hifs_thread_fn, NULL, "hifs_thread");
    if (IS_ERR(task)) {
        pr_err("hivefs: Failed to create the atomic variable monitoring kernel thread\n");
        return PTR_ERR(task);
    }
    pr_info("hivefs: A new kernel thread was forked to monitor/manage communications\n");

    return 0;
}

void hifs_atomic_exit(void) {
    device_destroy(atomic_class, MKDEV(major, 0));
    class_unregister(atomic_class);
    class_destroy(atomic_class);
    unregister_chrdev(major, ATOMIC_DEVICE_NAME);

    pr_info("hivefs: Atomic variable(s) unloaded\n");
    pr_info("hivefs: Atomic variable module unloaded from kernel\n");
}

// Override functions here: These we don't need to change.
int hifs_atomic_open(struct inode *inodep, struct file *filep) {
    pr_info("hivefs: Atomic Device opened\n");
    return 0;
}

int hifs_atomic_release(struct inode *inodep, struct file *filep) {
    pr_info("hivefs: Atomic Device closed\n");
    return 0;
}

ssize_t v_atomic_read(struct file *filep, char __user *buffer, size_t len, loff_t *offset) {
    pr_info("hivefs-comm: Read from the atomic variable\n");
    return 0;
}

ssize_t atomic_write(struct file *filep, const char __user *buffer, size_t len, loff_t *offset) {
    pr_info("hivefs-comm: Write to the atomic variable\n");
    return 0;
}
// Override functions here: These we don't need to change.

// These are the actual atomic functions here which change the variable below.
int hifs_atomic_read( void ) {

    int value = atomic_read(&my_atomic_variable);
    pr_info("hivefs: Read %d from the atomic variable\n", value);

    return value;
}

int hifs_atomic_write( int value ) {

    atomic_set(&my_atomic_variable, value);
    pr_info("hivefs: Wrote %d to the atomic variable\n", value);
 
    return value;
}
// These are the actual atomic functions here which change the variable.

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
}

vm_fault_t inode_mmap_fault(struct vm_fault *vmf)
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

vm_fault_t block_mmap_fault(struct vm_fault *vmf)
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

vm_fault_t cmd_mmap_fault(struct vm_fault *vmf)
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

int mmap_open(struct inode *inode, struct file *filp)
{
    filp->private_data = inode->i_private;
    return 0;
}

int mmap_close(struct inode *inode, struct file *filp)
{
    return 0;
}

int inode_mmap(struct file *filp, struct vm_area_struct *vma)
{
    vma->vm_ops = &inode_mmap_vm_ops;
    vma->vm_flags |= VM_DONTEXPAND | VM_DONTDUMP;
    vma->vm_private_data = filp->private_data;
    inode_mmap_fault(NULL);  // Populate the initial page
    return 0;
}

int block_mmap(struct file *filp, struct vm_area_struct *vma)
{
    vma->vm_ops = &block_mmap_vm_ops;
    vma->vm_flags |= VM_DONTEXPAND | VM_DONTDUMP;
    vma->vm_private_data = filp->private_data;
    block_mmap_fault(NULL);  // Populate the initial page
    return 0;
}

int cmd_mmap(struct file *filp, struct vm_area_struct *vma)
{
    vma->vm_ops = &cmd_mmap_vm_ops;
    vma->vm_flags |= VM_DONTEXPAND | VM_DONTDUMP;
    vma->vm_private_data = filp->private_data;
    block_mmap_fault(NULL);  // Populate the initial page
    return 0;
}