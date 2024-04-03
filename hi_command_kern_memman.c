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
int major, i_major, b_major, c_major;
bool new_device_data = false;
wait_queue_head_t waitqueue;

struct hifs_inode *shared_inode_outgoing;    // These six Doubly-Linked Lists are our
struct hifs_blocks *shared_block_outgoing;   // processing queues. They are sent & 
struct hifs_cmds *shared_cmd_outgoing;       // received thru the 3 device files known
struct hifs_inode *shared_inode_incoming;    // as the "queues" (to hi_command). We want
struct hifs_blocks *shared_block_incoming;   // to proces them fast, so they're split into
struct hifs_cmds *shared_cmd_incoming;       // incoming & outgoing queues here.

struct list_head shared_inode_outgoing_lst;    
struct list_head shared_block_outgoing_lst;    
struct list_head shared_cmd_outgoing_lst;       
struct list_head shared_inode_incoming_lst;    
struct list_head shared_block_incoming_lst;   
struct list_head shared_cmd_incoming_lst;   

char *filename;     // The filename we're currently sending/recieving to/from.

bool can_write = false;
bool can_read  = false;

DECLARE_WAIT_QUEUE_HEAD(waitqueue);
DEFINE_MUTEX(inode_mutex);
DEFINE_MUTEX(block_mutex);
DEFINE_MUTEX(cmd_mutex);

// Each device queue has it's own file_operations struct.
struct file_operations inode_fops = {
    .owner = THIS_MODULE,
    .read = hi_comm_inode_device_read,
    .write = hi_comm_inode_device_write,
    .release = hifs_comm_device_release,
    .poll = hifs_inode_device_poll,
};

struct file_operations block_fops = {
    .owner = THIS_MODULE,
    .read = hi_comm_block_device_read,
    .write = hi_comm_block_device_write,
    .release = hifs_comm_device_release,
    .poll = hifs_block_device_poll,
};

struct file_operations cmd_fops = {
    .owner = THIS_MODULE,
    .read = hi_comm_cmd_device_read,
    .write = hi_comm_cmd_device_write,
    .release = hifs_comm_device_release,
    .poll = hifs_cmd_device_poll,
};

// This faops is a single shared atomic variable that holds our protocol integer for comms.
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
    struct class *inode_dev_class, *block_dev_class, *cmd_dev_class;
    i_major = register_chrdev(0, DEVICE_FILE_INODE, &inode_fops);
    if (i_major < 0) {
        pr_err("hivefs: Failed to register inode & block comm queue device\n");
        return i_major;
    }

    b_major = register_chrdev(0, DEVICE_FILE_BLOCK "_block", &block_fops);
    if (b_major < 0) {
        pr_err("hivefs: Failed to register block (file) comm queue device\n");
        return b_major;
    }

    c_major = register_chrdev(0, DEVICE_FILE_CMDS, &cmd_fops);
    if (c_major < 0) {
        pr_err("hivefs: Failed to register command comm queue device\n");
        return c_major;
    }

 #if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0) 
    inode_dev_class = class_create(DEVICE_FILE_INODE);
    device_create(inode_dev_class, NULL, MKDEV(i_major, 0), NULL, DEVICE_FILE_INODE);
    block_dev_class = class_create(DEVICE_FILE_BLOCK);
    device_create(block_dev_class, NULL, MKDEV(b_major, 1), NULL, DEVICE_FILE_BLOCK);
    cmd_dev_class = class_create(DEVICE_FILE_CMDS);
    device_create(cmd_dev_class, NULL, MKDEV(c_major, 2), NULL, DEVICE_FILE_CMDS);
#else 
    inode_dev_class = class_create(THIS_MODULE, DEVICE_FILE_INODE);
    device_create(inode_dev_class, NULL, MKDEV(i_major, 0), NULL, DEVICE_FILE_INODE);
    block_dev_class = class_create(THIS_MODULE, DEVICE_FILE_BLOCK);
    device_create(block_dev_class, NULL, MKDEV(b_major, 1), NULL, DEVICE_FILE_BLOCK); 
    cmd_dev_class = class_create(THIS_MODULE, DEVICE_FILE_CMDS);
    device_create(cmd_dev_class, NULL, MKDEV(c_major, 2), NULL, DEVICE_FILE_CMDS);  
#endif

    pr_info("hivefs: Queue device created on %s\n", DEVICE_FILE_CMDS);
    pr_info("hivefs: Queue device created on %s\n", DEVICE_FILE_INODE);
    pr_info("hivefs: Queue device created on %s\n", DEVICE_FILE_BLOCK);

    return 0;
}

void unregister_all_comm_queues(void)
{
    unregister_chrdev(i_major, DEVICE_FILE_INODE);     
    unregister_chrdev(b_major, DEVICE_FILE_BLOCK "_block");
    unregister_chrdev(c_major, DEVICE_FILE_CMDS);
    unregister_chrdev(major, ATOMIC_DEVICE_NAME);
    kfree(shared_inode_outgoing);
    kfree(shared_block_outgoing);
    kfree(shared_cmd_outgoing);
    kfree(shared_inode_incoming);
    kfree(shared_block_incoming);
    kfree(shared_cmd_incoming);
    kfree(atomic_class);
    kfree(atomic_device);
    kfree(task);
}

int hifs_comm_device_release(struct inode *inode, struct file *filp) {
    /* free buffer */
    if (filename) kfree(filename);
    return 0;
}

ssize_t hi_comm_inode_device_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos) {
    // Write out to user space.
    ssize_t result = 0;

    struct hifs_inode *send_data;
    send_data = list_last_entry(&shared_inode_outgoing_lst, struct hifs_inode, hifs_inode_list);

    for (int lock = 0; lock < 100; lock++){
        if (mutex_trylock(&inode_mutex)) {
            break;
        } else {
            printk(KERN_INFO "hifs_comm: [Inode] Another process is accessing the device. Waiting...\n");
            msleep(10);
        }
        if (lock == 100) { return -EBUSY; }
    }

    if (copy_to_user((char *)buf, send_data, count) != 0) {
        result = (ssize_t)-EFAULT;
    } else {
        result = (ssize_t)count;
    }

    can_write = true;
    //wake up the waitqueue
    wake_up(&waitqueue);

    mutex_unlock(&inode_mutex);
    return (ssize_t)result;
}

ssize_t hi_comm_block_device_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos) {
    // Write out to user space.
    ssize_t result = 0;

    struct hifs_blocks *send_data; 
    send_data = list_last_entry(&shared_block_outgoing_lst, struct hifs_blocks, hifs_block_list);

    for (int lock = 0; lock < 100; lock++){
        if (mutex_trylock(&block_mutex)) {
            break;
        } else {
            printk(KERN_INFO "hifs_comm: [Block] Another process is accessing the device. Waiting...\n");
            msleep(10);
        }
        if (lock == 100) { return -EBUSY; }
    }

    if (copy_to_user((char *)buf, send_data, count) != 0) {
        result = (ssize_t)-EFAULT;
    } else {
        result = (ssize_t)count;
    }

    can_write = true;
    //wake up the waitqueue
    wake_up(&waitqueue);

    mutex_unlock(&block_mutex);
    return (ssize_t)result;
}

ssize_t hi_comm_cmd_device_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos) {
    // Write out to user space.
    ssize_t result = 0;

    struct hifs_cmds *send_data;
    send_data = list_last_entry(&shared_cmd_outgoing_lst, struct hifs_cmds, hifs_cmd_list);

    printk(KERN_INFO "hifs_comm: [CMD] Trasnferring command [%s] to buffer [%s] with [%lu characters\n", send_data->cmd, buf, count);
    for (int lock = 0; lock < 100; lock++){
        if (mutex_trylock(&cmd_mutex)) {
            break;
        } else {
            printk(KERN_INFO "hifs_comm: [CMD] Another process is accessing the device. Waiting...\n");
            msleep(10);
        }
        if (lock == 100) { return -EBUSY; }
    }


    if (copy_to_user(buf, send_data, count) != 0) {
        result = (ssize_t)-EFAULT;
    } else {
        result = (ssize_t)count;
    }

    can_write = true;
    //wake up the waitqueue
    wake_up(&waitqueue);

    mutex_unlock(&cmd_mutex);
    return (ssize_t)result;
}

ssize_t hi_comm_inode_device_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos) {
    // Read from user space
    ssize_t result = 0;

    if (copy_from_user(shared_inode_incoming, buf, count) != 0) {
        result = (ssize_t)-EFAULT;
    } else {
        result =(ssize_t)count;
        list_add(&shared_inode_incoming->hifs_inode_list, &shared_inode_incoming_lst);
    }


    can_read = true;
    //wake up the waitqueue
    wake_up(&waitqueue);

    return (ssize_t)result;
}

ssize_t hi_comm_block_device_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos) {
    // Read from user space
    ssize_t result = 0;

    if (copy_from_user(shared_block_incoming, buf, count) != 0) {
        result = (ssize_t)-EFAULT;
    } else {
        result = (ssize_t)count;
        list_add(&shared_block_incoming->hifs_block_list, &shared_block_incoming_lst);
    }


    can_read = true;
    //wake up the waitqueue
    wake_up(&waitqueue);

    return (ssize_t)result;
}

ssize_t hi_comm_cmd_device_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos) {
    // Read from user space
    ssize_t result = 0;

    if (copy_from_user(shared_cmd_incoming, buf, count) != 0) {
        result = (ssize_t)-EFAULT;
    } else {
        result = (ssize_t)count;
        list_add(&shared_cmd_incoming->hifs_cmd_list, &shared_cmd_incoming_lst);
    }

    can_read = true;
    //wake up the waitqueue
    wake_up(&waitqueue);

    return (ssize_t)result;
}

__poll_t hifs_inode_device_poll (struct file *filp, poll_table *wait) {
    __poll_t mask = 0;
    poll_wait(filp, &waitqueue, wait);

    if( can_read )
    {
        can_read = false;
        mask |= ( POLLIN | POLLRDNORM );
    }

    if( can_write )
    {
        can_write = false;
        mask |= ( POLLOUT | POLLWRNORM );
    }

    // Go through Queues

    return mask;
}

__poll_t hifs_block_device_poll (struct file *filp, poll_table *wait) {
    __poll_t mask = 0;
    poll_wait(filp, &waitqueue, wait);

    if( can_read )
    {
        can_read = false;
        mask |= ( POLLIN | POLLRDNORM );
    }

    if( can_write )
    {
        can_write = false;
        mask |= ( POLLOUT | POLLWRNORM );
    }

    // Go through Queues

    return mask;
}

__poll_t hifs_cmd_device_poll (struct file *filp, poll_table *wait) {
    __poll_t mask = 0;
    poll_wait(filp, &waitqueue, wait);

    if( can_read )
    {
        can_read = false;
        mask |= ( POLLIN | POLLRDNORM );
    }

    if( can_write )
    {
        can_write = false;
        mask |= ( POLLOUT | POLLWRNORM );
    }

    // Go through Queues

    return mask;
}