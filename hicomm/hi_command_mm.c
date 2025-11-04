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

int hicomm_comm_open(bool nonblock)
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

void hicomm_comm_close(int fd)
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

int hicomm_recv_cmd(int fd, struct hifs_cmds *cmd, bool nonblock)
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

int hicomm_comm_recv_data(int fd, struct hifs_data_frame *frame, bool nonblock)
{
    int target = resolve_fd(fd);

    if (target < 0 || !frame)
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

    return hifs_comm_ioctl_common(target, HIFS_IOCTL_DATA_DEQUEUE, frame);
}

int hicomm_comm_get_status(int fd, struct hifs_comm_status *status)
{
	if (!status)
		return -EINVAL;

	memset(status, 0, sizeof(*status));
    return hifs_comm_ioctl_common(fd, HIFS_IOCTL_STATUS, status);
}

int hicomm_comm_get_cache_status(int fd, struct hifs_cache_status *status)
{
	if (!status)
		return -EINVAL;

	memset(status, 0, sizeof(*status));
	return hifs_comm_ioctl_common(fd, HIFS_IOCTL_CACHE_STATUS, status);
}

int hicomm_comm_send_cmd(int fd, const struct hifs_cmds *cmd)
{
	if (!cmd)
		return -EINVAL;

    return hifs_comm_ioctl_common(fd, HIFS_IOCTL_CMD_ENQUEUE,
                          (void *)cmd);
}

int hicomm_send_cmd_str(int fd, const char *cmd)
{
	struct hifs_cmds msg;
	size_t len;

	if (!cmd)
		return -EINVAL;

	memset(&msg, 0, sizeof(msg));
	len = strnlen(cmd, HIFS_MAX_CMD_SIZE - 1);
	msg.count = len;
	strncpy(msg.cmd, cmd, sizeof(msg.cmd) - 1);

	return hicomm_comm_send_cmd(fd, &msg);
}

int hicomm_comm_send_data(int fd, const struct hifs_data_frame *frame)
{
    if (!frame)
        return -EINVAL;

    return hifs_comm_ioctl_common(fd, HIFS_IOCTL_DATA_ENQUEUE,
                      (void *)frame);
}

void hicomm_safe_cleanup(void)
{
	hicomm_comm_close(-1);
}
