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
struct hifs_link hifs_kern_link = {HIFS_COMM_LINK_DOWN, 0, 0, 0};
int u_major, k_major, i_major, b_major, c_major;
bool new_device_data = false;
struct class *inode_dev_class, *block_dev_class, *cmd_dev_class;
atomic_t kern_atomic_variable = ATOMIC_INIT(0);
atomic_t user_atomic_variable = ATOMIC_INIT(0);
struct device *kern_atomic_device = NULL;
struct device *user_atomic_device = NULL;
struct class *kern_atomic_class;
struct class *user_atomic_class;
struct hifs_link hifs_kern_link;
struct hifs_link hifs_user_link;
struct task_struct *task;

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
extern wait_queue_head_t thread_wq;

int hifs_create_test_inode(void) {

    shared_inode_incoming = kmalloc(sizeof(*shared_inode_incoming), GFP_KERNEL);
    shared_cmd_incoming = kmalloc(sizeof(*shared_cmd_incoming), GFP_KERNEL);
    shared_block_incoming = kmalloc(sizeof(*shared_block_incoming), GFP_KERNEL);

    if (!shared_inode_incoming || !shared_cmd_incoming || !shared_block_incoming) {
        if (shared_cmd_incoming) { kfree(shared_cmd_incoming); }
        if (shared_inode_incoming) { kfree(shared_inode_incoming); }
        if (shared_block_incoming) { kfree(shared_block_incoming); }
        return -ENOMEM;
    }

    shared_inode_outgoing = kmalloc(sizeof(*shared_inode_incoming), GFP_KERNEL);
    shared_cmd_outgoing = kmalloc(sizeof(*shared_cmd_incoming), GFP_KERNEL);
    shared_block_outgoing = kmalloc(sizeof(*shared_block_incoming), GFP_KERNEL);   

    if (!shared_inode_outgoing || !shared_cmd_outgoing || !shared_block_outgoing) {
        if (shared_cmd_outgoing) { kfree(shared_cmd_outgoing); }
        if (shared_inode_outgoing) { kfree(shared_inode_outgoing); }
        if (shared_block_outgoing) { kfree(shared_block_outgoing); }
        return -ENOMEM;
    }

    *shared_inode_outgoing = (struct hifs_inode) {
        .i_mode = S_IFREG | 0644,
        .i_uid = 000001,
        .i_gid = 010101,
        .i_blocks = 99,
        .i_bytes = 512,
        .i_size = 512,
        .i_ino = 1,
    };

    *shared_cmd_outgoing = (struct hifs_cmds){
        .cmd = HIFS_Q_PROTO_CMD_TEST,
        .count = 1,
    };

    INIT_LIST_HEAD(&shared_inode_incoming->hifs_inode_list);
    INIT_LIST_HEAD(&shared_cmd_incoming->hifs_cmd_list);
    INIT_LIST_HEAD(&shared_block_incoming->hifs_block_list);
    INIT_LIST_HEAD(&shared_inode_outgoing->hifs_inode_list);
    INIT_LIST_HEAD(&shared_cmd_outgoing->hifs_cmd_list);
    INIT_LIST_HEAD(&shared_block_outgoing->hifs_block_list);

    INIT_LIST_HEAD(&shared_inode_outgoing_lst);
    INIT_LIST_HEAD(&shared_block_outgoing_lst);
    INIT_LIST_HEAD(&shared_cmd_outgoing_lst);
    INIT_LIST_HEAD(&shared_inode_incoming_lst);
    INIT_LIST_HEAD(&shared_block_incoming_lst);
    INIT_LIST_HEAD(&shared_cmd_incoming_lst);

    list_add_tail(&shared_cmd_outgoing->hifs_cmd_list, &shared_cmd_outgoing_lst);
    list_add_tail(&shared_inode_outgoing->hifs_inode_list, &shared_inode_outgoing_lst);

    // The list memory was added to the lists, so here we NULL their references.
    shared_inode_outgoing = NULL;
    shared_cmd_outgoing = NULL;
    shared_block_outgoing = NULL;
    shared_inode_incoming = NULL;
    shared_cmd_incoming = NULL;
    shared_block_incoming = NULL;

    return 0;
}

int hifs_thread_fn(void *data) {

    int value;
    value = hifs_create_test_inode();

    if (value < 0) {
        hifs_warning("Failed to create test data\n");
        return value;
    }
    // Before going into the data management, we up our module status here.
    value = hifs_comm_set_program_up(HIFS_COMM_PROGRAM_KERN_MOD);
    value = hifs_comm_set_program_up(HIFS_COMM_PROGRAM_KERN_MOD);

    while (!kthread_should_stop()) {
        value = atomic_read(&user_atomic_variable);
        wait_event_interruptible_timeout(thread_wq, 1, msecs_to_jiffies(5000));
        if (value == HIFS_COMM_LINK_DOWN) {
            //hifs_info("user link is down. Waiting for hi_command to come up...\n");
        } else if (hifs_user_link.state == HIFS_COMM_LINK_DOWN) {
            hifs_info("Kernel link was recently up'd. Proceeding...\n");
            hifs_comm_set_program_up(HIFS_COMM_PROGRAM_USER_HICOMM);
        }
        hifs_notice("kernel link is [%d], user link is [%d], cycling...\n", hifs_kern_link.state, hifs_user_link.state);
    }

    return 0;
}

int hifs_comm_check_program_up( int program ) {
    int value;
    if (program == HIFS_COMM_PROGRAM_KERN_MOD) {
        value = atomic_read(&kern_atomic_variable);
    } else if (program == HIFS_COMM_PROGRAM_USER_HICOMM) {
        value = atomic_read(&user_atomic_variable);
    }

    return value;
}

int hifs_comm_set_program_up( int program ) {
    int value;
    if (program == HIFS_COMM_PROGRAM_KERN_MOD) {  
        atomic_set(&kern_atomic_variable, HIFS_COMM_LINK_UP);
        hifs_kern_link.last_state = hifs_kern_link.state;
        hifs_kern_link.last_check = 0;
        hifs_kern_link.state = HIFS_COMM_LINK_UP;
        value = 1;
        hifs_notice("kern link up'd at %ld seconds after hifs start, waiting on hi_command.\n", (GET_TIME() - hifs_kern_link.clockstart));
    } else if (program == HIFS_COMM_PROGRAM_USER_HICOMM) {
        //...The kernel should never set this mem location. It's owned by user space.
        value = hifs_comm_check_program_up(HIFS_COMM_PROGRAM_USER_HICOMM);
        if (value == HIFS_COMM_LINK_UP) {
            hifs_user_link.last_state = hifs_user_link.state;
            hifs_user_link.state = HIFS_COMM_LINK_UP;
            hifs_user_link.last_check = 0;
            value = 1;
        } else {
            hifs_user_link.last_state = hifs_user_link.state;
            hifs_user_link.state = HIFS_COMM_LINK_DOWN;
            hifs_user_link.last_check = 0;
            value = 1;           
        }
        hifs_notice("user link up'd at %ld seconds after hifs start, waiting on hi_command.\n", (GET_TIME() - hifs_user_link.clockstart));
    } else {
        value = 0;
    }

    return value;
}

int hifs_comm_set_program_down( int program ) {
    int value;
    if (program == HIFS_COMM_PROGRAM_KERN_MOD) {  
        atomic_set(&kern_atomic_variable, HIFS_COMM_LINK_DOWN);
        hifs_kern_link.last_state = hifs_kern_link.state;
        hifs_kern_link.last_check = 0;
        hifs_kern_link.state = HIFS_COMM_LINK_DOWN;
        value = 0;
        hifs_info("kern link down'd at %ld seconds after hifs start, waiting on hi_command.\n", (GET_TIME() - hifs_kern_link.clockstart));
    } else if (program == HIFS_COMM_PROGRAM_USER_HICOMM) {
        //...The kernel should never set this mem location. It's owned by user space.
        value = hifs_comm_check_program_up(HIFS_COMM_PROGRAM_USER_HICOMM);
        if (value == HIFS_COMM_LINK_UP) {
            // We don't want to do this! Hi_Command has to flush and shutdown the user side of the link! Do nothing.
            value = 1;
        }
        hifs_info("user link down attempted and rejected at %ld seconds after hifs start, waiting on hi_command.\n", (GET_TIME() - hifs_user_link.clockstart));
    } else {
        value = 0;
    }

    return value;
}

int hifs_start_queue_thread(void)
{
    // Start the new monitoring kernel thread
    task = kthread_run(hifs_thread_fn, NULL, "hive_queue_manager");
    if (IS_ERR(task)) {
        hifs_err("Failed to create the atomic variable monitoring kernel thread\n");
        return PTR_ERR(task);
    }
    hifs_info("A new kernel thread was forked to monitor/manage communications\n");
    return 0;
}

int hifs_stop_queue_thread(void)
{
    // Stop the new monitoring kernel thread
    wake_up_process(task); // Wake up the thread if it's sleeping
    if (task) { kthread_stop(task); }
    hifs_info("Shutting down hivefs queue management thread\n");
    return 0;
}
