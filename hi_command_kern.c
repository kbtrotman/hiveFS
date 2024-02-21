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
struct hifs_link hifs_kern_link = {HIFS_COMM_LINK_DOWN, 0, 0, 0};
struct task_struct *task;
extern atomic_t my_atomic_variable;
extern int major;
extern struct inode *shared_inode;
extern struct buffer_head *shared_block;
extern char *shared_cmd;



struct inode *create_test_inode(void) {
    struct inode *inode;

    // Allocate memory for the inode
    inode = kmalloc(sizeof(struct inode), GFP_KERNEL);
    if (!inode)
        return NULL;

    // Zero out the memory
    memset(inode, 0, sizeof(struct inode));

    // Populate inode fields with test data
    inode->i_mode = S_IFREG | 0644;  // Regular file with read and write permissions
    inode->i_uid.val = 000001;
    inode->i_gid.val = 010101;
    inode->i_blocks = 99;  // Number of 512-byte blocks
    //inode->i_atime = inode->i_mtime = inode->i_ctime = (GET_TIME());
    inode->i_bytes = 512;  // File size in bytes
    inode->i_size = 512;  // File size in bytes
    inode->i_ino = 1;  // Inode number

    return inode;
}

void destroy_test_inode(struct inode *inode) {
    iput(inode);
}

int hifs_thread_fn(void *data) {
    while (!kthread_should_stop()) {
        int value;
        value = hifs_atomic_read();
        if ( value == 0) {
            hifs_atomic_write(1);
            // Call our queue management function to write data to the queue here
            scan_queue_and_send();
        } else if (value == 4) {
            scan_queue_and_recv();
            hifs_atomic_write(0);
            // Call our queue management function to recieve data from the queue here
        } else if (value == 8 || value == 9 || value == 10) {
            // Call to complete an aborted link_up here
            pr_info("hivefs_comm: Waiting on link up. Re-trying...\n");
            hifs_comm_link_init_change();
        }
        msleep(5);  // Sleep for 5 ms
    }
    return 0;
}

int scan_queue_and_send(void) {
    // Send data to the queue here

    hifs_atomic_write(2);
    return 0;
}

int scan_queue_and_recv(void) {
    // Recieve data from the queue here


    return 0;
}


int hifs_comm_rcv_inode( void )
{
    char *iname;
    
    // Allocate memory for iname
    iname = kmalloc(50, GFP_KERNEL);

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
    int value = 0;
    value = hifs_atomic_read();
    if (value == 0) {
        hifs_atomic_write(1);
        hifs_comm_link_up();
        hifs_atomic_write(9);
        for (int i = 0; i < 100; i++) {
            if (hifs_atomic_read() == 8) {
                hifs_comm_link_up_completed();
                break;
            }
            msleep(10);
        }
    } else if (value == 8) {
        hifs_comm_link_up_completed();
    } else if (value ==9) {
        for (int i = 0; i < 100; i++) {
            if (hifs_atomic_read() == 8) {
                hifs_comm_link_up_completed();
                break;
            }
            msleep(10);
        }
    } else if (value == 10) {
        hifs_atomic_write(1);
        hifs_kern_link.remote_state = HIFS_COMM_LINK_UP;
        hifs_comm_link_up();
        hifs_atomic_write(0);
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
    hifs_atomic_write(0);
    pr_info("hivefs_comm: Link to hi_command completed at %ld seconds after hifs start.\n", (GET_TIME() - hifs_kern_link.clockstart));
}