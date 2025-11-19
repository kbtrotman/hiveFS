/**
 * HiveFS
 *
 * Hive Mind Filesystem user-space control tool
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

#include "hi_command.h"

static int comm_fd = -1;

/* Simple TCP relay for feeding kernel commands to remote listeners
 * and accepting commands from the network to push back into the
 * kernel command ring. This keeps the original kernel ring buffer
 * behaviour while adding TCP-based control.
 */
#define HICMD_MAX_TCP_CLIENTS 16
#define HICMD_DEFAULT_BIND_HOST "0.0.0.0"
#define HICMD_DEFAULT_BIND_PORT "7070"

struct tcp_client {
	int fd;
};

static int listen_fd = -1;
static struct tcp_client clients[HICMD_MAX_TCP_CLIENTS];
static size_t client_count = 0;

static void tcp_remove_client(size_t idx)
{
	if (idx >= client_count)
		return;
	close(clients[idx].fd);
	if (idx != client_count - 1)
		clients[idx] = clients[client_count - 1];
	client_count--;
}

static int tcp_set_nonblock(int fd)
{
	int flags = fcntl(fd, F_GETFL, 0);
	if (flags < 0)
		return -errno;
	if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
		return -errno;
	return 0;
}

static int tcp_bind_and_listen(const char *host, const char *port)
{
	struct addrinfo hints = {
		.ai_family   = AF_UNSPEC,
		.ai_socktype = SOCK_STREAM,
		.ai_flags    = AI_PASSIVE,
	};
	struct addrinfo *res = NULL, *rp = NULL;
	int rc = getaddrinfo(host, port, &hints, &res);
	if (rc != 0) {
		hifs_err("getaddrinfo(%s:%s) failed: %s", host, port, gai_strerror(rc));
		return -1;
	}

	int fd = -1;
	for (rp = res; rp; rp = rp->ai_next) {
		fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (fd < 0)
			continue;
		int one = 1;
		setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
		if (bind(fd, rp->ai_addr, rp->ai_addrlen) == 0 && listen(fd, 16) == 0) {
			break;
		}
		close(fd);
		fd = -1;
	}
	freeaddrinfo(res);

	if (fd >= 0) {
		tcp_set_nonblock(fd);
		hifs_notice("TCP control listening on %s:%s", host, port);
	}
	return fd;
}

static void tcp_accept_new(void)
{
	if (listen_fd < 0 || client_count >= HICMD_MAX_TCP_CLIENTS)
		return;

	struct sockaddr_storage ss;
	socklen_t slen = sizeof(ss);
	int fd = accept(listen_fd, (struct sockaddr *)&ss, &slen);
	if (fd < 0)
		return;
	if (tcp_set_nonblock(fd) != 0) {
		close(fd);
		return;
	}
	clients[client_count++].fd = fd;
}

static void tcp_broadcast_cmd(const struct hifs_cmds *cmd)
{
	if (!cmd || cmd->count <= 0)
		return;

	for (size_t i = 0; i < client_count; ) {
		ssize_t n = send(clients[i].fd, cmd->cmd, cmd->count, MSG_NOSIGNAL);
		if (n < 0 && (errno == EPIPE || errno == ECONNRESET)) {
			tcp_remove_client(i);
			continue;
		}
		++i;
	}
}

static void tcp_drain_client(size_t idx)
{
	char buf[HIFS_MAX_CMD_SIZE];
	ssize_t n = recv(clients[idx].fd, buf, sizeof(buf) - 1, 0);
	if (n <= 0) {
		tcp_remove_client(idx);
		return;
	}

	struct hifs_cmds cmd;
	memset(&cmd, 0, sizeof(cmd));
	if (n > HIFS_MAX_CMD_SIZE - 1)
		n = HIFS_MAX_CMD_SIZE - 1;
	cmd.count = (int)n;
	memcpy(cmd.cmd, buf, (size_t)n);

	/* push data from TCP into the kernel command ring */
	if (hicomm_comm_send_cmd(comm_fd, &cmd) != 0) {
		hifs_warning("failed to enqueue TCP command to kernel");
	}
}


void hicomm_log(int level, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);

    /* print to stderr/stdout */
    FILE *stream = (level <= LOG_ERR) ? stderr : stdout;
    vfprintf(stream, fmt, ap);
    fputc('\n', stream);
    fflush(stream);

    /* send to syslog as well */
    va_end(ap);
    va_start(ap, fmt);
    vsyslog(level, fmt, ap);
    va_end(ap);
}

int main(int argc, char *argv[])
{
	struct hifs_cmds cmd;
	int ret;
	int exit_code = EXIT_SUCCESS;

	(void)argc;
	(void)argv;
    
    openlog("hi_command", LOG_PID | LOG_NDELAY, LOG_USER); 

	comm_fd = hicomm_comm_open(false);
	if (comm_fd < 0) {
		hifs_err("Unable to open communication device. Is the kernel module loaded first?");
		return EXIT_FAILURE;
	}

	hifs_notice("Listening for commands via %s", HIFS_COMM_DEVICE_PATH);

	if (hicomm_send_cmd_str(comm_fd, HIFS_Q_PROTO_CMD_LINK_UP) != 0)
		hifs_warning("Failed to notify kernel that user link is up");

	init_hive_link();

	register_hive_host();
	if (!hifs_get_hive_host_sbs()) {
		hifs_warning("No local cache for this host; have you run mkcache? remote volumes will run solely on hive metadata.");
	}

	/* optional TCP control plane */
	const char *bind_host = getenv("HIFS_TCP_BIND_HOST");
	const char *bind_port = getenv("HIFS_TCP_BIND_PORT");
	if (!bind_host || !*bind_host)
		bind_host = HICMD_DEFAULT_BIND_HOST;
	if (!bind_port || !*bind_port)
		bind_port = HICMD_DEFAULT_BIND_PORT;
	listen_fd = tcp_bind_and_listen(bind_host, bind_port);

	for (;;) {
		struct pollfd pfds[1 + 1 + HICMD_MAX_TCP_CLIENTS];
		nfds_t nfds = 0;

		pfds[nfds].fd = comm_fd;
		pfds[nfds].events = POLLIN;
		++nfds;

		if (listen_fd >= 0) {
			pfds[nfds].fd = listen_fd;
			pfds[nfds].events = POLLIN;
			++nfds;
		}

		for (size_t i = 0; i < client_count; ++i) {
			pfds[nfds].fd = clients[i].fd;
			pfds[nfds].events = POLLIN;
			++nfds;
		}

		int poll_rc = poll(pfds, nfds, -1);
		if (poll_rc < 0) {
			if (errno == EINTR)
				continue;
			hifs_err("poll failed: %s", strerror(errno));
			exit_code = EXIT_FAILURE;
			goto out;
		}

		nfds_t idx = 0;

		/* kernel → user commands */
		if (pfds[idx].revents & POLLIN) {
			ret = hicomm_recv_cmd(comm_fd, &cmd, true);
			if (ret == 0) {
				hicomm_handle_command(comm_fd, &cmd);
				tcp_broadcast_cmd(&cmd);
			} else if (ret != -EAGAIN && ret != -EINTR) {
				hifs_err("Command dequeue failed: %d", ret);
				exit_code = EXIT_FAILURE;
				goto out;
			}
		}
		idx++;

		/* new TCP connections */
		if (listen_fd >= 0) {
			if (pfds[idx].revents & POLLIN)
				tcp_accept_new();
			idx++;
		}

		/* TCP client traffic */
		for (size_t c = 0; c < client_count && idx < nfds; ++c, ++idx) {
			if (pfds[idx].revents & POLLIN) {
				tcp_drain_client(c);
				/* tcp_drain_client may remove the current index;
				 * restart the loop in that case.
				 */
				break;
			}
		}
	}

out:
	for (size_t i = 0; i < client_count; ++i)
		close(clients[i].fd);
	if (listen_fd >= 0)
		close(listen_fd);
	if (comm_fd >= 0)
		hicomm_send_cmd_str(comm_fd, HIFS_Q_PROTO_CMD_LINK_DOWN);
	close_hive_link();
	hicomm_safe_cleanup();
    closelog();
	return exit_code;
}
