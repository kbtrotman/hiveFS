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
#include <linux/minmax.h>

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
static int hifs_compare_sb_newer(const struct hifs_volume_superblock *local,
                                 const struct hifs_volume_superblock *remote)
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

static int hifs_compare_root_newer(const struct hifs_volume_root_dentry *local,
                                   const struct hifs_volume_root_dentry *remote)
{
    u32 l_ctime = le32_to_cpu(local->rd_ctime);
    u32 r_ctime = le32_to_cpu(remote->rd_ctime);
    if (r_ctime != l_ctime)
        return (r_ctime > l_ctime) ? 1 : 0;

    u32 l_mtime = le32_to_cpu(local->rd_mtime);
    u32 r_mtime = le32_to_cpu(remote->rd_mtime);
    if (r_mtime != l_mtime)
        return (r_mtime > l_mtime) ? 1 : 0;

    u64 l_size = le64_to_cpu(local->rd_size);
    u64 r_size = le64_to_cpu(remote->rd_size);
    if (r_size != l_size)
        return (r_size > l_size) ? 1 : 0;

    return 0;
}

static int hifs_compare_dentry_newer(const struct hifs_volume_dentry *local,
                                     const struct hifs_volume_dentry *remote)
{
    u32 l_epoch = le32_to_cpu(local->de_epoch);
    u32 r_epoch = le32_to_cpu(remote->de_epoch);
    if (r_epoch != l_epoch)
        return (r_epoch > l_epoch) ? 1 : 0;
    return 0;
}

static void hifs_apply_remote_dentry(struct super_block *sb,
                                     struct hifs_sb_info *info,
                                     const struct hifs_volume_dentry *remote)
{
    char name_buf[HIFS_MAX_NAME_SIZE + 1];
    u32 name_len;

    if (!sb || !info || !remote)
        return;

    memset(name_buf, 0, sizeof(name_buf));
    name_len = min_t(u32, le32_to_cpu(remote->de_name_len), (u32)HIFS_MAX_NAME_SIZE);
    memcpy(name_buf, remote->de_name, name_len);

    hifs_notice("Remote dentry update volume %llu parent %llu inode %llu name '%s' (deferred apply)",
                (unsigned long long)info->volume_id,
                (unsigned long long)le64_to_cpu(remote->de_parent),
                (unsigned long long)le64_to_cpu(remote->de_inode),
                name_buf);
    /* TODO: apply remote dentry to cache storage. */
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
        /* continue to root handshake even if super updated */
    }
    return hifs_handshake_rootdentry(sb);
}

int hifs_handshake_rootdentry(struct super_block *sb)
{
    struct hifs_sb_info *info;
    struct hifs_cmds cmd;
    struct hifs_root_msg msg_local;
    struct hifs_root_msg msg_remote;
    int ret;

    if (!sb)
        return -EINVAL;
    info = (struct hifs_sb_info *)sb->s_fs_info;
    if (!info)
        return -EINVAL;

    memset(&cmd, 0, sizeof(cmd));
    strscpy(cmd.cmd, HIFS_Q_PROTO_CMD_ROOT_SEND, sizeof(cmd.cmd));
    cmd.count = strlen(cmd.cmd);
    ret = hifs_cmd_fifo_out_push(&cmd);
    if (ret)
        return ret;

    memset(&msg_local, 0, sizeof(msg_local));
    msg_local.volume_id = cpu_to_le64(info->volume_id);
    msg_local.root = info->root_dentry;

    ret = hifs_data_fifo_out_push_buf(&msg_local, sizeof(msg_local));
    if (ret)
        return ret;

    {
        int tries;
        for (tries = 0; tries < 20; tries++) {
            if (!hifs_cmd_fifo_in_pop(&cmd, true)) {
                if (cmd_equals(&cmd, HIFS_Q_PROTO_CMD_ROOT_RECV)) {
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
            return 0;
    }

    if (hifs_compare_root_newer(&info->root_dentry, &msg_remote.root) > 0) {
        info->root_dentry = msg_remote.root;
        hifs_volume_save(sb, info);
        return 1;
    }
    return 0;
}

int hifs_publish_dentry(struct super_block *sb, uint64_t parent_ino, uint64_t child_ino,
			const char *name, u32 name_len, u32 type)
{
    struct hifs_sb_info *info;
    struct hifs_cmds cmd;
    struct hifs_dentry_msg msg_local;
    struct hifs_dentry_msg msg_remote;
    u32 copy_len;
    int ret;

    if (!sb || !name)
        return -EINVAL;
    info = (struct hifs_sb_info *)sb->s_fs_info;
    if (!info)
        return -EINVAL;

    memset(&cmd, 0, sizeof(cmd));
    strscpy(cmd.cmd, HIFS_Q_PROTO_CMD_DENTRY_SEND, sizeof(cmd.cmd));
    cmd.count = strlen(cmd.cmd);
    ret = hifs_cmd_fifo_out_push(&cmd);
    if (ret)
        return ret;

    memset(&msg_local, 0, sizeof(msg_local));
    msg_local.volume_id = cpu_to_le64(info->volume_id);
    msg_local.dentry.de_parent = cpu_to_le64(parent_ino);
    msg_local.dentry.de_inode = cpu_to_le64(child_ino);
    msg_local.dentry.de_epoch = cpu_to_le32((u32)GET_TIME());
    msg_local.dentry.de_type = cpu_to_le32(type);
    copy_len = min_t(u32, name_len, (u32)sizeof(msg_local.dentry.de_name));
    memcpy(msg_local.dentry.de_name, name, copy_len);
    msg_local.dentry.de_name_len = cpu_to_le32(copy_len);

    ret = hifs_data_fifo_out_push_buf(&msg_local, sizeof(msg_local));
    if (ret)
        return ret;

    {
        int tries;
        for (tries = 0; tries < 20; tries++) {
            if (!hifs_cmd_fifo_in_pop(&cmd, true)) {
                if (cmd_equals(&cmd, HIFS_Q_PROTO_CMD_DENTRY_RECV)) {
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
            return 0;
    }

    if (hifs_compare_dentry_newer(&msg_local.dentry, &msg_remote.dentry) > 0) {
        hifs_apply_remote_dentry(sb, info, &msg_remote.dentry);
        return 1;
    }
    return 0;
}

int hifs_flush_dirty_cache_items(void)
{

	return 0;
}
