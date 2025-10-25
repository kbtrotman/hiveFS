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

#include <linux/kfifo.h>
#include <linux/miscdevice.h>
#include <linux/poll.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/wait.h>

static DEFINE_KFIFO(hifs_cmd_fifo, struct hifs_cmds, HIFS_CMD_RING_CAPACITY);
static DEFINE_KFIFO(hifs_data_fifo, struct hifs_data_frame, HIFS_DATA_RING_CAPACITY);
static DEFINE_KFIFO(hifs_cmd_in_fifo, struct hifs_cmds, HIFS_CMD_RING_CAPACITY);
static DEFINE_KFIFO(hifs_data_in_fifo, struct hifs_data_frame, HIFS_DATA_RING_CAPACITY);

static DEFINE_SPINLOCK(hifs_cmd_lock);
static DEFINE_SPINLOCK(hifs_data_lock);
static DEFINE_SPINLOCK(hifs_cmd_in_lock);
static DEFINE_SPINLOCK(hifs_data_in_lock);

static DECLARE_WAIT_QUEUE_HEAD(hifs_cmd_wait);
static DECLARE_WAIT_QUEUE_HEAD(hifs_data_wait);
static DECLARE_WAIT_QUEUE_HEAD(hifs_cmd_in_wait);
static DECLARE_WAIT_QUEUE_HEAD(hifs_data_in_wait);

static unsigned int hifs_cmd_fifo_in_len(void)
{
    return hifs_fifo_len_locked(&hifs_cmd_in_fifo, &hifs_cmd_in_lock);
}

static unsigned int hifs_data_fifo_in_len(void)
{
    return hifs_fifo_len_locked(&hifs_data_in_fifo, &hifs_data_in_lock);
}

static unsigned int hifs_cmd_fifo_out_len(void)
{
    return hifs_fifo_len_locked(&hifs_cmd_fifo, &hifs_cmd_lock);
}

static unsigned int hifs_data_fifo_out_len(void)
{
    return hifs_fifo_len_locked(&hifs_data_fifo, &hifs_data_lock);
}

int hifs_cmd_fifo_out_push(const struct hifs_cmds *msg)
{
    return hifs_fifo_push_locked(&hifs_cmd_fifo, &hifs_cmd_lock, &hifs_cmd_wait, msg);
}

int hifs_data_fifo_out_push_buf(const void *buf, size_t len)
{
    struct hifs_data_frame *frame;
    int ret;
    if (!buf || len == 0 || len > HIFS_DATA_MAX)
        return -EINVAL;
    frame = kmalloc(sizeof(*frame), GFP_KERNEL);
    if (!frame)
        return -ENOMEM;
    memset(frame, 0, sizeof(*frame));
    frame->len = len;
    memcpy(frame->data, buf, len);
    ret = hifs_fifo_push_locked(&hifs_data_fifo, &hifs_data_lock, &hifs_data_wait, frame);
    kfree(frame);
    return ret;
}

int hifs_cmd_fifo_in_push(const struct hifs_cmds *msg)
{
    return hifs_fifo_push_locked(&hifs_cmd_in_fifo, &hifs_cmd_in_lock, &hifs_cmd_in_wait, msg);
}

int hifs_data_fifo_in_push_buf(const void *buf, size_t len)
{
    struct hifs_data_frame *frame;
    int ret;
    if (!buf || len == 0 || len > HIFS_DATA_MAX)
        return -EINVAL;
    frame = kmalloc(sizeof(*frame), GFP_KERNEL);
    if (!frame)
        return -ENOMEM;
    memset(frame, 0, sizeof(*frame));
    frame->len = len;
    memcpy(frame->data, buf, len);
    ret = hifs_fifo_push_locked(&hifs_data_in_fifo, &hifs_data_in_lock, &hifs_data_in_wait, frame);
    kfree(frame);
    return ret;
}

// Helper. Forget the structure and just push a quick command.Extensible protocol.
int hifs_cmd_fifo_out_push_cstr(const char *command)
{
	struct hifs_cmds msg;
	size_t len;

	if (!command)
		return -EINVAL;

	memset(&msg, 0, sizeof(msg));
	len = strnlen(command, HIFS_MAX_CMD_SIZE - 1);
	msg.count = len;
	strscpy(msg.cmd, command, sizeof(msg.cmd));

    return hifs_cmd_fifo_out_push(&msg);
}

/* Helper removed: variable-length data FIFO supersedes inode-specific path. */

static int hifs_cmd_fifo_out_pop(struct hifs_cmds *msg, bool nonblock)
{
    return hifs_fifo_pop_locked(&hifs_cmd_fifo, &hifs_cmd_lock, hifs_cmd_wait, msg, nonblock);
}

static int hifs_data_fifo_out_pop(struct hifs_data_frame *frame, bool nonblock)
{
    return hifs_fifo_pop_locked(&hifs_data_fifo, &hifs_data_lock, hifs_data_wait, frame, nonblock);
}

int hifs_cmd_fifo_in_pop(struct hifs_cmds *msg, bool nonblock)
{
    return hifs_fifo_pop_locked(&hifs_cmd_in_fifo, &hifs_cmd_in_lock, hifs_cmd_in_wait, msg, nonblock);
}

int hifs_data_fifo_in_pop(struct hifs_data_frame *frame, bool nonblock)
{
    return hifs_fifo_pop_locked(&hifs_data_in_fifo, &hifs_data_in_lock, hifs_data_in_wait, frame, nonblock);
}

static long hifs_comm_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	void __user *user_ptr = (void __user *)arg;
	bool nonblock = file->f_flags & O_NONBLOCK;

	switch (cmd) {
	case HIFS_IOCTL_CMD_DEQUEUE: {
		struct hifs_cmds msg;
		int ret = hifs_cmd_fifo_out_pop(&msg, nonblock);

		if (ret)
			return ret;

		if (copy_to_user(user_ptr, &msg, sizeof(msg)))
			return -EFAULT;

		return 0;
	}
	case HIFS_IOCTL_DATA_DEQUEUE: {
		struct hifs_data_frame frame;
		int ret = hifs_data_fifo_out_pop(&frame, nonblock);

		if (ret)
			return ret;

		if (copy_to_user(user_ptr, &frame, sizeof(frame)))
			return -EFAULT;

		return 0;
	}
	case HIFS_IOCTL_STATUS: {
	struct hifs_comm_status status = {
		.cmd_available = hifs_cmd_fifo_out_len(),
		.inode_available = hifs_data_fifo_out_len(),
		.cmd_pending = hifs_cmd_fifo_in_len(),
		.inode_pending = hifs_data_fifo_in_len(),
		};

		if (copy_to_user(user_ptr, &status, sizeof(status)))
			return -EFAULT;

		return 0;
	}
	case HIFS_IOCTL_CMD_ENQUEUE: {
		struct hifs_cmds msg;

		if (copy_from_user(&msg, user_ptr, sizeof(msg)))
			return -EFAULT;

        return hifs_cmd_fifo_in_push(&msg);
	}
	case HIFS_IOCTL_DATA_ENQUEUE: {
		struct hifs_data_frame frame;

		if (copy_from_user(&frame, user_ptr, sizeof(frame)))
			return -EFAULT;

        return hifs_data_fifo_in_push_buf(frame.data, frame.len);
	}
	default:
		return -ENOTTY;
	}
}

static __poll_t hifs_fifo_poll(struct file *file, poll_table *wait)
{
    __poll_t mask = 0;

    poll_wait(file, &hifs_cmd_wait, wait);
    poll_wait(file, &hifs_data_wait, wait);

	if (hifs_cmd_fifo_out_len())
		mask |= POLLIN | POLLRDNORM;

    if (hifs_data_fifo_out_len())
        mask |= POLLIN | POLLRDBAND;

    if (!kfifo_is_full(&hifs_cmd_in_fifo) || !kfifo_is_full(&hifs_data_in_fifo))
        mask |= POLLOUT | POLLWRNORM;

	return mask;
}

static const struct file_operations hifs_comm_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = hifs_comm_ioctl,
	.compat_ioctl = hifs_comm_ioctl,
	.poll = hifs_fifo_poll,
	.llseek = noop_llseek,
};

static struct miscdevice hifs_comm_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = HIFS_COMM_DEVICE_NAME,
	.fops = &hifs_comm_fops,
};

int hifs_fifo_init(void)
{
	int ret;

	INIT_KFIFO(hifs_cmd_fifo);
    INIT_KFIFO(hifs_data_fifo);
	INIT_KFIFO(hifs_cmd_in_fifo);
    INIT_KFIFO(hifs_data_in_fifo);

    ret = misc_register(&hifs_comm_device);
    if (ret)
        hifs_err("Failed to register comm device\n");

    return ret;
}

void hifs_fifo_exit(void)
{
	unsigned long flags;

	misc_deregister(&hifs_comm_device);

	spin_lock_irqsave(&hifs_cmd_lock, flags);
	kfifo_reset(&hifs_cmd_fifo);
	spin_unlock_irqrestore(&hifs_cmd_lock, flags);

    spin_lock_irqsave(&hifs_data_lock, flags);
    kfifo_reset(&hifs_data_fifo);
    spin_unlock_irqrestore(&hifs_data_lock, flags);

	spin_lock_irqsave(&hifs_cmd_in_lock, flags);
	kfifo_reset(&hifs_cmd_in_fifo);
	spin_unlock_irqrestore(&hifs_cmd_in_lock, flags);

    spin_lock_irqsave(&hifs_data_in_lock, flags);
    kfifo_reset(&hifs_data_in_fifo);
    spin_unlock_irqrestore(&hifs_data_in_lock, flags);
}
