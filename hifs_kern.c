/**************************************************************************
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 **************************************************************************/
#include "hifs_shared_defs.h"
#include "hifs.h"

struct hifs_link hifs_kern_link;
struct hifs_link hifs_user_link;
static struct task_struct *task;

int hifs_thread_fn(void *data)
{
	int ret;

    hifs_comm_link_notify_online();
	
	while (!kthread_should_stop()) {

		ret = hifs_create_test_inode();
		if (ret) {
			hifs_warning("Failed to enqueue test inode: %d\n", ret);
		} else {
			struct hifs_cmds rsp;
			struct hifs_inode inode_rsp;

			ret = hifs_cmd_fifo_in_pop(&rsp, true);
			if (!ret)
				hifs_info("Received response \"%s\" from user space\n", rsp.cmd);

			{
				struct hifs_data_frame frame;
				ret = hifs_data_fifo_in_pop(&frame, true);
				if (!ret && frame.len == sizeof(inode_rsp)) {
					memcpy(&inode_rsp, frame.data, sizeof(inode_rsp));
					hifs_info("Received inode response %llu \"%s\"\n",
					  (unsigned long long)inode_rsp.i_ino,
					  inode_rsp.i_name);
				}
			}
		}

		schedule_timeout_interruptible(msecs_to_jiffies(2000));
	}

	hifs_comm_link_notify_offline();
	return 0;
}

int hifs_start_queue_thread(void)
{
	task = kthread_run(hifs_thread_fn, NULL, "hive_queue_manager");
	if (IS_ERR(task)) {
		hifs_err("Failed to create communications thread\n");
		return PTR_ERR(task);
	}
	hifs_info("Spawned communications thread\n");
	return 0;
}

int hifs_stop_queue_thread(void)
{
	if (!task)
		return 0;

	wake_up_process(task);
	kthread_stop(task);
	task = NULL;
	hifs_info("Shutting down hivefs communications thread\n");
	return 0;
}
