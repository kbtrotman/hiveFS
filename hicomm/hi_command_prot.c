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

#include <inttypes.h>

static bool hifs_command_equals(const struct hifs_cmds *cmd, const char *name)
{
	size_t len;

	if (!cmd || !name)
		return false;

	len = strnlen(cmd->cmd, HIFS_MAX_CMD_SIZE);
	return len == strlen(name) && strncmp(cmd->cmd, name, HIFS_MAX_CMD_SIZE) == 0;
}

#define VSB_CMD_EQUALS(name) hifs_command_equals(cmd, (name))

static int hifs_compare_sb_newer(const struct hifs_volume_superblock *a,
				 const struct hifs_volume_superblock *b)
{
	uint32_t a_gen = a ? a->s_rev_level : 0;
	uint32_t b_gen = b ? b->s_rev_level : 0;
	if (a_gen != b_gen)
		return (a_gen > b_gen) ? 1 : -1;

	uint32_t a_wtime = a ? a->s_wtime : 0;
	uint32_t b_wtime = b ? b->s_wtime : 0;
	if (a_wtime != b_wtime)
		return (a_wtime > b_wtime) ? 1 : -1;
	return 0;
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

	if (VSB_CMD_EQUALS(HIFS_Q_PROTO_CMD_LINK_DOWN)) {
		close_hive_link();
		return 0;
	}

	if (VSB_CMD_EQUALS(HIFS_Q_PROTO_CMD_TEST)) {
		struct hifs_data_frame frame;
		int err;

		ret = hicomm_comm_recv_data(fd, &frame, false);
		if (ret) {
			if (ret == -EAGAIN)
				return 0;
			hifs_err("Failed to fetch data payload: %d", ret);
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

	if (VSB_CMD_EQUALS(HIFS_Q_PROTO_CMD_SB_SEND)) {
		struct hifs_data_frame frame;
		struct hifs_sb_msg msg_local;
		struct hifs_sb_msg msg_reply;
		struct hifs_volume_superblock db_vsb;
		bool have_db;
		bool save_local = false;
		int err;

		ret = hicomm_comm_recv_data(fd, &frame, false);
		if (ret) {
			if (ret == -EAGAIN)
				return 0;
			hifs_err("Failed to fetch superblock payload: %d", ret);
			return ret;
		}
		if (frame.len != sizeof(struct hifs_sb_msg)) {
			hifs_err("Unexpected superblock payload length %u (expected %zu)",
				 frame.len, sizeof(struct hifs_sb_msg));
			return -EINVAL;
		}

		memcpy(&msg_local, frame.data, sizeof(msg_local));
		msg_reply.volume_id = msg_local.volume_id;
		msg_reply.vsb = msg_local.vsb;

		have_db = hifs_volume_super_get(msg_local.volume_id, &db_vsb);
		if (have_db) {
			int cmp = hifs_compare_sb_newer(&db_vsb, &msg_local.vsb);
			if (cmp > 0) {
				msg_reply.vsb = db_vsb;
			} else if (cmp < 0) {
				save_local = true;
			}
		} else {
			save_local = true;
		}

		if (save_local) {
			if (!hifs_volume_super_set(msg_local.volume_id, &msg_local.vsb)) {
				hifs_err("Failed to persist volume superblock %" PRIu64 " in database",
					 (uint64_t)msg_local.volume_id);
			}
		}

		struct hifs_data_frame out_frame = {0};
		out_frame.len = sizeof(msg_reply);
		memcpy(out_frame.data, &msg_reply, sizeof(msg_reply));

		err = hicomm_comm_send_data(fd, &out_frame);
		if (err) {
			hifs_err("Failed to send superblock response data: %d", err);
			ret = err;
		}
		err = hicomm_send_cmd_str(fd, HIFS_Q_PROTO_CMD_SB_RECV);
		if (err && !ret)
			ret = err;
		return ret;
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
