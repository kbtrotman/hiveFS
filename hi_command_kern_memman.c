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
struct class *inode_dev_class, *block_dev_class, *cmd_dev_class;

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
    device_destroy(inode_dev_class, MKDEV(i_major, 0));
    device_destroy(block_dev_class, MKDEV(b_major, 1));
    device_destroy(cmd_dev_class, MKDEV(c_major, 2));
    device_destroy(atomic_class, MKDEV(major, 02));
    class_unregister(inode_dev_class);
    class_unregister(block_dev_class);
    class_unregister(cmd_dev_class);
    class_unregister(atomic_class);
    unregister_chrdev(i_major, DEVICE_FILE_INODE);     
    unregister_chrdev(b_major, DEVICE_FILE_BLOCK "_block");
    unregister_chrdev(c_major, DEVICE_FILE_CMDS);
    unregister_chrdev(major, ATOMIC_DEVICE_NAME);
    free_a_list(&shared_inode_outgoing_lst, struct hifs_inode, hifs_inode_list);
    free_a_list(&shared_block_outgoing_lst, struct hifs_blocks, hifs_block_list);
    free_a_list(&shared_cmd_outgoing_lst, struct hifs_cmds, hifs_cmd_list);
    free_a_list(&shared_inode_incoming_lst, struct hifs_inode, hifs_inode_list);
    free_a_list(&shared_block_incoming_lst, struct hifs_blocks, hifs_block_list);
    free_a_list(&shared_cmd_incoming_lst, struct hifs_cmds, hifs_cmd_list);
    kfree(atomic_class);
    kfree(atomic_device);
    kfree(task);
}

int hifs_comm_device_release(struct inode *inode, struct file *filp) {
    /* free buffer */
    if (filename) kfree(filename);
    return 0;
}

ssize_t hi_comm_inode_device_read(struct file *filep, char *buf, size_t count, loff_t *offset) {
    // Write out to user space.
    ssize_t result = 0;

    struct hifs_inode *send_data;
    if (!list_empty(&shared_inode_outgoing_lst)) {
        send_data = list_last_entry(&shared_inode_outgoing_lst, struct hifs_inode, hifs_inode_list);
        if (send_data != NULL) {
            //We don't want to put a 4k block on the stack, so we'll use kernel mem w/ kmalloc.
            struct hifs_inode_user send_data_user;
            list_del(&send_data->hifs_inode_list);    

            for (int lock = 0; lock < 100; lock++){
                if (mutex_trylock(&inode_mutex)) {
                    break;
                } else {
                    printk(KERN_INFO "hifs_comm: [Inode] Another process is accessing the device. Waiting...\n");
                    msleep(10);
                }
                if (lock == 100) { return -EBUSY; }
            }
            strncpy(send_data_user.i_name, send_data->i_name, HIFS_MAX_NAME_SIZE);
            memcpy(send_data_user.i_addre, send_data->i_addre, sizeof(send_data_user.i_addre));
            memcpy(send_data_user.i_addrb, send_data->i_addrb, sizeof(send_data_user.i_addrb));
            send_data_user.i_size = send_data->i_size;
            send_data_user.i_mode = send_data->i_mode;
            send_data_user.i_uid = send_data->i_uid;
            send_data_user.i_gid = send_data->i_gid;
            send_data_user.i_blocks = send_data->i_blocks;
            send_data_user.i_bytes = send_data->i_bytes;
            send_data_user.i_size = send_data->i_size;
            send_data_user.i_ino = send_data->i_ino;
            if (copy_to_user(buf, &send_data_user, sizeof(send_data_user)) != 0) {
                result = (ssize_t)-EFAULT;
            } else {
                result = (ssize_t)count;
            }
        } else {
            printk(KERN_INFO "hifs_comm: [INODE] The command queue is empty, waiting for data to transfer...\n");
            result = (ssize_t)-EFAULT;
        }
    } else {
        result = (ssize_t)-EFAULT;
    }

    can_write = true;
    //wake up the waitqueue
    wake_up(&waitqueue);

    mutex_unlock(&inode_mutex);
    return (ssize_t)result;
}

ssize_t hi_comm_block_device_read(struct file *filep, char *buf, size_t count, loff_t *offset) {
    // Write out to user space.
    ssize_t result = 0;

    struct hifs_blocks_user {
        char block[HIFS_DEFAULT_BLOCK_SIZE];
        int block_size;
        int count;
    };

    struct hifs_blocks *send_data = kmalloc(sizeof(struct hifs_blocks_user), GFP_KERNEL);
    struct hifs_blocks_user *send_data_user = kmalloc(sizeof(struct hifs_blocks_user), GFP_KERNEL);
    if (!list_empty(&shared_block_outgoing_lst)) { 
        send_data = list_last_entry(&shared_block_outgoing_lst, struct hifs_blocks, hifs_block_list);
        if (send_data != NULL) {
            //We don't want to put a 4k block on the stack, so we'll use kernel mem w/ kmalloc.

            list_del(&send_data->hifs_block_list);

            for (int lock = 0; lock < 100; lock++){
                if (mutex_trylock(&block_mutex)) {
                    break;
                } else {
                    printk(KERN_INFO "hifs_comm: [Block] Another process is accessing the device. Waiting...\n");
                    msleep(10);
                }
                if (lock == 100) { return -EBUSY; }
            }

            strncpy(send_data_user->block, send_data->block, send_data->block_size);
            send_data_user->block_size = send_data->block_size;
            send_data_user->count = send_data->count;
            if (copy_to_user(buf, &send_data_user, sizeof(send_data_user)) != 0) {
                result = (ssize_t)-EFAULT;
            } else {
                result = (ssize_t)count;
            }
            kfree(send_data_user);
        } else {
            printk(KERN_INFO "hifs_comm: [CMD] The command queue is empty, waiting for data to transfer...\n");
            result = (ssize_t)-EFAULT;
        }
    } else {
        result = (ssize_t)-EFAULT;
    }

    can_write = true;
    //wake up the waitqueue
    wake_up(&waitqueue);

    mutex_unlock(&block_mutex);
    return (ssize_t)result;
}

ssize_t hi_comm_cmd_device_read(struct file *filep, char *buf, size_t count, loff_t *offset) {
    // Write out to user space.
    int lock;
    ssize_t result = 0;
    struct hifs_cmds_user {
        char cmd[HIFS_MAX_CMD_SIZE];  // MAX_CMD_SIZE is the maximum size of a command
        int count;
    };
    struct hifs_cmds *send_data = NULL;
    struct hifs_cmds_user send_data_user;
    if (!list_empty(&shared_cmd_outgoing_lst)) { 
        send_data = list_first_entry(&shared_cmd_outgoing_lst, struct hifs_cmds, hifs_cmd_list);
        if (send_data) {
            list_del(&send_data->hifs_cmd_list);
        } else {
            printk(KERN_INFO "hifs_comm: [CMD] send_data is empty, dropping out to process next queue message\n");
            return -EFAULT;
        }

        printk(KERN_INFO "hifs_comm: [CMD] Transferring command [%s] with [%lu] characters\n", send_data->cmd, count);
        for (lock = 0; lock < 100; lock++){
            if (!mutex_trylock(&cmd_mutex)) {
                break;
            } else {
                printk(KERN_INFO "hifs_comm: [CMD] Another process is accessing the device. Waiting...\n");
                msleep(10);
            }
        }
        if (lock > 99) { 
            mutex_unlock(&cmd_mutex);
            kfree(send_data);
            return -EBUSY; 
        }

        strncpy(send_data_user.cmd, send_data->cmd, HIFS_MAX_CMD_SIZE);
        send_data_user.count = send_data->count;
        if (copy_to_user(buf, &send_data_user, sizeof(send_data_user)) != 0) {
            result = -EFAULT;
        } else {
            result = count;
        }
        can_write = true;
        //wake up the waitqueue
        wake_up(&waitqueue);

        mutex_unlock(&cmd_mutex);
    } else {
        result = -EFAULT;
    }
    kfree(send_data);
    return result;
}

ssize_t hi_comm_inode_device_write(struct file *filep, const char *buffer, size_t count, loff_t *offset) {
    // Read from user space
    ssize_t result = 0;

    if (copy_from_user(shared_inode_incoming, buffer, min(sizeof(*shared_inode_incoming), count))) {
        // handle error
        return -EFAULT;
    }

    result = min(sizeof(*shared_inode_incoming), count);
    list_add(&shared_inode_incoming->hifs_inode_list, &shared_inode_incoming_lst);

    can_read = true;
    //wake up the waitqueue
    wake_up(&waitqueue);

    return result;
}

ssize_t hi_comm_block_device_write(struct file *filep, const char *buffer, size_t count, loff_t *offset) {
    // Read from user space
    ssize_t result = 0;

    if (copy_from_user(shared_block_incoming, buffer, min(sizeof(*shared_block_incoming), count))) {
        return -EFAULT;
    }

    result = min(sizeof(*shared_block_incoming), count);
    list_add(&shared_block_incoming->hifs_block_list, &shared_block_incoming_lst);

    can_read = true;
    //wake up the waitqueue
    wake_up(&waitqueue);

    return result;
}

ssize_t hi_comm_cmd_device_write(struct file *filep, const char *buffer, size_t count, loff_t *offset) {
    // Read from user space
    ssize_t result = 0;

    if (copy_from_user(shared_cmd_incoming, buffer, min(sizeof(*shared_cmd_incoming), count))) {
        return -EFAULT;
    }

    result = min(sizeof(*shared_cmd_incoming), count);
    list_add(&shared_cmd_incoming->hifs_cmd_list, &shared_cmd_incoming_lst);

    can_read = true;
    //wake up the waitqueue
    wake_up(&waitqueue);

    return result;
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