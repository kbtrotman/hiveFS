/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

#include "hifs.h"
#include <linux/list.h>

// Globals
struct hifs_link hifs_kern_link = {HIFS_COMM_LINK_DOWN, 0, 0, 0};
struct task_struct *task;
extern atomic_t my_atomic_variable;
extern int major;
extern struct hifs_inode *shared_inode_outgoing;    // These six Doubly-Linked Lists are our
extern struct hifs_blocks *shared_block_outgoing;   // processing queues. They are sent & 
extern struct hifs_cmds *shared_cmd_outgoing;       // received thru the 3 device files known
extern struct hifs_inode *shared_inode_incoming;    // as the "queues" (to hi_command). We want
extern struct hifs_blocks *shared_block_incoming;   // to proces them fast, so they're split into
extern struct hifs_cmds *shared_cmd_incoming;       // incoming & outgoing queues here.

extern struct list_head shared_inode_outgoing_lst;    
extern struct list_head shared_block_outgoing_lst;    
extern struct list_head shared_cmd_outgoing_lst;       
extern struct list_head shared_inode_incoming_lst;    
extern struct list_head shared_block_incoming_lst;   
extern struct list_head shared_cmd_incoming_lst;  

static bool stop_thread = false;
extern char *filename;     // The filename we're currently sending/recieving to/from.
char buffer[4096];


void hifs_create_test_inode(void) {

    shared_inode_incoming = kmalloc(sizeof(*shared_inode_incoming), GFP_KERNEL);
    shared_cmd_incoming = kmalloc(sizeof(*shared_cmd_incoming), GFP_KERNEL);

    if (!shared_inode_incoming || !shared_cmd_incoming) {
        // Handle error
        //return -ENOMEM;
    }

    *shared_inode_incoming = (struct hifs_inode) {
        .i_mode = S_IFREG | 0644,
        .i_uid = 000001,
        .i_gid = 010101,
        .i_blocks = 99,
        .i_bytes = 512,
        .i_size = 512,
        .i_ino = 1,
    };

    *shared_cmd_incoming = (struct hifs_cmds){
        .cmd = HIFS_Q_PROTO_CMD_TEST,
        .count = 1,
    };

    INIT_LIST_HEAD(&shared_inode_incoming_lst);
    INIT_LIST_HEAD(&shared_cmd_incoming_lst);
    INIT_LIST_HEAD(&shared_inode_incoming->hifs_inode_list);
    INIT_LIST_HEAD(&shared_cmd_incoming->hifs_cmd_list);

}

int hifs_thread_fn(void *data) {
    while (!kthread_should_stop() && !stop_thread) {
        int value;

        hifs_create_test_inode();
        value = atomic_read(&my_atomic_variable);
        if (value == HIFS_Q_PROTO_ACK_LINK_UP || 
            value == HIFS_Q_PROTO_ACK_LINK_KERN || 
            value == HIFS_Q_PROTO_ACK_LINK_USER ||
            hifs_kern_link.state == HIFS_COMM_LINK_DOWN) 
        {
            // Call to complete a new or a temp aborted link_up here
            pr_info("hivefs_comm: Waiting on link up. Re-trying...\n");
            hifs_comm_link_init_change();
        } else {
            // Call to manage the queue contents
            hifs_manage_queue_contents();
        }
        msleep(5);  // Sleep for 5 ms
    }
    return 0;
}

int hifs_comm_link_init_change( void )
{
    int value = HIFS_Q_PROTO_UNUSED;
    value = atomic_read(&my_atomic_variable);

    if (value == HIFS_Q_PROTO_UNUSED && hifs_kern_link.state == HIFS_COMM_LINK_DOWN) {
        hifs_comm_link_up();
        atomic_set(&my_atomic_variable, HIFS_Q_PROTO_ACK_LINK_KERN);
        hifs_wait_on_link();
    } else if (value == HIFS_Q_PROTO_ACK_LINK_UP) {
        hifs_comm_link_up();
    } else if (value ==HIFS_Q_PROTO_ACK_LINK_KERN) {
        hifs_wait_on_link();
    } else if (value == HIFS_Q_PROTO_ACK_LINK_USER) {
        hifs_comm_link_up();
    }

    return 0;
}

void hifs_comm_link_up (void) 
{
    pr_info(KERN_INFO "hivefs: Received hivefs Link_Up Command.\n");
    hifs_kern_link.last_state = hifs_kern_link.state;
    hifs_kern_link.state = HIFS_COMM_LINK_UP;
    pr_info(KERN_INFO "hivefs: kern link up'd at %ld seconds after hifs start, waiting on hi_command.\n", (GET_TIME() - hifs_kern_link.clockstart));
    hifs_kern_link.last_check = 0;
    atomic_set(&my_atomic_variable, HIFS_Q_PROTO_UNUSED);
}

void hifs_wait_on_link(void)
{
    for (int i = 0; i < 100; i++) {
        if (atomic_read(&my_atomic_variable) == HIFS_Q_PROTO_ACK_LINK_UP) {
            hifs_comm_link_up();
            break;
        }
        msleep(10);
    }
}

int hifs_manage_queue_contents(void)
{
    // Check if the outgoing queue is empty. If it is, we can't do anything.
    if (list_empty(&shared_inode_outgoing_lst) && list_empty(&shared_block_outgoing_lst) && list_empty(&shared_cmd_outgoing_lst)) {
        pr_info("hivefs_comm: Outgoing queue is empty. Waiting for data.\n");
        return -1;
    } else {
        // Check if the outgoing queue has data. If it does, process it.
        if (!list_empty(&shared_inode_outgoing_lst) || !list_empty(&shared_block_outgoing_lst) || !list_empty(&shared_cmd_outgoing_lst)) {
            pr_info("hivefs_comm: Outgoing queue has data. Processing...\n");
            //hifs_process_outgoing_queue();
        }
    }

    // Check if the incoming queue is empty. If it is, we can't do anything.
    if (list_empty(&shared_inode_incoming_lst) && list_empty(&shared_block_incoming_lst) && list_empty(&shared_cmd_incoming_lst)) {
        pr_info("hivefs_comm: Incoming queue is empty. Waiting for data.\n");
        return -1;
    } else {
        // Pop data from the incoming queue and process it.
        if (!list_empty(&shared_inode_incoming_lst) || !list_empty(&shared_block_incoming_lst) || !list_empty(&shared_cmd_incoming_lst)) {
            pr_info("hivefs_comm: Incoming queue has data. Processing...\n");
            //hifs_process_incoming_queue();
        }
    }

    return 0;

}

int hifs_start_queue_thread(void)
{
    // Start the new monitoring kernel thread
    task = kthread_run(hifs_thread_fn, NULL, "hifs_thread");
    if (IS_ERR(task)) {
        pr_err("hivefs: Failed to create the atomic variable monitoring kernel thread\n");
        return PTR_ERR(task);
    }
    pr_info("hivefs: A new kernel thread was forked to monitor/manage communications\n");
    return 0;
}

int hifs_stop_queue_thread(void)
{
    // Stop the new monitoring kernel thread
    stop_thread = true;
    kthread_stop(task);
    pr_info("hivefs: Shutting down hivefs queue management thread\n");
    return 0;
}