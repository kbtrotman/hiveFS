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
struct hifs_link hifs_kern_link;
struct hifs_link hifs_user_link;
struct task_struct *task;


int hifs_create_test_inode(void) {
    struct hifs_inode *inode_data;
    struct hifs_inode_user *user_data;

    inode_data = kmalloc(sizeof(*inode_data), GFP_KERNEL);
    if (!inode_data) return -ENOMEM;

    inode_data->i_name = "test_inode";
    inode_data->i_mode = S_IFREG | 0644;
    inode_data->i_uid = 000001;
    inode_data->i_gid = 010101;
    inode_data->i_blocks = 99;
    inode_data->i_bytes = 512;
    inode_data->i_size = 512;
    inode_data->i_ino = 1;

    user_data = hifs_parse_inode_struct(inode_data);
    if (!user_data) {
        kfree(inode_data);
        return -ENOMEM;
    }

    // Push to ringbuffer
    void *buf = bpf_ringbuf_output(inode_ringbuf, user_data, sizeof(*user_data), 0);
    if (!buf) {
        hifs_err("Failed to push inode data to ringbuffer\n");
        kfree(user_data);
        return -EFAULT;
    }

    kfree(inode_data); // Ringbuffer owns user_data now
    return 0;
}

int hifs_thread_fn(void *data) {
    int value;

    value = hifs_create_test_inode();
    if (value < 0) {
        hifs_warning("Failed to create test inode\n");
        return value;
    }

    value = hifs_comm_set_program_up(HIFS_COMM_PROGRAM_KERN_MOD);
    value = hifs_comm_set_program_up(HIFS_COMM_PROGRAM_USER_HICOMM);

    while (!kthread_should_stop()) {
        value = atomic_read(&user_atomic_variable);
        if (value == HIFS_COMM_LINK_DOWN) {
            hifs_info("User link is down. Waiting for hi_command to come up...\n");
        } else {
            // Push data to ringbuffers periodically
            hifs_create_test_inode();
            hifs_info("Pushed test inode to ringbuffer\n");
        }

        // Sleep or wait for user signal
        schedule_timeout_interruptible(msecs_to_jiffies(5000));
    }

    return 0;
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
            hifs_notice("user link up'd at %ld seconds after hifs start, waiting on hi_command.\n", (GET_TIME() - hifs_user_link.clockstart));
        } else {
            hifs_user_link.last_state = hifs_user_link.state;
            hifs_user_link.state = HIFS_COMM_LINK_DOWN;
            hifs_user_link.last_check = 0;
            value = 1;           
        }
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
