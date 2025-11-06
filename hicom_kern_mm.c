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

#include <linux/device.h>
#include <linux/jiffies.h>
#include <linux/kfifo.h>
#include <linux/kstrtox.h>
#include <linux/miscdevice.h>
#include <linux/minmax.h>
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

static atomic_long_t hifs_sysfs_timeout_override = ATOMIC_LONG_INIT(-1);
static unsigned int hifs_mount_timeout_ms = HIFS_IO_WAIT_DEFAULT_MS;

unsigned int hifs_get_io_timeout_ms(void)
{
    long override = atomic_long_read(&hifs_sysfs_timeout_override);

    if (override >= 0)
        return clamp_t(unsigned int, (unsigned int)override, 0U, HIFS_IO_WAIT_MAX_MS);

    return clamp_t(unsigned int, READ_ONCE(hifs_mount_timeout_ms), 0U, HIFS_IO_WAIT_MAX_MS);
}

void hifs_set_mount_io_timeout(unsigned int timeout_ms)
{
    timeout_ms = clamp_t(unsigned int, timeout_ms, 0U, HIFS_IO_WAIT_MAX_MS);
    WRITE_ONCE(hifs_mount_timeout_ms, timeout_ms);
}

static void hifs_set_sysfs_timeout_override(long timeout_ms)
{
    if (timeout_ms < 0)
        atomic_long_set(&hifs_sysfs_timeout_override, -1);
    else {
        timeout_ms = clamp_t(long, timeout_ms, 0L, (long)HIFS_IO_WAIT_MAX_MS);
        atomic_long_set(&hifs_sysfs_timeout_override, timeout_ms);
    }
}

static unsigned int hifs_wait_jiffies(unsigned int timeout_ms)
{
    if (!timeout_ms)
        return 0;
    return msecs_to_jiffies(timeout_ms);
}

#define HIFS_DEFINE_FIFO_POP(storage, fn_name, fifo_var, lock_var, wait_var, type) \
storage int fn_name(type *msg, bool nonblock)                                     \
{                                                                                \
    unsigned long flags;                                                         \
                                                                                 \
    if (!msg)                                                                    \
        return -EINVAL;                                                          \
                                                                                 \
    for (;;) {                                                                   \
        unsigned int copied;                                                     \
                                                                                 \
        spin_lock_irqsave(&(lock_var), flags);                                   \
        if (!kfifo_is_empty(&(fifo_var))) {                                      \
            copied = kfifo_out(&(fifo_var), msg, 1);                             \
            spin_unlock_irqrestore(&(lock_var), flags);                          \
            return (copied == 1) ? 0 : -EFAULT;                                  \
        }                                                                        \
        spin_unlock_irqrestore(&(lock_var), flags);                              \
                                                                                 \
        if (nonblock) {                                                          \
            unsigned int timeout_ms = hifs_get_io_timeout_ms();                  \
            int ret;                                                             \
                                                                                 \
            if (!timeout_ms)                                                     \
                return -EAGAIN;                                                  \
                                                                                 \
            ret = wait_event_interruptible_timeout((wait_var),                   \
                                                   !kfifo_is_empty(&(fifo_var)), \
                                                   hifs_wait_jiffies(timeout_ms));\
            if (ret > 0)                                                         \
                continue;                                                        \
            if (ret == 0)                                                        \
                return -EAGAIN;                                                  \
            return ret;                                                          \
        }                                                                        \
                                                                                 \
        {                                                                        \
            int ret = wait_event_interruptible((wait_var),                       \
                                               !kfifo_is_empty(&(fifo_var)));    \
            if (ret)                                                             \
                return ret;                                                      \
        }                                                                        \
    }                                                                            \
}

HIFS_DEFINE_FIFO_POP(static, hifs_cmd_fifo_out_pop, hifs_cmd_fifo,
                     hifs_cmd_lock, hifs_cmd_wait, struct hifs_cmds);
HIFS_DEFINE_FIFO_POP(static, hifs_data_fifo_out_pop, hifs_data_fifo,
                     hifs_data_lock, hifs_data_wait, struct hifs_data_frame);
HIFS_DEFINE_FIFO_POP( , hifs_cmd_fifo_in_pop, hifs_cmd_in_fifo,
                     hifs_cmd_in_lock, hifs_cmd_in_wait, struct hifs_cmds);
HIFS_DEFINE_FIFO_POP( , hifs_data_fifo_in_pop, hifs_data_in_fifo,
                     hifs_data_in_lock, hifs_data_in_wait, struct hifs_data_frame);

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

static ssize_t timeout_inms_show(struct device *dev,
                                 struct device_attribute *attr, char *buf)
{
    return scnprintf(buf, PAGE_SIZE, "%u\n", hifs_get_io_timeout_ms());
}

static ssize_t timeout_inms_store(struct device *dev,
                                  struct device_attribute *attr,
                                  const char *buf, size_t count)
{
    long val;
    int ret;

    ret = kstrtol(buf, 0, &val);
    if (ret)
        return ret;

    hifs_set_sysfs_timeout_override(val);
    return count;
}

static DEVICE_ATTR_RW(timeout_inms);

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
		struct hifs_data_frame *frame;
		int ret;

		frame = kmalloc(sizeof(*frame), GFP_KERNEL);
		if (!frame)
			return -ENOMEM;

		ret = hifs_data_fifo_out_pop(frame, nonblock);
		if (ret) {
			kfree(frame);
			return ret;
		}

		if (copy_to_user(user_ptr, frame, sizeof(*frame)))
			ret = -EFAULT;

		kfree(frame);
		return ret;
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
	case HIFS_IOCTL_CACHE_STATUS: {
		struct hifs_cache_status stats;

		hifs_cache_get_stats(&stats);
		if (copy_to_user(user_ptr, &stats, sizeof(stats)))
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
		struct hifs_data_frame *frame;
		int ret;

		frame = kmalloc(sizeof(*frame), GFP_KERNEL);
		if (!frame)
			return -ENOMEM;

		if (copy_from_user(frame, user_ptr, sizeof(*frame))) {
			kfree(frame);
			return -EFAULT;
		}

		ret = hifs_data_fifo_in_push_buf(frame->data, frame->len);
		kfree(frame);
		return ret;
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

    if (!kfifo_is_full(&hifs_cmd_in_fifo) || !kfifo_is_full(&hifs_data_in_fifo)) {
        mask |= POLLOUT | POLLWRNORM;
    }

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
    if (ret) {
        hifs_err("Failed to register comm device\n");
        return ret;
    }

    ret = device_create_file(hifs_comm_device.this_device, &dev_attr_timeout_inms);
    if (ret) {
        hifs_err("Failed to create timeout_inms attribute: %d", ret);
        misc_deregister(&hifs_comm_device);
        return ret;
    }

    return 0;
}

void hifs_fifo_exit(void)
{
	unsigned long flags;

    if (hifs_comm_device.this_device) {
        device_remove_file(hifs_comm_device.this_device, &dev_attr_timeout_inms);
    }

    misc_deregister(&hifs_comm_device);
    atomic_long_set(&hifs_sysfs_timeout_override, -1);
    hifs_set_mount_io_timeout(HIFS_IO_WAIT_DEFAULT_MS);

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
