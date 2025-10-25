/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

#include "hifs_shared_defs.h"
#include "hifs.h"

#include <linux/string.h>

extern struct hifs_link hifs_kern_link;
extern struct hifs_link hifs_user_link;

static void hifs_link_set_state(struct hifs_link *link, enum hifs_link_state state)
{
	if (!link)
		return;

	link->last_state = link->state;
	link->state = state;
	link->last_check = GET_TIME();
	if (!link->clockstart)
		link->clockstart = GET_TIME();
}

int hifs_create_test_inode(void)
{
	struct hifs_inode inode;
	int ret;

	memset(&inode, 0, sizeof(inode));

	inode.i_version = 1;
	inode.i_mode = S_IFREG | 0644;
	inode.i_uid = 0;
	inode.i_gid = 0;
	inode.i_blocks = 1;
	inode.i_bytes = 512;
	inode.i_size = 512;
	inode.i_ino = 1;
	strscpy(inode.i_name, "test_inode", sizeof(inode.i_name));

    ret = hifs_cmd_fifo_out_push_cstr(HIFS_Q_PROTO_CMD_TEST);
	if (ret)
		return ret;

    /* Test Example: send inode as raw payload via data FIFO if desired */
    ret = hifs_data_fifo_out_push_buf(&inode, sizeof(inode));
	if (ret)
		return ret;

	hifs_info("Queued test inode and command\n");
	return 0;
}

void hifs_comm_link_notify_online(void)
{
	hifs_link_set_state(&hifs_kern_link, HIFS_COMM_LINK_UP);
	hifs_kern_link.remote_state = HIFS_COMM_LINK_DOWN;
	hifs_info("Kernel communication link marked online\n");
}

void hifs_comm_link_notify_offline(void)
{
	hifs_link_set_state(&hifs_kern_link, HIFS_COMM_LINK_DOWN);
	hifs_info("Kernel communication link marked offline\n");
}

/* Superblock comparison/handshake. */
static bool cmd_equals(const struct hifs_cmds *cmd, const char *name)
{
    size_t len;
    if (!cmd || !name)
        return false;
    len = strnlen(cmd->cmd, HIFS_MAX_CMD_SIZE);
    return len == strlen(name) && strncmp(cmd->cmd, name, HIFS_MAX_CMD_SIZE) == 0;
}

/* Return >0 if remote newer, 0 if local is up-to-date or equal, <0 on error */
static int hifs_compare_sb_newer(const struct hifs_superblock *local,
                                 const struct hifs_superblock *remote)
{
    u32 l_gen = le32_to_cpu(local->s_rev_level);
    u32 r_gen = le32_to_cpu(remote->s_rev_level);
    if (r_gen != l_gen)
        return (r_gen > l_gen) ? 1 : 0;

    u32 l_wtime = le32_to_cpu(local->s_wtime);
    u32 r_wtime = le32_to_cpu(remote->s_wtime);
    if (r_wtime != l_wtime)
        return (r_wtime > l_wtime) ? 1 : 0;
    return 0;
}

int hifs_handshake_superblock(struct super_block *sb)
{
    struct hifs_sb_info *info;
    struct hifs_cmds cmd;
    struct hifs_sb_msg msg_local;
    struct hifs_sb_msg msg_remote;
    int ret;

    if (!sb)
        return -EINVAL;
    info = (struct hifs_sb_info *)sb->s_fs_info;
    if (!info)
        return -EINVAL;

    memset(&cmd, 0, sizeof(cmd));
    strscpy(cmd.cmd, HIFS_Q_PROTO_CMD_SB_SEND, sizeof(cmd.cmd));
    cmd.count = strlen(cmd.cmd);
    ret = hifs_cmd_fifo_out_push(&cmd);
    if (ret)
        return ret;

    /* Build and send local per-volume super */
    memset(&msg_local, 0, sizeof(msg_local));
    msg_local.volume_id = cpu_to_le64(info->volume_id);
    msg_local.vsb = info->vol_super;
    strscpy(msg_local.vsb.s_volume_name, "", sizeof(msg_local.vsb.s_volume_name));

    ret = hifs_data_fifo_out_push_buf(&msg_local, sizeof(msg_local));
    if (ret)
        return ret;

    /* Wait for an optional response: SB_RECV + payload of superblock */
    {
        int tries;
        for (tries = 0; tries < 20; tries++) {
            if (!hifs_cmd_fifo_in_pop(&cmd, true)) {
                if (cmd_equals(&cmd, HIFS_Q_PROTO_CMD_SB_RECV)) {
                    struct hifs_data_frame frame;
                    ret = hifs_data_fifo_in_pop(&frame, false);
                    if (ret)
                        return ret;
                    if (frame.len != sizeof(msg_remote))
                        return -EINVAL;
                    memcpy(&msg_remote, frame.data, sizeof(msg_remote));
                    break;
                }
            }
            schedule_timeout_interruptible(msecs_to_jiffies(50));
        }
        if (tries == 20)
            return 0; /* No response; assume local is fine */
    }

    /* Compare and update if remote is newer */
    if (hifs_compare_sb_newer(&msg_local.vsb, &msg_remote.vsb) > 0) {
        /* Update per-volume logical super and persist in volume table */
        info->vol_super = msg_remote.vsb;
        hifs_volume_save(sb, info);
        return 1; /* updated */
    }
    return 0; /* no change */
}

int hifs_flush_dirty_cache_items(void)
{

	return 0;
}
