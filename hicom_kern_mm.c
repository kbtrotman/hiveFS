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

static DEFINE_KFIFO(hifs_cmd_fifo, struct hifs_cmds_user, HIFS_CMD_RING_CAPACITY);
static DEFINE_KFIFO(hifs_inode_fifo, struct hifs_inode_user, HIFS_INODE_RING_CAPACITY);
static DEFINE_KFIFO(hifs_cmd_in_fifo, struct hifs_cmds_user, HIFS_CMD_RING_CAPACITY);
static DEFINE_KFIFO(hifs_inode_in_fifo, struct hifs_inode_user, HIFS_INODE_RING_CAPACITY);

static DEFINE_SPINLOCK(hifs_cmd_lock);
static DEFINE_SPINLOCK(hifs_inode_lock);
static DEFINE_SPINLOCK(hifs_cmd_in_lock);
static DEFINE_SPINLOCK(hifs_inode_in_lock);

static DECLARE_WAIT_QUEUE_HEAD(hifs_cmd_wait);
static DECLARE_WAIT_QUEUE_HEAD(hifs_inode_wait);
static DECLARE_WAIT_QUEUE_HEAD(hifs_cmd_in_wait);
static DECLARE_WAIT_QUEUE_HEAD(hifs_inode_in_wait);


static unsigned int hifs_cmd_in_queue_len(void)
{
	unsigned long flags;
	unsigned int len;

	spin_lock_irqsave(&hifs_cmd_in_lock, flags);
	len = kfifo_len(&hifs_cmd_in_fifo);
	spin_unlock_irqrestore(&hifs_cmd_in_lock, flags);

	return len;
}

static unsigned int hifs_inode_in_queue_len(void)
{
	unsigned long flags;
	unsigned int len;

	spin_lock_irqsave(&hifs_inode_in_lock, flags);
	len = kfifo_len(&hifs_inode_in_fifo);
	spin_unlock_irqrestore(&hifs_inode_in_lock, flags);

	return len;
}

static unsigned int hifs_cmd_out_queue_len(void)
{
	unsigned long flags;
	unsigned int len;

	spin_lock_irqsave(&hifs_cmd_lock, flags);
	len = kfifo_len(&hifs_cmd_fifo);
	spin_unlock_irqrestore(&hifs_cmd_lock, flags);

	return len;
}

static unsigned int hifs_inode_out_queue_len(void)
{
	unsigned long flags;
	unsigned int len;

	spin_lock_irqsave(&hifs_inode_lock, flags);
	len = kfifo_len(&hifs_inode_fifo);
	spin_unlock_irqrestore(&hifs_inode_lock, flags);

	return len;
}

int hifs_cmd_queue_push(const struct hifs_cmds_user *msg)
{
	unsigned long flags;
	int ret = 0;

	if (!msg)
		return -EINVAL;

	spin_lock_irqsave(&hifs_cmd_lock, flags);
	if (!kfifo_is_full(&hifs_cmd_fifo)) {
		kfifo_in(&hifs_cmd_fifo, msg, 1);
	} else {
		ret = -ENOSPC;
	}
	spin_unlock_irqrestore(&hifs_cmd_lock, flags);

	if (!ret)
		wake_up_interruptible(&hifs_cmd_wait);

	return ret;
}

int hifs_cmd_queue_push_cstr(const char *command)
{
	struct hifs_cmds_user msg;
	size_t len;

	if (!command)
		return -EINVAL;

	memset(&msg, 0, sizeof(msg));
	len = strnlen(command, HIFS_MAX_CMD_SIZE - 1);
	msg.count = len;
	strscpy(msg.cmd, command, sizeof(msg.cmd));

	return hifs_cmd_queue_push(&msg);
}

int hifs_inode_queue_push(const struct hifs_inode_user *msg)
{
	unsigned long flags;
	int ret = 0;

	if (!msg)
		return -EINVAL;

	spin_lock_irqsave(&hifs_inode_lock, flags);
	if (!kfifo_is_full(&hifs_inode_fifo)) {
		kfifo_in(&hifs_inode_fifo, msg, 1);
	} else {
		ret = -ENOSPC;
	}
	spin_unlock_irqrestore(&hifs_inode_lock, flags);

	if (!ret)
		wake_up_interruptible(&hifs_inode_wait);

	return ret;
}

static int hifs_cmd_in_queue_enqueue(const struct hifs_cmds_user *msg)
{
	unsigned long flags;
	int ret = 0;

	if (!msg)
		return -EINVAL;

	spin_lock_irqsave(&hifs_cmd_in_lock, flags);
	if (!kfifo_is_full(&hifs_cmd_in_fifo)) {
		kfifo_in(&hifs_cmd_in_fifo, msg, 1);
	} else {
		ret = -ENOSPC;
	}
	spin_unlock_irqrestore(&hifs_cmd_in_lock, flags);

	if (!ret)
		wake_up_interruptible(&hifs_cmd_in_wait);

	return ret;
}

static int hifs_inode_in_queue_enqueue(const struct hifs_inode_user *msg)
{
	unsigned long flags;
	int ret = 0;

	if (!msg)
		return -EINVAL;

	spin_lock_irqsave(&hifs_inode_in_lock, flags);
	if (!kfifo_is_full(&hifs_inode_in_fifo)) {
		kfifo_in(&hifs_inode_in_fifo, msg, 1);
	} else {
		ret = -ENOSPC;
	}
	spin_unlock_irqrestore(&hifs_inode_in_lock, flags);

	if (!ret)
		wake_up_interruptible(&hifs_inode_in_wait);

	return ret;
}

void hifs_prepare_inode_user(struct hifs_inode_user *dst, const struct hifs_inode *src)
{
	if (!dst || !src)
		return;

	memset(dst, 0, sizeof(*dst));

	dst->i_version = src->i_version;
	dst->i_flags = src->i_flags;
	dst->i_mode = src->i_mode;
	dst->i_ino = src->i_ino;
	dst->i_uid = src->i_uid;
	dst->i_gid = src->i_gid;
	dst->i_hrd_lnk = src->i_hrd_lnk;
	dst->i_atime = src->i_atime;
	dst->i_mtime = src->i_mtime;
	dst->i_ctime = src->i_ctime;
	dst->i_size = src->i_size;
	dst->i_blocks = src->i_blocks;
	dst->i_bytes = src->i_bytes;
	strscpy(dst->i_name, src->i_name, sizeof(dst->i_name));
	memcpy(dst->i_addrb, src->i_addrb, sizeof(dst->i_addrb));
	memcpy(dst->i_addre, src->i_addre, sizeof(dst->i_addre));
}

int hifs_inode_queue_push_from_inode(const struct hifs_inode *inode)
{
	struct hifs_inode_user msg;

	if (!inode)
		return -EINVAL;

	hifs_prepare_inode_user(&msg, inode);
	return hifs_inode_queue_push(&msg);
}

static int hifs_cmd_queue_dequeue(struct hifs_cmds_user *msg, bool nonblock)
{
	int ret;

	if (!msg)
		return -EINVAL;

	for (;;) {
		unsigned long flags;
		unsigned int copied;

		spin_lock_irqsave(&hifs_cmd_lock, flags);
		if (!kfifo_is_empty(&hifs_cmd_fifo)) {
			copied = kfifo_out(&hifs_cmd_fifo, msg, 1);
			spin_unlock_irqrestore(&hifs_cmd_lock, flags);
			if (copied == 1)
				return 0;
			return -EFAULT;
		}
		spin_unlock_irqrestore(&hifs_cmd_lock, flags);

		if (nonblock)
			return -EAGAIN;

		ret = wait_event_interruptible(hifs_cmd_wait,
					       !kfifo_is_empty(&hifs_cmd_fifo));
		if (ret)
			return ret;
	}
}

static int hifs_inode_queue_dequeue(struct hifs_inode_user *msg, bool nonblock)
{
	int ret;

	if (!msg)
		return -EINVAL;

	for (;;) {
		unsigned long flags;
		unsigned int copied;

		spin_lock_irqsave(&hifs_inode_lock, flags);
		if (!kfifo_is_empty(&hifs_inode_fifo)) {
			copied = kfifo_out(&hifs_inode_fifo, msg, 1);
			spin_unlock_irqrestore(&hifs_inode_lock, flags);
			if (copied == 1)
				return 0;
			return -EFAULT;
		}
		spin_unlock_irqrestore(&hifs_inode_lock, flags);

		if (nonblock)
			return -EAGAIN;

		ret = wait_event_interruptible(hifs_inode_wait,
					       !kfifo_is_empty(&hifs_inode_fifo));
		if (ret)
			return ret;
	}
}

static int hifs_cmd_in_queue_dequeue(struct hifs_cmds_user *msg, bool nonblock)
{
	int ret;

	if (!msg)
		return -EINVAL;

	for (;;) {
		unsigned long flags;
		unsigned int copied;

		spin_lock_irqsave(&hifs_cmd_in_lock, flags);
		if (!kfifo_is_empty(&hifs_cmd_in_fifo)) {
			copied = kfifo_out(&hifs_cmd_in_fifo, msg, 1);
			spin_unlock_irqrestore(&hifs_cmd_in_lock, flags);
			if (copied == 1)
				return 0;
			return -EFAULT;
		}
		spin_unlock_irqrestore(&hifs_cmd_in_lock, flags);

		if (nonblock)
			return -EAGAIN;

		ret = wait_event_interruptible(hifs_cmd_in_wait,
					       !kfifo_is_empty(&hifs_cmd_in_fifo));
		if (ret)
			return ret;
	}
}

static int hifs_inode_in_queue_dequeue(struct hifs_inode_user *msg, bool nonblock)
{
	int ret;

	if (!msg)
		return -EINVAL;

	for (;;) {
		unsigned long flags;
		unsigned int copied;

		spin_lock_irqsave(&hifs_inode_in_lock, flags);
		if (!kfifo_is_empty(&hifs_inode_in_fifo)) {
			copied = kfifo_out(&hifs_inode_in_fifo, msg, 1);
			spin_unlock_irqrestore(&hifs_inode_in_lock, flags);
			if (copied == 1)
				return 0;
			return -EFAULT;
		}
		spin_unlock_irqrestore(&hifs_inode_in_lock, flags);

		if (nonblock)
			return -EAGAIN;

		ret = wait_event_interruptible(hifs_inode_in_wait,
					       !kfifo_is_empty(&hifs_inode_in_fifo));
		if (ret)
			return ret;
	}
}

int hifs_cmd_response_dequeue(struct hifs_cmds_user *msg, bool nonblock)
{
	return hifs_cmd_in_queue_dequeue(msg, nonblock);
}

int hifs_inode_response_dequeue(struct hifs_inode_user *msg, bool nonblock)
{
	return hifs_inode_in_queue_dequeue(msg, nonblock);
}

static long hifs_comm_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	void __user *user_ptr = (void __user *)arg;
	bool nonblock = file->f_flags & O_NONBLOCK;

	switch (cmd) {
	case HIFS_IOCTL_CMD_DEQUEUE: {
		struct hifs_cmds_user msg;
		int ret = hifs_cmd_queue_dequeue(&msg, nonblock);

		if (ret)
			return ret;

		if (copy_to_user(user_ptr, &msg, sizeof(msg)))
			return -EFAULT;

		return 0;
	}
	case HIFS_IOCTL_INODE_DEQUEUE: {
		struct hifs_inode_user msg;
		int ret = hifs_inode_queue_dequeue(&msg, nonblock);

		if (ret)
			return ret;

		if (copy_to_user(user_ptr, &msg, sizeof(msg)))
			return -EFAULT;

		return 0;
	}
	case HIFS_IOCTL_STATUS: {
		struct hifs_comm_status status = {
			.cmd_available = hifs_cmd_out_queue_len(),
			.inode_available = hifs_inode_out_queue_len(),
			.cmd_pending = hifs_cmd_in_queue_len(),
			.inode_pending = hifs_inode_in_queue_len(),
		};

		if (copy_to_user(user_ptr, &status, sizeof(status)))
			return -EFAULT;

		return 0;
	}
	case HIFS_IOCTL_CMD_ENQUEUE: {
		struct hifs_cmds_user msg;

		if (copy_from_user(&msg, user_ptr, sizeof(msg)))
			return -EFAULT;

		return hifs_cmd_in_queue_enqueue(&msg);
	}
	case HIFS_IOCTL_INODE_ENQUEUE: {
		struct hifs_inode_user msg;

		if (copy_from_user(&msg, user_ptr, sizeof(msg)))
			return -EFAULT;

		return hifs_inode_in_queue_enqueue(&msg);
	}
	default:
		return -ENOTTY;
	}
}

static __poll_t hifs_comm_poll(struct file *file, poll_table *wait)
{
	__poll_t mask = 0;

	poll_wait(file, &hifs_cmd_wait, wait);
	poll_wait(file, &hifs_inode_wait, wait);

	if (hifs_cmd_out_queue_len())
		mask |= POLLIN | POLLRDNORM;

	if (hifs_inode_out_queue_len())
		mask |= POLLIN | POLLRDBAND;

	if (!kfifo_is_full(&hifs_cmd_in_fifo) || !kfifo_is_full(&hifs_inode_in_fifo))
		mask |= POLLOUT | POLLWRNORM;

	return mask;
}

static loff_t hifs_comm_no_llseek(struct file *file, loff_t offset, int whence)
{
	return -ESPIPE;
}

static const struct file_operations hifs_comm_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = hifs_comm_ioctl,
	.compat_ioctl = hifs_comm_ioctl,
	.poll = hifs_comm_poll,
	.llseek = hifs_comm_no_llseek,
};

static struct miscdevice hifs_comm_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = HIFS_COMM_DEVICE_NAME,
	.fops = &hifs_comm_fops,
};

int hifs_comm_init(void)
{
	int ret;

	INIT_KFIFO(hifs_cmd_fifo);
	INIT_KFIFO(hifs_inode_fifo);
	INIT_KFIFO(hifs_cmd_in_fifo);
	INIT_KFIFO(hifs_inode_in_fifo);

	ret = misc_register(&hifs_comm_device);
	if (ret)
		hifs_err("Failed to register comm device\n");

	return ret;
}

void hifs_comm_exit(void)
{
	unsigned long flags;

	misc_deregister(&hifs_comm_device);

	spin_lock_irqsave(&hifs_cmd_lock, flags);
	kfifo_reset(&hifs_cmd_fifo);
	spin_unlock_irqrestore(&hifs_cmd_lock, flags);

	spin_lock_irqsave(&hifs_inode_lock, flags);
	kfifo_reset(&hifs_inode_fifo);
	spin_unlock_irqrestore(&hifs_inode_lock, flags);

	spin_lock_irqsave(&hifs_cmd_in_lock, flags);
	kfifo_reset(&hifs_cmd_in_fifo);
	spin_unlock_irqrestore(&hifs_cmd_in_lock, flags);

	spin_lock_irqsave(&hifs_inode_in_lock, flags);
	kfifo_reset(&hifs_inode_in_fifo);
	spin_unlock_irqrestore(&hifs_inode_in_lock, flags);
}
