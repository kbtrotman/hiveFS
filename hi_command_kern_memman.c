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
struct hifs_inode *shared_inode_outgoing;    // These six Doubly-Linked Lists are our
struct hifs_blocks *shared_block_outgoing;   // processing queues. They are sent & 
struct hifs_cmds *shared_cmd_outgoing;       // received thru the 3 device files known
struct hifs_inode *shared_inode_incoming;    // as the "queues" (to hi_command). We want
struct hifs_blocks *shared_block_incoming;   // to proces them fast, so they're split into
struct hifs_cmds *shared_cmd_incoming;       // incoming & outgoing queues here.
 char *filename;     // The filename we're currently sending/recieving to/from.


// Each device queue has it's own file_operations struct.
struct file_operations inode_fops = {
    .owner = THIS_MODULE,
    .read = hi_comm_device_read,
    .write = hi_comm_device_write,
    .release = hifs_comm_device_release,
};

struct file_operations block_fops = {
    .owner = THIS_MODULE,
    .read = hi_comm_device_read,
    .write = hi_comm_device_write,
    .release = hifs_comm_device_release,
};

struct file_operations cmd_fops = {
    .owner = THIS_MODULE,
    .read = hi_comm_device_read,
    .write = hi_comm_device_write,
    .release = hifs_comm_device_release,
};

// This faops is a single shared atomic variable that holds our protocol integer for comms.
// (Queues are opened by user-space only, but this is a shared variable that can be written
// to by both kernel-space and user-space.) This enforces proper kernel use and that the queues
// are only used when the kernel is ready for data comms.
struct file_operations faops = {
    .open = v_atomic_open,   //virtual defs
    .read = v_atomic_read,
    .write = v_atomic_write,
    .release = v_atomic_release,
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
int v_atomic_open(struct inode *inodep, struct file *filep) {
    pr_info("hivefs: Atomic Device opened\n");
    return 0;
}

int v_atomic_release(struct inode *inodep, struct file *filep) {
    pr_info("hivefs: Atomic Device closed\n");
    return 0;
}

ssize_t v_atomic_read(struct file *filep, char __user *buffer, size_t len, loff_t *offset) {
    int value = atomic_read(&my_atomic_variable);
    if (copy_to_user(buffer, &value, sizeof(int))) {
        return -EFAULT;
    }
    return sizeof(int);
}

ssize_t v_atomic_write(struct file *filep, const char __user *buffer, size_t len, loff_t *offset) {
    int value;
    int count;
    count = sizeof(int);
    if (count != sizeof(int)) {
        return -EINVAL;
    }
    if (copy_from_user(&value, buffer, count)) {
        return -EFAULT;
    }
    atomic_set(&my_atomic_variable, value);
    return sizeof(int);
}
// Override functions here: These we don't need to change.

int register_all_comm_queues(void)
{
    struct class *dev_class;
    major = register_chrdev(0, DEVICE_FILE_INODE, &inode_fops);
    if (major < 0) {
        pr_err("hivefs: Failed to register inode & block comm queue device\n");
        return major;
    }

    major = register_chrdev(0, DEVICE_FILE_BLOCK "_block", &block_fops);
    if (major < 0) {
        pr_err("hivefs: Failed to register block (file) comm queue device\n");
        return major;
    }

    major = register_chrdev(0, DEVICE_FILE_CMDS, &cmd_fops);
    if (major < 0) {
        pr_err("hivefs: Failed to register command comm queue device\n");
        return major;
    }

 #if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0) 
    dev_class = class_create(DEVICE_FILE_INODE);
    device_create(dev_class, NULL, MKDEV(major, 0), NULL, DEVICE_FILE_INODE);
    dev_class = class_create(DEVICE_FILE_BLOCK);
    device_create(dev_class, NULL, MKDEV(major, 1), NULL, DEVICE_FILE_BLOCK);
    dev_class = class_create(DEVICE_FILE_CMDS);
    device_create(dev_class, NULL, MKDEV(major, 2), NULL, DEVICE_FILE_CMDS);
#else 
    dev_class = class_create(THIS_MODULE, DEVICE_FILE_INODE);
    device_create(dev_class, NULL, MKDEV(major, 0), NULL, DEVICE_FILE_INODE);
    dev_class = class_create(THIS_MODULE, DEVICE_FILE_BLOCK);
    device_create(dev_class, NULL, MKDEV(major, 1), NULL, DEVICE_FILE_BLOCK); 
    dev_class = class_create(THIS_MODULE, DEVICE_FILE_CMDS);
    device_create(dev_class, NULL, MKDEV(major, 2), NULL, DEVICE_FILE_CMDS);  
#endif

    pr_info("hivefs: Queue device created on %s\n", DEVICE_FILE_CMDS);
    pr_info("hivefs: Queue device created on %s\n", DEVICE_FILE_INODE);
    pr_info("hivefs: Queue device created on %s\n", DEVICE_FILE_BLOCK);

    return 0;
}

void unregister_all_comm_queues(void)
{
    unregister_chrdev(major, DEVICE_FILE_INODE);     
    unregister_chrdev(major, DEVICE_FILE_BLOCK "_block");
    unregister_chrdev(major, DEVICE_FILE_CMDS);
    kfree(shared_inode_outgoing);
    kfree(shared_block_outgoing);
    kfree(shared_cmd_outgoing);
    kfree(shared_inode_incoming);
    kfree(shared_block_incoming);
    kfree(shared_cmd_incoming);
}

int hifs_comm_device_release(struct inode *inode, struct file *filp) {
    struct my_device_data *data = filp->private_data;

    /* free buffer */
    kfree(data);
    kfree(filename);

    return 0;
}

ssize_t hi_comm_device_read(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos) {
    // Write out to user space.
    ssize_t result = 0;
    char *filename = (char *)filp->f_path.dentry->d_name.name;
    struct list_head *entry;

    if (strcmp(filename, DEVICE_FILE_INODE) == 0) {
        struct hifs_inode *send_data;
        entry = shared_inode_outgoing->hifs_inode_list.next; 
        send_data = list_entry(entry, struct hifs_inode, hifs_inode_list);
        if (copy_to_user((char *)buf, send_data, count) != 0) {
            result = -EFAULT;
        } else {
            result = count;
            hifs_queue_send(buf);
        }
    } else if (strcmp(filename, DEVICE_FILE_BLOCK) == 0) {
        struct hifs_blocks *send_data;
        entry = shared_block_outgoing->hifs_block_list.next; 
        send_data = list_entry(entry, struct hifs_blocks, hifs_block_list);
        if (copy_to_user((char *)buf, send_data, count) != 0) {
            result = -EFAULT;
        } else {
            result = count;
            hifs_queue_send(buf);
        }
    } else if (strcmp(filename, DEVICE_FILE_CMDS) == 0) {
        struct hifs_cmds *send_data;
        entry = shared_cmd_outgoing->hifs_cmd_list.next; 
        send_data = list_entry(entry, struct hifs_cmds, hifs_cmd_list);
        if (copy_to_user((char *)buf, send_data, count) != 0) {
            result = -EFAULT;
        } else {
            result = count;
            hifs_queue_send(buf);
        }
    } else {
        result = -EFAULT;
    }

    return result;
}

ssize_t hi_comm_device_write(struct file *filp, char __user *buf, size_t count, loff_t *f_pos) {
    // Read from user space
    ssize_t result = 0;
    char *filename = (char *)filp->f_path.dentry->d_name.name;

    if (strcmp(filename, DEVICE_FILE_INODE) == 0) {
        struct hifs_inode *new_entry = kmalloc(sizeof(*new_entry), GFP_KERNEL);
        if (!new_entry) {
            // Handle error
            return -ENOMEM;
        }
        if (copy_from_user(new_entry, buf, count) != 0) {
            result = -EFAULT;
        } else {
            result = count;
            list_add(&new_entry->hifs_inode_list, shared_inode_incoming->hifs_inode_list);
            hifs_queue_recv();
        }
    } else if (strcmp(filename, DEVICE_FILE_BLOCK) == 0) {
        struct hifs_blocks *new_entry = kmalloc(sizeof(*new_entry), GFP_KERNEL);
        if (!new_entry) {
            // Handle error
            return -ENOMEM;
        }
        if (copy_from_user(new_entry, buf, count) != 0) {
            result = -EFAULT;
        } else {
            result = count;
            list_add(&new_entry->hifs_block_list, &shared_block_incoming);
            hifs_queue_recv();
        }
    } else if (strcmp(filename, DEVICE_FILE_CMDS) == 0) {
        struct hifs_cmds *new_entry = kmalloc(sizeof(*new_entry), GFP_KERNEL);
        if (!new_entry) {
            // Handle error
            return -ENOMEM;
        }    
        if (copy_from_user(new_entry, buf, count) != 0) {
            result = -EFAULT;
        } else {
            result = count;
            list_add(&new_entry->hifs_cmd_list, &shared_cmd_incoming);
            hifs_queue_recv();
        }
    } else {
        result = -EFAULT;
    }

    return result;
}