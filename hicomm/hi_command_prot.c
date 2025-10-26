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

/* Helper to compare command strings */
static bool hifs_command_equals(const struct hifs_cmds *cmd, const char *name)
{
	size_t len;

	if (!cmd || !name)
		return false;

	len = strnlen(cmd->cmd, HIFS_MAX_CMD_SIZE);
	return len == strlen(name) && strncmp(cmd->cmd, name, HIFS_MAX_CMD_SIZE) == 0;
}

/* Macro to simplify command comparisons */
#define VSB_CMD_EQUALS(name) hifs_command_equals(cmd, (name))

/* Compare two superblocks; return 1 if 'a' is newer, -1 if 'b' is newer, 0 if equal */
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

static int hifs_compare_dentry_newer(const struct hifs_volume_dentry *a,
				    const struct hifs_volume_dentry *b)
{
	uint32_t a_epoch = a ? a->de_epoch : 0;
	uint32_t b_epoch = b ? b->de_epoch : 0;
	if (a_epoch != b_epoch)
		return (a_epoch > b_epoch) ? 1 : -1;
	return 0;
}

/* Compare two root dentries; return 1 if 'a' is newer, -1 if 'b' is newer, 0 if equal */
static int hifs_compare_root_newer(const struct hifs_volume_root_dentry *a,
				   const struct hifs_volume_root_dentry *b)
{
	uint32_t a_ctime = a ? a->rd_ctime : 0;
	uint32_t b_ctime = b ? b->rd_ctime : 0;
	if (a_ctime != b_ctime)
		return (a_ctime > b_ctime) ? 1 : -1;

	uint32_t a_mtime = a ? a->rd_mtime : 0;
	uint32_t b_mtime = b ? b->rd_mtime : 0;
	if (a_mtime != b_mtime)
		return (a_mtime > b_mtime) ? 1 : -1;

	uint64_t a_inode = a ? a->rd_inode : 0;
	uint64_t b_inode = b ? b->rd_inode : 0;
	if (a_inode != b_inode)
		return (a_inode > b_inode) ? 1 : -1;

	return 0;
}

/* Handle a received command */
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

	if (VSB_CMD_EQUALS(HIFS_Q_PROTO_CMD_ROOT_SEND)) {
		struct hifs_data_frame frame;
		struct hifs_root_msg msg_local;
		struct hifs_root_msg msg_reply;
		struct hifs_volume_root_dentry db_root;
		bool have_db;
		bool save_local = false;
		int err;

		ret = hicomm_comm_recv_data(fd, &frame, false);
		if (ret) {
			if (ret == -EAGAIN)
				return 0;
			hifs_err("Failed to fetch root dentry payload: %d", ret);
			return ret;
		}
		if (frame.len != sizeof(struct hifs_root_msg)) {
			hifs_err("Unexpected root dentry payload length %u (expected %zu)",
				 frame.len, sizeof(struct hifs_root_msg));
			return -EINVAL;
		}

		memcpy(&msg_local, frame.data, sizeof(msg_local));
		msg_reply.volume_id = msg_local.volume_id;
		msg_reply.root = msg_local.root;

		have_db = hifs_root_dentry_load(msg_local.volume_id, &db_root);
		if (have_db) {
			int cmp = hifs_compare_root_newer(&db_root, &msg_local.root);
			if (cmp > 0) {
				msg_reply.root = db_root;
			} else if (cmp < 0) {
				save_local = true;
			}
		} else {
			save_local = true;
		}

		if (save_local) {
			if (!hifs_root_dentry_store(msg_local.volume_id, &msg_local.root)) {
				hifs_err("Failed to persist root dentry for volume %" PRIu64,
					 (uint64_t)msg_local.volume_id);
			}
		}

		struct hifs_data_frame out_frame = {0};
		out_frame.len = sizeof(msg_reply);
		memcpy(out_frame.data, &msg_reply, sizeof(msg_reply));

		err = hicomm_comm_send_data(fd, &out_frame);
		if (err) {
			hifs_err("Failed to send root dentry response data: %d", err);
			ret = err;
		}
		err = hicomm_send_cmd_str(fd, HIFS_Q_PROTO_CMD_ROOT_RECV);
		if (err && !ret)
			ret = err;
		return ret;
	}

	if (VSB_CMD_EQUALS(HIFS_Q_PROTO_CMD_DENTRY_SEND)) {
		struct hifs_data_frame frame;
		struct hifs_dentry_msg msg_local;
		struct hifs_dentry_msg msg_reply;
		struct hifs_volume_dentry db_dent;
		bool have_db;
		bool save_local = false;
		int err;

		ret = hicomm_comm_recv_data(fd, &frame, false);
		if (ret) {
			if (ret == -EAGAIN)
				return 0;
			hifs_err("Failed to fetch dentry payload: %d", ret);
			return ret;
		}
		if (frame.len != sizeof(struct hifs_dentry_msg)) {
			hifs_err("Unexpected dentry payload length %u (expected %zu)",
				 frame.len, sizeof(struct hifs_dentry_msg));
			return -EINVAL;
		}

		memcpy(&msg_local, frame.data, sizeof(msg_local));
		msg_reply.volume_id = msg_local.volume_id;
		msg_reply.dentry = msg_local.dentry;

		have_db = hifs_volume_dentry_load(msg_local.volume_id,
				 msg_local.dentry.de_inode, &db_dent);
		if (have_db) {
			int cmp = hifs_compare_dentry_newer(&db_dent, &msg_local.dentry);
			if (cmp > 0) {
				msg_reply.dentry = db_dent;
			} else if (cmp < 0) {
				save_local = true;
			}
		} else {
			save_local = true;
		}

		if (save_local) {
			if (!hifs_volume_dentry_store(msg_local.volume_id,
					&msg_local.dentry)) {
				hifs_err("Failed to persist dentry for volume %" PRIu64,
					 (uint64_t)msg_local.volume_id);
			}
		}

		struct hifs_data_frame out_frame = {0};
		out_frame.len = sizeof(msg_reply);
		memcpy(out_frame.data, &msg_reply, sizeof(msg_reply));

		err = hicomm_comm_send_data(fd, &out_frame);
		if (err) {
			hifs_err("Failed to send dentry response data: %d", err);
			ret = err;
		}
		err = hicomm_send_cmd_str(fd, HIFS_Q_PROTO_CMD_DENTRY_RECV);
		if (err && !ret)
			ret = err;
		return ret;
	}

	return ret;
}

/* Print inode information */
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

/* Get string representation of link state */
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
