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
#include <endian.h>
#include <endian.h>

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

static void bin_to_hex(const char *src, size_t len, char *dst)
{
	static const char hexdigits[] = "0123456789abcdef";
	for (size_t i = 0; i < len; ++i) {
		dst[i * 2] = hexdigits[(src[i] >> 4) & 0xF];
		dst[i * 2 + 1] = hexdigits[src[i] & 0xF];
	}
	dst[len * 2] = '\0';
}

static int hifs_compare_inode_newer(const struct hifs_inode_wire *a,
				 const struct hifs_inode_wire *b)
{
	uint32_t a_ctime = a ? le32toh(a->i_ctime) : 0;
	uint32_t b_ctime = b ? le32toh(b->i_ctime) : 0;
	if (a_ctime != b_ctime)
		return (a_ctime > b_ctime) ? 1 : -1;

	uint32_t a_mtime = a ? le32toh(a->i_mtime) : 0;
	uint32_t b_mtime = b ? le32toh(b->i_mtime) : 0;
	if (a_mtime != b_mtime)
		return (a_mtime > b_mtime) ? 1 : -1;

	uint32_t a_size = a ? le32toh(a->i_size) : 0;
	uint32_t b_size = b ? le32toh(b->i_size) : 0;
	if (a_size != b_size)
		return (a_size > b_size) ? 1 : -1;

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
		uint64_t volume_id;
		bool request_only;

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
		volume_id = le64toh(msg_local.volume_id);

		have_db = hifs_root_dentry_load(volume_id, &db_root);
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

		request_only = (le32toh(msg_local.root.rd_links) == 0 &&
			       le32toh(msg_local.root.rd_inode) == 0);

		if (request_only && have_db) {
			msg_reply.root = db_root;
		}

		if (!request_only && save_local) {
			if (!hifs_root_dentry_store(volume_id, &msg_local.root)) {
				hifs_err("Failed to persist root dentry for volume %" PRIu64,
					 volume_id);
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
		uint64_t volume_id;
		uint64_t inode_no;
		uint64_t parent_no;
		bool request_only;
		char name_hex[HIFS_MAX_NAME_SIZE * 2 + 1];

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
		volume_id = le64toh(msg_local.volume_id);
		inode_no = le64toh(msg_local.dentry.de_inode);

		parent_no = le64toh(msg_local.dentry.de_parent);
		request_only = (le32toh(msg_local.dentry.de_flags) & HIFS_DENTRY_MSGF_REQUEST) != 0;
		if (request_only) {
			uint32_t name_len = le32toh(msg_local.dentry.de_name_len);
			if (name_len > HIFS_MAX_NAME_SIZE)
				name_len = HIFS_MAX_NAME_SIZE;
			bin_to_hex(msg_local.dentry.de_name, name_len, name_hex);
			have_db = hifs_volume_dentry_load_by_name(volume_id, parent_no,
				name_hex, name_len * 2, &db_dent);
		} else {
			have_db = hifs_volume_dentry_load_by_inode(volume_id,
				inode_no, &db_dent);
		}
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

	hifs_info("rx dentry vol=%" PRIu64 " parent=%" PRIu64 " inode=%" PRIu64 " flags=%#x",
		volume_id, parent_no, inode_no, le32toh(msg_local.dentry.de_flags));
	if (request_only && have_db) {
		msg_reply.dentry = db_dent;
	}

		if (!request_only && save_local) {
			msg_local.dentry.de_flags = htole32(0);
			if (!hifs_volume_dentry_store(volume_id,
					&msg_local.dentry)) {
				hifs_err("Failed to persist dentry for volume %" PRIu64,
					 volume_id);
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

	if (VSB_CMD_EQUALS(HIFS_Q_PROTO_CMD_INODE_SEND)) {
		struct hifs_data_frame frame;
		struct hifs_inode_msg msg_local;
		struct hifs_inode_msg msg_reply;
		struct hifs_inode_wire db_wire;
		bool have_db;
		bool save_local = false;
		int err;
		uint64_t volume_id;
		uint64_t inode_no;
		bool request_only;

		ret = hicomm_comm_recv_data(fd, &frame, false);
		if (ret) {
			if (ret == -EAGAIN)
				return 0;
			hifs_err("Failed to fetch inode payload: %d", ret);
			return ret;
		}
		if (frame.len != sizeof(struct hifs_inode_msg)) {
			hifs_err("Unexpected inode payload length %u (expected %zu)",
				 frame.len, sizeof(struct hifs_inode_msg));
			return -EINVAL;
		}

		memcpy(&msg_local, frame.data, sizeof(msg_local));
		msg_reply = msg_local;
		volume_id = le64toh(msg_local.volume_id);
		inode_no = le64toh(msg_local.inode.i_ino);
		request_only = (le32toh(msg_local.inode.i_msg_flags) & HIFS_INODE_MSGF_REQUEST) != 0;

		have_db = hifs_volume_inode_load(volume_id, inode_no, &db_wire);
		if (have_db) {
			int cmp = hifs_compare_inode_newer(&db_wire, &msg_local.inode);
			if (cmp > 0) {
				msg_reply.inode = db_wire;
			} else if (cmp < 0) {
				save_local = true;
			}
		} else {
			save_local = true;
		}

	hifs_info("rx inode vol=%" PRIu64 " ino=%" PRIu64 " flags=%#x",
		volume_id, inode_no, le32toh(msg_local.inode.i_msg_flags));
	if (request_only && have_db) {
		msg_reply.inode = db_wire;
	}

		if (!request_only && save_local) {
			msg_local.inode.i_msg_flags = htole32(0);
			if (!hifs_volume_inode_store(volume_id, &msg_local.inode)) {
				hifs_err("Failed to persist inode %" PRIu64 " for volume %" PRIu64,
					 inode_no, volume_id);
			}
		}

		struct hifs_data_frame out_frame = {0};
		out_frame.len = sizeof(msg_reply);
		memcpy(out_frame.data, &msg_reply, sizeof(msg_reply));

		err = hicomm_comm_send_data(fd, &out_frame);
		if (err) {
			hifs_err("Failed to send inode response data: %d", err);
			ret = err;
		}
		err = hicomm_send_cmd_str(fd, HIFS_Q_PROTO_CMD_INODE_RECV);
		if (err && !ret)
			ret = err;
		return ret;
	}

	if (VSB_CMD_EQUALS(HIFS_Q_PROTO_CMD_BLOCK_SEND)) {
		struct hifs_data_frame frame;
		struct hifs_block_msg msg_local;
		struct hifs_block_msg msg_reply;
		bool have_db = false;
		bool save_local = false;
		int err;
		uint64_t volume_id;
		uint64_t block_no;
		bool request_only;
		uint8_t block_buf[HIFS_DEFAULT_BLOCK_SIZE];
		uint32_t block_len = 0;
		struct hifs_data_frame data_frame;
		const uint8_t *incoming_data = NULL;
		uint32_t incoming_len = 0;

		ret = hicomm_comm_recv_data(fd, &frame, false);
		if (ret) {
			if (ret == -EAGAIN)
				return 0;
			hifs_err("Failed to fetch block payload: %d", ret);
			return ret;
		}
		if (frame.len != sizeof(struct hifs_block_msg)) {
			hifs_err("Unexpected block payload length %u (expected %zu)",
				 frame.len, sizeof(struct hifs_block_msg));
			return -EINVAL;
		}

		memcpy(&msg_local, frame.data, sizeof(msg_local));
		msg_reply = msg_local;
		volume_id = le64toh(msg_local.volume_id);
		block_no = le64toh(msg_local.block_no);
		request_only = (le32toh(msg_local.flags) & HIFS_BLOCK_MSGF_REQUEST) != 0;

		if (request_only) {
			have_db = hifs_volume_block_load(volume_id, block_no,
				block_buf, &block_len);
			if (have_db) {
				msg_reply.flags = htole32(0);
				msg_reply.data_len = htole32(block_len);
			}
		} else {
			save_local = true;
			have_db = true;
			if (le32toh(msg_local.data_len) > 0) {
				ret = hicomm_comm_recv_data(fd, &data_frame, false);
				if (ret)
					return ret;
				incoming_len = data_frame.len;
				incoming_data = data_frame.data;
			}
		}

		if (save_local && have_db) {
			uint32_t store_len = incoming_len ? incoming_len : le32toh(msg_local.data_len);
			const uint8_t *store_buf = incoming_data ? incoming_data : block_buf;
			if (!hifs_volume_block_store(volume_id, block_no,
				 store_buf,
				 store_len)) {
				hifs_err("Failed to persist block %" PRIu64 " for volume %" PRIu64,
					 block_no, volume_id);
			}
		}
		if (!request_only)
			msg_reply.data_len = htole32(0);

		struct hifs_data_frame out_frame = {0};
		out_frame.len = sizeof(msg_reply);
		memcpy(out_frame.data, &msg_reply, sizeof(msg_reply));

		err = hicomm_comm_send_data(fd, &out_frame);
		if (err) {
			hifs_err("Failed to send block response data: %d", err);
			ret = err;
		}
		if (block_len > 0 && have_db) {
			struct hifs_data_frame data_out = {0};
			data_out.len = block_len;
			memcpy(data_out.data, block_buf, block_len);
			err = hicomm_comm_send_data(fd, &data_out);
			if (err) {
				hifs_err("Failed to send block data payload: %d", err);
				if (!ret)
					ret = err;
			}
		}
		err = hicomm_send_cmd_str(fd, HIFS_Q_PROTO_CMD_BLOCK_RECV);
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
