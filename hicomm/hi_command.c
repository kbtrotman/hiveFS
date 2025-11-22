/**
 * HiveFS
 *
 * Hive Mind Filesystem user-space control tool
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

#include "hi_command.h"
#include "hi_command_tcp.h"
#include <signal.h>

static int comm_fd = -1;

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

	/* Avoid unexpected termination if the guard socket dies */
	signal(SIGPIPE, SIG_IGN);

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
	hicmd_tcp_init(bind_host, bind_port, comm_fd);

	for (;;) {
		ret = hicomm_recv_cmd(comm_fd, &cmd, true);
		if (ret == 0) {
			ret = hicomm_handle_command(comm_fd, &cmd);
			if (ret && ret != -EAGAIN)
				hifs_warning("Command handling failed: %d", ret);

			hicmd_tcp_broadcast_cmd(&cmd);
			hicmd_tcp_poll(0);
			continue;
		}
		if (ret == -EINTR)
			continue;
		if (ret == -EAGAIN) {
			hicmd_tcp_poll(250);
			continue;
		}
		hifs_err("Command dequeue failed: %d", ret);
		exit_code = EXIT_FAILURE;
		goto out;
	}

out:
	hicmd_tcp_shutdown();
	if (comm_fd >= 0)
		hicomm_send_cmd_str(comm_fd, HIFS_Q_PROTO_CMD_LINK_DOWN);
	close_hive_link();
	hicomm_safe_cleanup();
    closelog();
	return exit_code;
}
