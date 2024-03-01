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
extern struct hifs_inode *shared_inode;
extern char *shared_block;;
extern char *shared_cmd;
char buffer[4096];

void hifs_create_test_inode(void) {

    struct hifs_inode first_inode = {
        .i_mode = S_IFREG | 0644,
        .i_uid = 000001,
        .i_gid = 010101,
        .i_blocks = 99,
        .i_bytes = 512,
        .i_size = 512,
        .i_ino = 1,
        .hifs_inode_list = LIST_HEAD_INIT(first_inode.hifs_inode_list)
    };

    shared_inode = &first_inode;
    // Return the inode
}

int hifs_thread_fn(void *data) {
    while (!kthread_should_stop()) {
        int value;

        value = atomic_read(&my_atomic_variable);
        if ( value == HIFS_Q_PROTO_UNUSED) {
            atomic_set(&my_atomic_variable, HIFS_Q_PROTO_KERNEL_LOCK);;
            // Call our queue management function to write data to the queue here
            hifs_queue_send(buffer);
        } else if (value == HIFS_Q_PROTO_USER_WO_KERNEL) {
            hifs_queue_recv();
            atomic_set(&my_atomic_variable, HIFS_Q_PROTO_UNUSED);
            // Call our queue management function to recieve data from the queue here
        } else if (value == HIFS_Q_PROTO_ACK_LINK_UP || 
                    value == HIFS_Q_PROTO_ACK_LINK_KERN || 
                    value == HIFS_Q_PROTO_ACK_LINK_USER) {
            // Call to complete a new or a temp aborted link_up here
            pr_info("hivefs_comm: Waiting on link up. Re-trying...\n");
            hifs_comm_link_init_change();
        }
        msleep(5);  // Sleep for 5 ms
    }
    return 0;
}

int hifs_queue_send(char *buf) {
    // Send data to the queue here

    atomic_set(&my_atomic_variable, HIFS_Q_PROTO_KERNEL_WO_USER);
    return 0;
}

int hifs_queue_recv(void) {
    // Recieve data from the queue here

    int fd;

    atomic_set(&my_atomic_variable, HIFS_Q_PROTO_UNUSED);
    return 0;
}


int hifs_comm_rcv_inode( void )
{
    char *iname;
    
    // Allocate memory for iname
    iname = kzalloc(50, GFP_KERNEL);

    // READ the inode from the shared queue here......

    if (iname == NULL) {
        pr_info(KERN_ERR "hivefs: Error allocating memory for iname.\n");
        return -ENOMEM;
    }

    // Check if the acknowledgement attribute is present
    if (1) {

        pr_info(KERN_INFO "hivefs-comm: Received INODE Command.\n");
        pr_info(KERN_INFO "hivefs-comm: i_name: %s\n", iname);
        kfree(iname); // Free the allocated memory
        return 0;
    } else {
        pr_info(KERN_ERR "hivefs-comm: Error processing comm INODE Command packet.\n");
        kfree(iname); // Free the allocated memory
        return -1;
    }   
}

int hifs_comm_link_init_change( void )
{
    int value = HIFS_Q_PROTO_UNUSED;
    value = atomic_read(&my_atomic_variable);

    if (value == HIFS_Q_PROTO_UNUSED) {
        atomic_set(&my_atomic_variable, HIFS_Q_PROTO_KERNEL_LOCK);
        hifs_comm_link_up();
        atomic_set(&my_atomic_variable, HIFS_Q_PROTO_ACK_LINK_KERN);
        hifs_wait_on_link();
    } else if (value == HIFS_Q_PROTO_ACK_LINK_UP) {
        hifs_comm_link_up_completed();
    } else if (value ==HIFS_Q_PROTO_ACK_LINK_KERN) {
        hifs_wait_on_link();
    } else if (value == HIFS_Q_PROTO_ACK_LINK_USER) {
        atomic_set(&my_atomic_variable, HIFS_Q_PROTO_KERNEL_LOCK);
        hifs_kern_link.remote_state = HIFS_COMM_LINK_UP;
        hifs_comm_link_up();
        atomic_set(&my_atomic_variable, HIFS_Q_PROTO_UNUSED);
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
}

void hifs_comm_link_up_completed (void)
{
    // Link up to hi_command completed.
    hifs_kern_link.remote_state = HIFS_COMM_LINK_UP;
    atomic_set(&my_atomic_variable, HIFS_Q_PROTO_UNUSED);
    pr_info("hivefs_comm: Link to hi_command completed at %ld seconds after hifs start.\n", (GET_TIME() - hifs_kern_link.clockstart));
}

void hifs_wait_on_link(void)
{
    for (int i = 0; i < 100; i++) {
        if (atomic_read(&my_atomic_variable) == HIFS_Q_PROTO_ACK_LINK_UP) {
            hifs_comm_link_up_completed();
            break;
        }
        msleep(10);
    }
}