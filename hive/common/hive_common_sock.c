/**
 * HiveFS
 *
 * Common socket helpers.
 */

#include "hive_common_sock.h"

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

static void copy_path(char *dst, size_t dst_len, const char *src)
{
	if (!dst || dst_len == 0)
		return;
	if (!src)
		src = "";
	size_t len = strnlen(src, dst_len - 1);
	memcpy(dst, src, len);
	dst[len] = '\0';
}

int hive_common_sock_create_listener(const struct hive_common_sock_params *params)
{
	struct sockaddr_un addr;
	const char *path;

	if (!params)
		return -1;
	path = (params->path && *params->path)
		? params->path
		: params->default_path;
	if (!path || !*path)
		return -1;

	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0)
		return -1;

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	copy_path(addr.sun_path, sizeof(addr.sun_path), path);
	unlink(addr.sun_path);
	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		int err = errno;
		close(fd);
		errno = err;
		return -1;
	}
	mode_t mode = params->mode ? params->mode : 0666;
	if (chmod(addr.sun_path, mode) != 0) {
		int err = errno;
		close(fd);
		unlink(addr.sun_path);
		errno = err;
		return -1;
	}
	int backlog = params->backlog > 0 ? params->backlog : SOMAXCONN;
	if (listen(fd, backlog) != 0) {
		int err = errno;
		close(fd);
		unlink(addr.sun_path);
		errno = err;
		return -1;
	}
	if (params->resolved_path && params->resolved_path_len > 0)
		copy_path(params->resolved_path, params->resolved_path_len,
			  addr.sun_path);
	return fd;
}

void hive_common_sock_destroy_listener(int fd, const char *path)
{
	if (fd >= 0)
		close(fd);
	if (path && *path)
		unlink(path);
}

void hive_common_sock_respond(int fd, const char *msg)
{
	size_t len;

	if (fd < 0 || !msg)
		return;
	len = strlen(msg);
	while (len > 0) {
		ssize_t wr = write(fd, msg, len);
		if (wr < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		if (wr == 0)
			break;
		msg += wr;
		len -= (size_t)wr;
	}
}

bool hive_common_sock_read_json_request(int fd, char *buf, size_t buf_sz)
{
	size_t total = 0;

	if (!buf || buf_sz == 0)
		return false;

	while (total < buf_sz - 1) {
		ssize_t rd = recv(fd, buf + total, buf_sz - 1 - total, 0);
		if (rd < 0) {
			if (errno == EINTR)
				continue;
			return false;
		}
		if (rd == 0)
			break;
		total += (size_t)rd;
		if (memchr(buf + total - rd, '\n', (size_t)rd) ||
		    memchr(buf + total - rd, '\r', (size_t)rd))
			break;
	}
	if (total == 0)
		return false;
	buf[total] = '\0';
	char *newline = strpbrk(buf, "\r\n");
	if (newline)
		*newline = '\0';
	return true;
}
