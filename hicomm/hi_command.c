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

int main(int argc, char *argv[])
{
	struct hifs_cmds_user cmd;
	int ret;
	int exit_code = EXIT_SUCCESS;

	(void)argc;
	(void)argv;

	comm_fd = hifs_comm_open(false);
	if (comm_fd < 0) {
		hifs_err("Unable to open communication device");
		return EXIT_FAILURE;
	}

	hifs_notice("Listening for commands via %s", HIFS_COMM_DEVICE_PATH);

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
	hi_comm_safe_cleanup();
	return exit_code;
}
