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
struct hifs_inode *shared_inode;
char *shared_block;;
char *shared_cmd;

struct my_device_data {
    char *buffer;
};


struct file_operations inode_mmap_fops = {
    .owner = THIS_MODULE,
    .read = hi_comm_device_read,
    .write = hi_comm_device_write,
    .open = hifs_comm_device_open,
    .release = hifs_comm_device_release,
};

struct file_operations block_mmap_fops = {
    .owner = THIS_MODULE,
    .read = hi_comm_device_read,
    .write = hi_comm_device_write,
    .open = hifs_comm_device_open,
    .release = hifs_comm_device_release,
};

struct file_operations cmd_mmap_fops = {
    .owner = THIS_MODULE,
    .read = hi_comm_device_read,
    .write = hi_comm_device_write,
    .open = hifs_comm_device_open,
    .release = hifs_comm_device_release,
};

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
    major = register_chrdev(0, DEVICE_FILE_INODE, &inode_mmap_fops);
    if (major < 0) {
        pr_err("hivefs: Failed to register inode & block comm queue device\n");
        return major;
    }

    major = register_chrdev(0, DEVICE_FILE_BLOCK "_block", &block_mmap_fops);
    if (major < 0) {
        pr_err("hivefs: Failed to register block (file) comm queue device\n");
        return major;
    }

    major = register_chrdev(0, DEVICE_FILE_CMDS, &cmd_mmap_fops);
    if (major < 0) {
        pr_err("hivefs: Failed to register command comm queue device\n");
        return major;
    }

    return 0;
}

void unregister_all_comm_queues(void)
{
    unregister_chrdev(major, DEVICE_FILE_INODE);     
    unregister_chrdev(major, DEVICE_FILE_BLOCK "_block");
    unregister_chrdev(major, DEVICE_FILE_CMDS);
    kfree(shared_inode);
    kfree(shared_block);
    kfree(shared_cmd);
}

int hifs_comm_device_open(struct inode *inode, struct file *filp) {
    struct my_device_data *data;

    data = kzalloc(sizeof(struct my_device_data), GFP_KERNEL);
    if (!data) {
        return -ENOMEM;
    }

    data->buffer = /* allocate and initialize buffer */

    filp->private_data = data;

    return 0;
}

int hifs_comm_device_release(struct inode *inode, struct file *filp) {
    struct my_device_data *data = filp->private_data;

    /* free buffer */

    kfree(data);

    return 0;
}

ssize_t hi_comm_device_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos) {
    ssize_t result = 0;

    struct my_device_data *data = filp->private_data;

    if (*f_pos >= HIFS_BUFFER_SIZE) {
        return 0;
    }

    if (*f_pos + count > HIFS_BUFFER_SIZE) {
        count = HIFS_BUFFER_SIZE - *f_pos;
    }

    if (copy_to_user(data + *f_pos, buf, count) != 0) {
        result = -EFAULT;
    } else {
        *f_pos += count;
        result = count;
    }

    return result;
}

ssize_t hi_comm_device_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos) {
    ssize_t result = 0;

    struct my_device_data *data = filp->private_data;
 
    if (*f_pos >= HIFS_BUFFER_SIZE) {
        return 0;
    }

    if (*f_pos + count > HIFS_BUFFER_SIZE) {
        count = HIFS_BUFFER_SIZE - *f_pos;
    }

    if (copy_from_user(buf, data + *f_pos, count) != 0) {
        result = -EFAULT;
    } else {
        *f_pos += count;
        result = count;
    }

    return result;
}