/**
 * HiveFS
 *
 * Hive Mind Filesystem user-space control tool
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

#include "hi_command.h"
#include "sql/hi_command_sql.h"

static int comm_fd = -1;


void hifs_log_user(int level, const char *fmt, ...)
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
    
    openlog("hivefs", LOG_PID | LOG_NDELAY, LOG_USER); 

	comm_fd = hifs_comm_open(false);
	if (comm_fd < 0) {
		hifs_err("Unable to open communication device");
		return EXIT_FAILURE;
	}

	hifs_notice("Listening for commands via %s", HIFS_COMM_DEVICE_PATH);

	if (hifs_comm_send_cmd_string(comm_fd, HIFS_Q_PROTO_CMD_LINK_UP) != 0)
		hifs_warning("Failed to notify kernel that user link is up");

	for (;;) {
		ret = hifs_comm_recv_cmd(comm_fd, &cmd, false);
		if (ret == -EINTR)
			continue;
		if (ret) {
			if (ret == -EAGAIN) {
				usleep(100000);
				continue;
			}
			hifs_err("Command dequeue failed: %d", ret);
			exit_code = EXIT_FAILURE;
			goto out;
		}

		ret = hifs_handle_command(comm_fd, &cmd);
		if (ret && ret != -EAGAIN) {
			hifs_err("Command handling failed: %d", ret);
			exit_code = EXIT_FAILURE;
			goto out;
		}
	}

out:
	if (comm_fd >= 0)
		hifs_comm_send_cmd_string(comm_fd, HIFS_Q_PROTO_CMD_LINK_DOWN);
	close_hive_link();
	hi_comm_safe_cleanup();
    closelog();
	return exit_code;
}
