/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

#include "hi_command.h"

static int comm_fd = -1;

static int resolve_fd(int fd)
{
	if (fd >= 0)
		return fd;

	return comm_fd;
}

int hifs_comm_open(bool nonblock)
{
	int flags = O_RDWR;

	if (nonblock)
		flags |= O_NONBLOCK;

	comm_fd = open(HIFS_COMM_DEVICE_PATH, flags);
	if (comm_fd < 0) {
		hifs_err("Failed to open %s: %s", HIFS_COMM_DEVICE_PATH, strerror(errno));
		return -errno;
	}

	return comm_fd;
}

void hifs_comm_close(int fd)
{
	int target = resolve_fd(fd);

	if (target < 0)
		return;

	close(target);

	if (target == comm_fd)
		comm_fd = -1;
}

static int hifs_comm_ioctl_common(int fd, unsigned long request, void *arg)
{
	int target = resolve_fd(fd);

	if (target < 0)
		return -EINVAL;

	if (ioctl(target, request, arg) == -1)
		return -errno;

	return 0;
}

int hifs_comm_recv_cmd(int fd, struct hifs_cmds_user *cmd, bool nonblock)
{
	int target = resolve_fd(fd);

	if (target < 0 || !cmd)
		return -EINVAL;

	if (nonblock) {
		int flags = fcntl(target, F_GETFL, 0);
		if (flags < 0)
			return -errno;
		if (!(flags & O_NONBLOCK)) {
			if (fcntl(target, F_SETFL, flags | O_NONBLOCK) < 0)
				return -errno;
		}
	}

	return hifs_comm_ioctl_common(target, HIFS_IOCTL_CMD_DEQUEUE, cmd);
}

int hifs_comm_recv_inode(int fd, struct hifs_inode_user *inode, bool nonblock)
{
	int target = resolve_fd(fd);

	if (target < 0 || !inode)
		return -EINVAL;

	if (nonblock) {
		int flags = fcntl(target, F_GETFL, 0);
		if (flags < 0)
			return -errno;
		if (!(flags & O_NONBLOCK)) {
			if (fcntl(target, F_SETFL, flags | O_NONBLOCK) < 0)
				return -errno;
		}
	}

	return hifs_comm_ioctl_common(target, HIFS_IOCTL_INODE_DEQUEUE, inode);
}

int hifs_comm_get_status(int fd, struct hifs_comm_status *status)
{
	if (!status)
		return -EINVAL;

	memset(status, 0, sizeof(*status));
	return hifs_comm_ioctl_common(fd, HIFS_IOCTL_STATUS, status);
}

int hifs_comm_send_cmd(int fd, const struct hifs_cmds_user *cmd)
{
	if (!cmd)
		return -EINVAL;

	return hifs_comm_ioctl_common(fd, HIFS_IOCTL_CMD_ENQUEUE,
				      (void *)cmd);
}

int hifs_comm_send_cmd_string(int fd, const char *cmd)
{
	struct hifs_cmds_user msg;
	size_t len;

	if (!cmd)
		return -EINVAL;

	memset(&msg, 0, sizeof(msg));
	len = strnlen(cmd, HIFS_MAX_CMD_SIZE - 1);
	msg.count = len;
	strncpy(msg.cmd, cmd, sizeof(msg.cmd) - 1);

	return hifs_comm_send_cmd(fd, &msg);
}

int hifs_comm_send_inode(int fd, const struct hifs_inode_user *inode)
{
	if (!inode)
		return -EINVAL;

	return hifs_comm_ioctl_common(fd, HIFS_IOCTL_INODE_ENQUEUE,
				      (void *)inode);
}

void hi_comm_safe_cleanup(void)
{
	hifs_comm_close(-1);
}
