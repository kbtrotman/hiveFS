/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

#include "hi_command.h"
#include "sql/hi_command_sql.h"

static bool hifs_command_equals(const struct hifs_cmds *cmd, const char *name)
{
	size_t len;

	if (!cmd || !name)
		return false;

	len = strnlen(cmd->cmd, HIFS_MAX_CMD_SIZE);
	return len == strlen(name) && strncmp(cmd->cmd, name, HIFS_MAX_CMD_SIZE) == 0;
}

int hifs_handle_command(int fd, const struct hifs_cmds *cmd)
{
	int ret = 0;

	if (!cmd)
		return -EINVAL;

	hifs_info("Command received: \"%s\" (%d bytes)", cmd->cmd, cmd->count);

	if (hifs_command_equals(cmd, HIFS_Q_PROTO_CMD_LINK_INIT)) {
		init_hive_link();
		if (sqldb.sql_init) {
			if (hifs_comm_send_cmd_string(fd, HIFS_Q_PROTO_CMD_LINK_READY) != 0)
				hifs_err("Failed to notify kernel that link is ready");
		} else {
			if (hifs_comm_send_cmd_string(fd, HIFS_Q_PROTO_CMD_LINK_DOWN) != 0)
				hifs_err("Failed to notify kernel that link is down");
		}
		return 0;
	}

	if (hifs_command_equals(cmd, HIFS_Q_PROTO_CMD_LINK_DOWN)) {
		close_hive_link();
		return 0;
	}

	if (hifs_command_equals(cmd, HIFS_Q_PROTO_CMD_TEST)) {
		struct hifs_inode inode;
		int err;

		ret = hifs_comm_recv_inode(fd, &inode, false);
		if (ret) {
			if (ret == -EAGAIN)
				return 0;
			hifs_err("Failed to fetch inode payload: %d", ret);
			return ret;
		}

		hifs_print_inode(&inode);

		err = hifs_comm_send_inode(fd, &inode);
		if (err) {
			hifs_err("Failed to send inode response: %d", err);
			ret = err;
		}

		err = hifs_comm_send_cmd_string(fd, "test_ack");
		if (err) {
			hifs_err("Failed to send command response: %d", err);
			if (!ret)
				ret = err;
		}
	}

	return ret;
}

void hifs_print_inode(const struct hifs_inode *inode)
{
	if (!inode)
		return;

	hifs_info("Inode %llu \"%s\" mode %#o size %u bytes blocks %u",
		  (unsigned long long)inode->i_ino,
		  inode->i_name,
		  inode->i_mode,
		  inode->i_size,
		  inode->i_blocks);
}

const char *hifs_link_state_string(enum hifs_link_state state)
{
	switch (state) {
	case HIFS_COMM_LINK_UP:
		return "up";
	case HIFS_COMM_LINK_PARTIAL:
		return "partial";
	case HIFS_COMM_LINK_DOWN:
	default:
		return "down";
	}
}
