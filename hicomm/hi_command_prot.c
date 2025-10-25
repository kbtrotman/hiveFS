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

int hicomm_handle_command(int fd, const struct hifs_cmds *cmd)
{
	int ret = 0;

	if (!cmd)
		return -EINVAL;

	hifs_info("Command received: \"%s\" (%d bytes)", cmd->cmd, cmd->count);

	if (hifs_command_equals(cmd, HIFS_Q_PROTO_CMD_LINK_INIT)) {
		init_hive_link();
		if (sqldb.sql_init) {
			if (hicomm_send_cmd_str(fd, HIFS_Q_PROTO_CMD_LINK_READY) != 0)
				hifs_err("Failed to notify kernel that link is ready");
		} else {
			if (hicomm_send_cmd_str(fd, HIFS_Q_PROTO_CMD_LINK_DOWN) != 0)
				hifs_err("Failed to notify kernel that link is down");
		}
		return 0;
	}

	if (hifs_command_equals(cmd, HIFS_Q_PROTO_CMD_LINK_DOWN)) {
		close_hive_link();
		return 0;
	}

    if (hifs_command_equals(cmd, HIFS_Q_PROTO_CMD_TEST)) {
        struct hifs_data_frame frame;
        int err;

        ret = hicomm_comm_recv_data(fd, &frame, false);
        if (ret) {
            if (ret == -EAGAIN)
                return 0;
            hifs_err("Failed to fetch data payload: %d", ret);
            return ret;
    }

    if (hifs_command_equals(cmd, HIFS_Q_PROTO_CMD_SB_SEND)) {
        /* Receive kernel's per-volume SB message */
        struct hifs_data_frame frame;
        int err;
        ret = hicomm_comm_recv_data(fd, &frame, false);
        if (ret) {
            if (ret == -EAGAIN)
                return 0;
            hifs_err("Failed to fetch superblock payload: %d", ret);
            return ret;
        }
        /* TODO: decode struct hifs_sb_msg, lookup MariaDB by volume_id,
         * compare vsb.s_rev_level then vsb.s_wtime, prepare winning struct hifs_sb_msg. */
        /* For now, echo back what we received to complete handshake skeleton */
        err = hicomm_comm_send_data(fd, &frame);
        if (err) {
            hifs_err("Failed to send superblock response data: %d", err);
            ret = err;
        }
        err = hicomm_send_cmd_str(fd, HIFS_Q_PROTO_CMD_SB_RECV);
        if (err && !ret)
            ret = err;
        return ret;
    }

        if (frame.len == sizeof(struct hifs_inode))
            hicomm_print_inode((const struct hifs_inode *)frame.data);

        err = hicomm_comm_send_data(fd, &frame);
        if (err) {
            hifs_err("Failed to send data response: %d", err);
            ret = err;
        }

		err = hicomm_send_cmd_str(fd, "test_ack");
		if (err) {
			hifs_err("Failed to send command response: %d", err);
			if (!ret)
				ret = err;
		}
	}

	return ret;
}

void hicomm_print_inode(const struct hifs_inode *inode)
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
