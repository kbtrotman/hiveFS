/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */
#include "hifs_shared_defs.h"
#include "hifs.h"


// Globals
extern int u_major, k_major, i_major, b_major, c_major;
wait_queue_head_t thread_wq;
wait_queue_head_t waitqueue;
extern struct class *inode_dev_class, *block_dev_class, *cmd_dev_class;
extern atomic_t kern_atomic_variable;
extern atomic_t user_atomic_variable;
extern struct device *kern_atomic_device;
extern struct device *user_atomic_device;
extern struct class *kern_atomic_class;
extern struct class *user_atomic_class;
extern struct hifs_link hifs_kern_link;
extern struct hifs_link hifs_user_link;

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

bool can_write = false;
bool can_read  = false;

DECLARE_WAIT_QUEUE_HEAD(waitqueue);
DECLARE_WAIT_QUEUE_HEAD(thread_wq);
DEFINE_MUTEX(inode_mutex);
DEFINE_MUTEX(block_mutex);
DEFINE_MUTEX(cmd_mutex);

// Each device queue has it's own file_operations struct.
static const struct file_operations inode_fops = {
    .owner = THIS_MODULE,
    .read = hi_comm_inode_device_read,
    .write = hi_comm_inode_device_write,
    .release = hifs_comm_device_release,
    .poll = hifs_inode_device_poll,
};

static const struct file_operations block_fops = {
    .owner = THIS_MODULE,
    .read = hi_comm_block_device_read,
    .write = hi_comm_block_device_write,
    .release = hifs_comm_device_release,
    .poll = hifs_block_device_poll,
};

static const struct file_operations cmd_fops = {
    .owner = THIS_MODULE,
    .read = hi_comm_cmd_device_read,
    .write = hi_comm_cmd_device_write,
    .release = hifs_comm_device_release,
    .poll = hifs_cmd_device_poll,
};

// This 2 faops variable are atomic variable that tell both kernel/user space when the module & hi_command are active to sync comms.
struct file_operations fakops = {
    .open = v_atomic_open,   //virtual defs
    .read = k_atomic_read,
    .write = k_atomic_write,
    .release = v_atomic_release,
};

struct file_operations fauops = {
    .open = v_atomic_open,   //virtual defs
    .read = u_atomic_read,
    .write = u_atomic_write,
    .release = v_atomic_release,
};

int hifs_atomic_init(void) {
    k_major = register_chrdev(0, ATOMIC_KERN_DEVICE_NAME, &fakops);
    if (k_major < 0) {
        hifs_err("Failed to register the kernel atmomic link device\n");
        return k_major;
    }

    u_major = register_chrdev(0, ATOMIC_USER_DEVICE_NAME, &fauops);
    if (u_major < 0) {
        hifs_err("Failed to register the user atmomic link device\n");
        return u_major;
    }

    kern_atomic_class = class_create(THIS_MODULE, ATOMIC_KERN_CLASS_NAME);
    if (IS_ERR(kern_atomic_class)) {
        unregister_chrdev(k_major, ATOMIC_KERN_DEVICE_NAME);
        hifs_err("Failed to create kernel atomic class\n");
        return PTR_ERR(kern_atomic_class);
    }

    user_atomic_class = class_create(THIS_MODULE, ATOMIC_USER_CLASS_NAME);
    if (IS_ERR(user_atomic_class)) {
        unregister_chrdev(u_major, ATOMIC_USER_DEVICE_NAME);
        hifs_err("Failed to create user atomic class\n");
        return PTR_ERR(kern_atomic_class);
    }

    kern_atomic_device = device_create(kern_atomic_class, NULL, MKDEV(k_major, 0), NULL, ATOMIC_KERN_DEVICE_NAME);
    if (IS_ERR(kern_atomic_device)) {
        class_destroy(kern_atomic_class);
        unregister_chrdev(k_major, ATOMIC_KERN_DEVICE_NAME);
        hifs_err("Failed to create the final kernel atomic link device\n");
        return PTR_ERR(kern_atomic_device);
    }

    user_atomic_device = device_create(user_atomic_class, NULL, MKDEV(u_major, 0), NULL, ATOMIC_USER_DEVICE_NAME);
    if (IS_ERR(user_atomic_device)) {
        class_destroy(user_atomic_class);
        unregister_chrdev(u_major, ATOMIC_USER_DEVICE_NAME);
        hifs_err("Failed to create the final user atomic link device\n");
        return PTR_ERR(user_atomic_device);
    }

    hifs_info("Atomic link variable devices loaded in kernel\n");

    return 0;
}

void hifs_atomic_exit(void) {
    device_destroy(kern_atomic_class, MKDEV(k_major, 0));
    device_destroy(kern_atomic_class, MKDEV(u_major, 0));
    class_unregister(kern_atomic_class);
    class_unregister(user_atomic_class);
    class_destroy(kern_atomic_class);
    class_destroy(user_atomic_class);
    unregister_chrdev(k_major, ATOMIC_KERN_DEVICE_NAME);
    unregister_chrdev(u_major, ATOMIC_USER_DEVICE_NAME);

    hifs_info("Atomic link variables unloaded\n");
}

// Override functions here: These we don't need to change.
int v_atomic_open(struct inode *inodep, struct file *filep) {
    hifs_info("Atomic Device opened\n");
    return 0;
}

int v_atomic_release(struct inode *inodep, struct file *filep) {
    //pr_info("hivefs: Atomic Device closed\n");
    return 0;
}

ssize_t k_atomic_read(struct file *filep, char __user *buffer, size_t len, loff_t *offset) {
    int value = atomic_read(&kern_atomic_variable);
    if (copy_to_user(buffer, &value, sizeof(int))) {
        return -EFAULT;
    }
    return sizeof(int);
}

ssize_t k_atomic_write(struct file *filep, const char __user *buffer, size_t len, loff_t *offset) {
    int value;
    int count;
    count = sizeof(int);
    if (count != sizeof(int)) {
        return -EINVAL;
    }
    if (copy_from_user(&value, buffer, count)) {
        return -EFAULT;
    }
    atomic_set(&kern_atomic_variable, value);
    return sizeof(int);
}
// Override functions here: These we don't need to change.


ssize_t u_atomic_read(struct file *filep, char __user *buffer, size_t len, loff_t *offset) {
    int value = atomic_read(&user_atomic_variable);
    if (copy_to_user(buffer, &value, sizeof(int))) {
        return -EFAULT;
    }
    return sizeof(int);
}

ssize_t u_atomic_write(struct file *filep, const char __user *buffer, size_t len, loff_t *offset) {
    int value;
    int count;
    count = sizeof(int);
    if (count != sizeof(int)) {
        return -EINVAL;
    }
    if (copy_from_user(&value, buffer, count)) {
        return -EFAULT;
    }
    atomic_set(&user_atomic_variable, value);
    return sizeof(value);
}
// Override functions here: These we don't need to change.

// These are for the 3 devices which controls 6 queues (1 each way) for transferring 3 types of data: inode, block, & cmd.
int register_all_comm_queues(void)
{
    i_major = register_chrdev(0, DEVICE_FILE_INODE, &inode_fops);
    if (i_major < 0) {
        hifs_err("Failed to register inode & block comm queue device\n");
        return i_major;
    }

    b_major = register_chrdev(0, DEVICE_FILE_BLOCK "_block", &block_fops);
    if (b_major < 0) {
        hifs_err("Failed to register block (file) comm queue device\n");
        return b_major;
    }

    c_major = register_chrdev(0, DEVICE_FILE_CMDS, &cmd_fops);
    if (c_major < 0) {
        hifs_err("Failed to register command comm queue device\n");
        return c_major;
    }

    cmd_dev_class = class_create(THIS_MODULE, DEVICE_FILE_CMDS);
    device_create(cmd_dev_class, NULL, MKDEV(c_major, 0), NULL, DEVICE_FILE_CMDS);  
    if (IS_ERR(cmd_dev_class)) {
        hifs_err("Failed to create inode device class\n");
        goto err_class_cmds;
    }

    block_dev_class = class_create(THIS_MODULE, DEVICE_FILE_BLOCK);
    device_create(block_dev_class, NULL, MKDEV(b_major, 0), NULL, DEVICE_FILE_BLOCK); 
    if (IS_ERR(block_dev_class)) {
        hifs_err("Failed to create inode device class\n");
        goto err_class_block;
    }

    inode_dev_class = class_create(THIS_MODULE, DEVICE_FILE_INODE);
    device_create(inode_dev_class, NULL, MKDEV(i_major, 0), NULL, DEVICE_FILE_INODE);
    if (IS_ERR(inode_dev_class)) {
        hifs_err("Failed to create inode device class\n");
        goto err_class_inode;
    }

    hifs_info("Queue device created on \\dev\\%s\n", DEVICE_FILE_CMDS);
    hifs_info("Queue device created on \\dev\\%s\n", DEVICE_FILE_INODE);
    hifs_info("Queue device created on \\dev\\%s\n", DEVICE_FILE_BLOCK);

    return 0;

err_class_inode:
    unregister_chrdev(c_major, DEVICE_FILE_CMDS); // Cleanup on error
err_class_block:
    unregister_chrdev(b_major, DEVICE_FILE_BLOCK "_block"); // Cleanup on error
err_class_cmds:
    unregister_chrdev(i_major, DEVICE_FILE_INODE); // Cleanup on error
    return -EFAULT;
}

void unregister_all_comm_queues(void)
{
    device_destroy(inode_dev_class, MKDEV(i_major, 0));
    device_destroy(block_dev_class, MKDEV(b_major, 1));
    device_destroy(cmd_dev_class, MKDEV(c_major, 2));
    class_unregister(inode_dev_class);
    class_unregister(block_dev_class);
    class_unregister(cmd_dev_class);
    unregister_chrdev(i_major, DEVICE_FILE_INODE);     
    unregister_chrdev(b_major, DEVICE_FILE_BLOCK "_block");
    unregister_chrdev(c_major, DEVICE_FILE_CMDS);
    free_a_list(&shared_inode_outgoing_lst, struct hifs_inode, hifs_inode_list);
    free_a_list(&shared_block_outgoing_lst, struct hifs_blocks, hifs_block_list);
    free_a_list(&shared_cmd_outgoing_lst, struct hifs_cmds, hifs_cmd_list);
    free_a_list(&shared_inode_incoming_lst, struct hifs_inode, hifs_inode_list);
    free_a_list(&shared_block_incoming_lst, struct hifs_blocks, hifs_block_list);
    free_a_list(&shared_cmd_incoming_lst, struct hifs_cmds, hifs_cmd_list);
}

int hifs_comm_device_release(struct inode *inode, struct file *filp) {
    /* free buffer */
    return 0;
}

ssize_t hi_comm_inode_device_read(struct file *filep, char __user *buf, size_t count, loff_t *offset) {
   // A read from user space transfers to a read here....write from kernel perspective.
    ssize_t result, send_len;
    struct hifs_inode_user *send_data_user;
    struct hifs_inode *send_data = NULL; 
    result = 0;
    hifs_info("In send data queue for INODE\n");

    if (count < sizeof(struct hifs_inode_user))  { return 0; }

    if (!list_empty(&shared_inode_outgoing_lst)) {
        send_data = list_first_entry(&shared_inode_outgoing_lst, struct hifs_inode, hifs_inode_list);
        if (send_data) {
            send_data_user = hifs_parse_inode_struct(send_data);
            list_del(&send_data->hifs_inode_list);
            kfree(send_data);
        }
        send_data = NULL;
    } else {
        hifs_info("[INODE] queue is empty, dropping out to process next queue\n");
        return 0;
    }
    if (send_data_user == NULL) { return 0; }  // Queue is empty, so let's drop out.
    send_len = sizeof(struct hifs_inode_user) + 1;

    // Obtain mutex lock
    if (mutex_lock_interruptible(&inode_mutex)) {
        hifs_info("Failed to lock mutex, interrupted by signal.\n");
        return 0; // the system call was interrupted by a signal
    }

    hifs_info("[INODE] Copying command data [%s] with [%ld] characters to buffer size [%ld]\n", send_data_user->i_name, send_len, count);
    if (copy_to_user(buf, send_data_user, send_len) != 0) {
        result = -EFAULT;
    } else {
        result = sizeof(send_data_user);
    }

    mutex_unlock(&inode_mutex);    // Unlock mutex on char device right after send.
    can_write = true;
    wake_up(&waitqueue);
    if (send_data_user) { kfree(send_data_user); }
    send_data_user = NULL;
    return result;
}

ssize_t hi_comm_block_device_read(struct file *filep, char __user *buf, size_t count, loff_t *offset) {
   // A read from user space transfers to a read here....write from kernel perspective.
    ssize_t result, send_len;
    struct hifs_blocks_user *send_data_user;
    struct hifs_blocks *send_data = NULL; 
    result = 0;
    hifs_info("In send data queue for BLOCK\n");

    if (count < sizeof(struct hifs_blocks_user))  { return 0; }

    if (!list_empty(&shared_block_outgoing_lst)) {
        send_data = list_first_entry(&shared_block_outgoing_lst, struct hifs_blocks, hifs_block_list);
        if (send_data) {
            send_data_user = hifs_parse_block_struct(send_data);
            list_del(&send_data->hifs_block_list);
            kfree(send_data);
        }
        send_data = NULL;
    } else {
        hifs_info("[BLOCK] queue is empty, dropping out to process next queue\n");
        return 0;
    }
    if (send_data_user == NULL) { return 0; }  // Queue is empty, so let's drop out.
    send_len = sizeof(struct hifs_blocks_user) + 1;

    // Obtain mutex lock
    if (mutex_lock_interruptible(&block_mutex)) {
        hifs_info("Failed to lock mutex, interrupted by signal.\n");
        return 0; // the system call was interrupted by a signal
    }

    hifs_info("[BLOCK] Copying command data [%s] with [%ld] characters to buffer size [%ld]\n", send_data_user->block, send_len, count);
    if (copy_to_user(buf, send_data_user, send_len) != 0) {
        result = -EFAULT;
    } else {
        result = sizeof(send_data_user);
    }

    mutex_unlock(&block_mutex);    // Unlock mutex on char device right after send.
    can_write = true;
    wake_up(&waitqueue);
    if (send_data_user) { kfree(send_data_user); }
    send_data_user = NULL;
    return result;
}

ssize_t hi_comm_cmd_device_read(struct file *filep, char __user *buf, size_t count, loff_t *offset) {
    // A read from user space transfers to a read here....write from kernel perspective.
    ssize_t result, send_len;
    struct hifs_cmds_user *send_data_user;
    struct hifs_cmds *send_data = NULL; 
    result = 0;
    hifs_info("In send data queue for CMD\n");

    if (count < sizeof(struct hifs_cmds_user))  { return 0; }

    if (!list_empty(&shared_cmd_outgoing_lst)) {
        send_data = list_first_entry(&shared_cmd_outgoing_lst, struct hifs_cmds, hifs_cmd_list);
        if (send_data) { 
            if (send_data->count >= sizeof(struct hifs_cmds_user) + 1) { send_data->count = sizeof(struct hifs_cmds_user) + 1; }
            send_data_user = hifs_parse_cmd_struct(send_data);
            list_del(&send_data->hifs_cmd_list);
            kfree(send_data);
        }
        send_data = NULL;
    } else {
        hifs_info("[CMD] queue is empty, dropping out to process next queue\n");
        return 0;
    }
    if (send_data_user == NULL) { return 0; }  // Queue is empty, so let's drop out.
    send_len = sizeof(struct hifs_cmds_user) + 1;

    // Obtain mutex lock
    if (mutex_lock_interruptible(&cmd_mutex)) {
        hifs_info("Failed to lock mutex, interrupted by signal.\n");
        return 0; // the system call was interrupted by a signal
    }

    hifs_info("[CMD] Copying command data [%s] with [%ld] characters to buffer size [%ld]\n", send_data_user->cmd, send_len, count);
    if (copy_to_user(buf, send_data_user, send_len) != 0) {
        result = -EFAULT;
    } else {
        result = sizeof(send_data_user);
    }

    mutex_unlock(&cmd_mutex);    // Unlock mutex on char device right after send.
    can_write = true;
    wake_up(&waitqueue);
    if (send_data_user) { kfree(send_data_user); }
    send_data_user = NULL;
    return result;
}

struct hifs_cmds_user *hifs_parse_cmd_struct( struct hifs_cmds *send_data)
{
    struct hifs_cmds_user *user_data = kmalloc(sizeof(struct hifs_cmds_user) + 1, GFP_KERNEL);

    if (!user_data) { return NULL; }
    //snprintf(user_data, HIFS_MAX_CMD_SIZE + sizeof(int) + 1, "%d!&%s!&", send_data->count, send_data->cmd);
    user_data->count = send_data->count;
    strlcpy(user_data->cmd, send_data->cmd, HIFS_MAX_CMD_SIZE);
    return user_data;
}

struct hifs_inode_user *hifs_parse_inode_struct( struct hifs_inode *send_data)
{
    struct hifs_inode_user *user_data = kmalloc(sizeof(struct hifs_inode_user) + 1, GFP_KERNEL);

    if (!user_data) { return NULL; }
    //snprintf(user_data, sizeof(struct hifs_inode) + 1, "%lld!&%d!&%d!&%d!&%s!&", send_data->i_ino, send_data->i_size, send_data->i_bytes, send_data->i_blocks, send_data->i_name);
    user_data->i_ino = send_data->i_ino;
    user_data->i_size = send_data->i_size;
    user_data->i_bytes = send_data->i_bytes;
    user_data->i_blocks = send_data->i_blocks;
    strlcpy(user_data->i_name, send_data->i_name, HIFS_MAX_NAME_SIZE);
    return user_data;
}

struct hifs_blocks_user *hifs_parse_block_struct( struct hifs_blocks *send_data)
{
    struct hifs_blocks_user *user_data = kmalloc(sizeof(struct hifs_blocks_user) + 1, GFP_KERNEL);

    if (!user_data) { return NULL; }
    //snprintf(user_data, HIFS_DEFAULT_BLOCK_SIZE + sizeof(int) + 1, "%d!&%d!&%s!&", send_data->block_size, send_data->count, send_data->block);
    user_data->count = send_data->count;
    user_data->block_size = send_data->block_size;
    strlcpy(user_data->block, send_data->block, HIFS_DEFAULT_BLOCK_SIZE);
    return user_data;
}

ssize_t hi_comm_inode_device_write(struct file *filep, const char  __user *buffer, size_t count, loff_t *offset) {
    // Write in user space to here in kernel
    ssize_t result = 0;
    hifs_info("In [write] for [INODE]\n");
    //New node incoming, so allocate it, copy data to it, and then put it on the queue and NULL the node.
    shared_inode_incoming = kmalloc(sizeof(*shared_inode_incoming), GFP_KERNEL);

    if (copy_from_user(shared_inode_incoming, buffer, min(sizeof(*shared_inode_incoming), count))) {
        // handle error
        return -EFAULT;
    }

    result = min(sizeof(*shared_inode_incoming), count);
    list_add(&shared_inode_incoming->hifs_inode_list, &shared_inode_incoming_lst);
    hifs_info("Added new Inode to the kernel incoming queue")
    can_read = true;
    //wake up the waitqueue
    wake_up(&waitqueue);
    shared_inode_incoming = NULL;

    return result;
}

ssize_t hi_comm_block_device_write(struct file *filep, const char __user *buffer, size_t count, loff_t *offset) {
    // Write in user space to here in kernel
    ssize_t result = 0;
    hifs_info("In [write] for [BLOCK]\n");
    //New node incoming, so allocate it, copy data to it, and then put it on the queue and NULL the node.
    shared_block_incoming = kmalloc(sizeof(*shared_block_incoming), GFP_KERNEL);

    if (copy_from_user(shared_block_incoming, buffer, min(sizeof(*shared_block_incoming), count))) {
        return -EFAULT;
    }

    result = min(sizeof(*shared_block_incoming), count);
    list_add(&shared_block_incoming->hifs_block_list, &shared_block_incoming_lst);
    hifs_info("Added new Block to the kernel incoming queue")
    can_read = true;
    //wake up the waitqueue
    wake_up(&waitqueue);
    shared_block_incoming = NULL;

    return result;
}

ssize_t hi_comm_cmd_device_write(struct file *filep, const char __user *buffer, size_t count, loff_t *offset) {
    // Write in user space to here in kernel
    ssize_t result = 0;
    hifs_info("In [write] for [CMD]\n");
    //New node incoming, so allocate it, copy data to it, and then put it on the queue and NULL the node.
    shared_cmd_incoming = kmalloc(sizeof(*shared_cmd_incoming), GFP_KERNEL);

    if (copy_from_user(shared_cmd_incoming, buffer, min(sizeof(*shared_cmd_incoming), count))) {
        return -EFAULT;
    }

    result = min(sizeof(*shared_cmd_incoming), count);
    list_add(&shared_cmd_incoming->hifs_cmd_list, &shared_cmd_incoming_lst);
    hifs_info("Added new Command to the kernel incoming queue")
    can_read = true;
    //wake up the waitqueue
    wake_up(&waitqueue);
    shared_cmd_incoming = NULL;

    return result;
}

__poll_t hifs_inode_device_poll (struct file *filp, poll_table *wait) {
    __poll_t mask = 0;
    poll_wait(filp, &waitqueue, wait);
    hifs_info("Polling for INODE\n");
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
    hifs_info("Polling for BLOCK\n");
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
    hifs_info("Polling for CMD\n");

    if( can_read )
    {
        can_read = false;
        mask |= ( POLLIN );
    }

    if( can_write )
    {
        can_write = false;
        mask |= ( POLLOUT | POLLWRNORM );
    }

    // Go through Queues

    return mask;
}