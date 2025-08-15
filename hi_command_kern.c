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
