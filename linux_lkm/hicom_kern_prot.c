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
#include <linux/buffer_head.h>
#include <linux/uidgid.h>

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
    u32 r_flags = le32_to_cpu(remote->de_flags);
    u32 l_flags = le32_to_cpu(local->de_flags);

    if (r_flags & HIFS_DENTRY_MSGF_REQUEST)
        return 0;
    if (l_flags & HIFS_DENTRY_MSGF_REQUEST)
        return 1;

    u32 l_epoch = le32_to_cpu(local->de_epoch);
    u32 r_epoch = le32_to_cpu(remote->de_epoch);
    if (r_epoch != l_epoch)
        return (r_epoch > l_epoch) ? 1 : 0;
    return 0;
}

static void hifs_inode_host_to_wire(const struct hifs_inode *src,
                                    struct hifs_inode_wire *dst,
                                    u32 msg_flags)
{
    unsigned int i;

    memset(dst, 0, sizeof(*dst));
    dst->i_msg_flags = cpu_to_le32(msg_flags);
    dst->i_version = src->i_version;
    dst->i_flags = src->i_flags;
    dst->i_mode = cpu_to_le32(src->i_mode);
    dst->i_ino = cpu_to_le64(src->i_ino);
    dst->i_uid = cpu_to_le16(src->i_uid);
    dst->i_gid = cpu_to_le16(src->i_gid);
    dst->i_hrd_lnk = cpu_to_le16(src->i_hrd_lnk);
    dst->i_atime = cpu_to_le32(src->i_atime);
    dst->i_mtime = cpu_to_le32(src->i_mtime);
    dst->i_ctime = cpu_to_le32(src->i_ctime);
    dst->i_size = cpu_to_le32(src->i_size);
    memcpy(dst->i_name, src->i_name, sizeof(dst->i_name));
    for (i = 0; i < HIFS_INODE_TSIZE; ++i) {
        dst->extents[i].block_start = cpu_to_le32(src->extents[i].block_start);
        dst->extents[i].block_count = cpu_to_le32(src->extents[i].block_count);
    }
    dst->i_blocks = cpu_to_le32(src->i_blocks);
    dst->i_bytes = cpu_to_le32(src->i_bytes);
    dst->i_links = src->i_links;
    for (i = 0; i < HIFS_MAX_BLOCK_HASHES; ++i) {
        const struct hifs_block_fingerprint *fp = &src->i_block_fingerprints[i];
        struct hifs_block_fingerprint_wire *wire = &dst->i_block_fingerprints[i];

        wire->block_no = cpu_to_le32(fp->block_no);
        memcpy(wire->hash, fp->hash, sizeof(wire->hash));
        wire->hash_algo = fp->hash_algo;
        memset(wire->reserved, 0, sizeof(wire->reserved));
    }
    dst->i_hash_count = cpu_to_le16(src->i_hash_count);
    dst->i_hash_reserved = cpu_to_le16(src->i_hash_reserved);
}

static void hifs_inode_wire_to_host(const struct hifs_inode_wire *src,
                                    struct hifs_inode *dst)
{
    unsigned int i;

    memset(dst, 0, sizeof(*dst));
    /* msg_flags handled by caller */
    dst->i_version = src->i_version;
    dst->i_flags = src->i_flags;
    dst->i_mode = le32_to_cpu(src->i_mode);
    dst->i_ino = le64_to_cpu(src->i_ino);
    dst->i_uid = le16_to_cpu(src->i_uid);
    dst->i_gid = le16_to_cpu(src->i_gid);
    dst->i_hrd_lnk = le16_to_cpu(src->i_hrd_lnk);
    dst->i_atime = le32_to_cpu(src->i_atime);
    dst->i_mtime = le32_to_cpu(src->i_mtime);
    dst->i_ctime = le32_to_cpu(src->i_ctime);
    dst->i_size = le32_to_cpu(src->i_size);
    memcpy(dst->i_name, src->i_name, sizeof(dst->i_name));
    for (i = 0; i < HIFS_INODE_TSIZE; ++i) {
        dst->extents[i].block_start = le32_to_cpu(src->extents[i].block_start);
        dst->extents[i].block_count = le32_to_cpu(src->extents[i].block_count);
    }
    dst->i_blocks = le32_to_cpu(src->i_blocks);
    dst->i_bytes = le32_to_cpu(src->i_bytes);
    dst->i_links = src->i_links;
    for (i = 0; i < HIFS_MAX_BLOCK_HASHES; ++i) {
        struct hifs_block_fingerprint *fp = &dst->i_block_fingerprints[i];
        const struct hifs_block_fingerprint_wire *wire = &src->i_block_fingerprints[i];

        fp->block_no = le32_to_cpu(wire->block_no);
        memcpy(fp->hash, wire->hash, sizeof(fp->hash));
        fp->hash_algo = wire->hash_algo;
        memset(fp->reserved, 0, sizeof(fp->reserved));
    }
    dst->i_hash_count = le16_to_cpu(src->i_hash_count);
    dst->i_hash_reserved = le16_to_cpu(src->i_hash_reserved);
}

static int hifs_compare_inode_newer(const struct hifs_inode_wire *local,
                                    const struct hifs_inode_wire *remote)
{
    if (le32_to_cpu(remote->i_msg_flags) & HIFS_INODE_MSGF_REQUEST)
        return 0;
    if (le32_to_cpu(local->i_msg_flags) & HIFS_INODE_MSGF_REQUEST)
        return 1;

    u32 l_ctime = le32_to_cpu(local->i_ctime);
    u32 r_ctime = le32_to_cpu(remote->i_ctime);
    if (r_ctime != l_ctime)
        return (r_ctime > l_ctime) ? 1 : 0;

    u32 l_mtime = le32_to_cpu(local->i_mtime);
    u32 r_mtime = le32_to_cpu(remote->i_mtime);
    if (r_mtime != l_mtime)
        return (r_mtime > l_mtime) ? 1 : 0;

    u32 l_size = le32_to_cpu(local->i_size);
    u32 r_size = le32_to_cpu(remote->i_size);
    if (r_size != l_size)
        return (r_size > l_size) ? 1 : 0;

    return 0;
}

static void hifs_apply_remote_inode(struct super_block *sb,
                                    const struct hifs_inode_wire *remote)
{
    struct hifs_inode host_inode;
    struct inode *vfs_inode;

    if (!sb || !remote)
        return;

    hifs_inode_wire_to_host(remote, &host_inode);
    hifs_store_inode(sb, &host_inode);
    hifs_cache_mark_inode(sb, host_inode.i_ino);

    vfs_inode = ilookup(sb, host_inode.i_ino);
    if (vfs_inode) {
        struct hifs_inode *priv = vfs_inode->i_private;
        kuid_t uid = make_kuid(&init_user_ns, host_inode.i_uid);
        kgid_t gid = make_kgid(&init_user_ns, host_inode.i_gid);
        struct timespec64 ts;

        if (priv)
            memcpy(priv, &host_inode, sizeof(*priv));

        if (uid_valid(uid))
            vfs_inode->i_uid = uid;
        if (gid_valid(gid))
            vfs_inode->i_gid = gid;

        vfs_inode->i_mode = host_inode.i_mode;
        i_size_write(vfs_inode, host_inode.i_size);
        vfs_inode->i_blocks = host_inode.i_blocks;
        set_nlink(vfs_inode, host_inode.i_links);

        ts = (struct timespec64){ .tv_sec = host_inode.i_atime, .tv_nsec = 0 };
        inode_set_atime_to_ts(vfs_inode, ts);
        ts = (struct timespec64){ .tv_sec = host_inode.i_mtime, .tv_nsec = 0 };
        inode_set_mtime_to_ts(vfs_inode, ts);
        ts = (struct timespec64){ .tv_sec = host_inode.i_ctime, .tv_nsec = 0 };
        inode_set_ctime_to_ts(vfs_inode, ts);

        mark_inode_dirty(vfs_inode);
        iput(vfs_inode);
    }
}

static int hifs_apply_remote_block(struct super_block *sb,
                                   uint64_t block_no,
                                   const void *data,
                                   u32 len)
{
    struct buffer_head *bh;

    if (!sb || !data)
        return -EINVAL;

    if (len == 0 || len > HIFS_DEFAULT_BLOCK_SIZE)
        return -EINVAL;

    bh = sb_bread(sb, block_no);
    if (!bh)
        return -EIO;

    memcpy(bh->b_data, data, len);
    mark_buffer_dirty(bh);
    sync_dirty_buffer(bh);
    brelse(bh);

    hifs_cache_mark_present(sb, block_no);
    hifs_cache_clear_dirty(sb, block_no);
    return 0;
}

static void hifs_apply_remote_dentry(struct super_block *sb,
                                     struct hifs_sb_info *info,
                                     const struct hifs_volume_dentry *remote)
{
    char name_buf[HIFS_MAX_NAME_SIZE + 1];
    u32 name_len;
    u32 flags;

    struct inode *parent_inode = NULL;
    struct hifs_inode *parent_hifs = NULL;
    bool parent_hifs_owned = false;
    struct buffer_head *bh = NULL;
    struct hifs_dir_entry *dir_rec;
    u32 i;
    u64 parent_ino;

    if (!sb || !info || !remote)
        return;

    memset(name_buf, 0, sizeof(name_buf));
    name_len = min_t(u32, le32_to_cpu(remote->de_name_len), (u32)HIFS_MAX_NAME_SIZE);
    memcpy(name_buf, remote->de_name, name_len);
    flags = le32_to_cpu(remote->de_flags);

    if (flags & HIFS_DENTRY_MSGF_REQUEST)
        return;

    parent_ino = le64_to_cpu(remote->de_parent);

    hifs_notice("Remote dentry update volume %llu parent %llu inode %llu name '%s'",
                (unsigned long long)info->volume_id,
                (unsigned long long)parent_ino,
                (unsigned long long)le64_to_cpu(remote->de_inode),
                name_buf);

    parent_inode = ilookup(sb, parent_ino);
    if (parent_inode)
        parent_hifs = (struct hifs_inode *)parent_inode->i_private;

    if (!parent_hifs) {
        parent_hifs = hifs_iget(sb, parent_ino);
        if (!parent_hifs) {
            hifs_warning("Unable to locate parent inode %llu for remote dentry",
                         (unsigned long long)parent_ino);
            return;
        }
        parent_hifs_owned = true;
    }

    if (!parent_hifs) {
        hifs_warning("Parent inode %lu missing private data; cannot sync remote dentry",
                     parent_inode ? parent_inode->i_ino : (unsigned long)parent_ino);
        if (parent_inode)
            iput(parent_inode);
        if (parent_hifs_owned)
            cache_put_inode(&parent_hifs);
        return;
    }

    for (i = 0; i < HIFS_INODE_TSIZE; ++i) {
        u32 start = parent_hifs->extents[i].block_start;
        u32 count = parent_hifs->extents[i].block_count;
        u32 blk = start;
        u32 end = start + count;

        if (count == 0)
            continue;

        while (blk < end) {
            bh = sb_bread(sb, blk);
            if (!bh) {
                hifs_warning("Failed to read block %u for parent inode %lu",
                             blk, parent_inode->i_ino);
                break;
            }

            dir_rec = (struct hifs_dir_entry *)(bh->b_data);
            if (!dir_rec)
                goto next_block;

            for (unsigned int offset = 0; offset < sb->s_blocksize;
                 offset += sizeof(*dir_rec), dir_rec++) {
                if (dir_rec->inode_nr == le64_to_cpu(remote->de_inode) ||
                    (dir_rec->inode_nr == HIFS_EMPTY_ENTRY &&
                     dir_rec->name_len == 0)) {
                    bool new_slot = (dir_rec->inode_nr == HIFS_EMPTY_ENTRY);
                    dir_rec->inode_nr = (u32)le64_to_cpu(remote->de_inode);
                    dir_rec->name_len = name_len;
                    memset(dir_rec->name, 0, sizeof(dir_rec->name));
                    memcpy(dir_rec->name, name_buf, name_len);
                    mark_buffer_dirty(bh);
                    sync_dirty_buffer(bh);
                    brelse(bh);
                    bh = NULL;
                    if (new_slot) {
                        parent_hifs->i_size += sizeof(*dir_rec);
                        hifs_store_inode(sb, parent_hifs);
                        hifs_publish_inode(sb, parent_hifs, false);
                    }
                    hifs_cache_mark_dirent(sb, le64_to_cpu(remote->de_inode));
                    goto out;
                }
            }

next_block:
            brelse(bh);
            blk++;
        }
    }

    hifs_warning("Failed to update remote dentry '%s' for parent %llu (no slot found)",
                 name_buf, (unsigned long long)parent_ino);

out:
    if (parent_inode)
        iput(parent_inode);
    if (parent_hifs_owned)
        cache_put_inode(&parent_hifs);
    if (bh)
        brelse(bh);
}

int hifs_handshake_superblock(struct super_block *sb)
{
    struct hifs_sb_info *info;
    struct hifs_cmds cmd;
    struct hifs_sb_msg *msg_local = NULL;
    struct hifs_sb_msg *msg_remote = NULL;
    struct hifs_data_frame *frame = NULL;
    int ret = 0;

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

    msg_local = kzalloc(sizeof(*msg_local), GFP_KERNEL);
    if (!msg_local)
        return -ENOMEM;
    msg_remote = kzalloc(sizeof(*msg_remote), GFP_KERNEL);
    if (!msg_remote) {
        ret = -ENOMEM;
        goto out;
    }
    frame = kmalloc(sizeof(*frame), GFP_KERNEL);
    if (!frame) {
        ret = -ENOMEM;
        goto out;
    }

    /* Build and send local per-volume super */
    memset(msg_local, 0, sizeof(*msg_local));
    msg_local->volume_id = cpu_to_le64(info->volume_id);
    msg_local->vsb = info->vol_super;

    ret = hifs_data_fifo_out_push_buf(msg_local, sizeof(*msg_local));
    if (ret)
        goto out;

    /* Wait for an optional response: SB_RECV + payload of superblock */
    {
        int tries;
        for (tries = 0; tries < 20; tries++) {
            int pop_ret;

            memset(frame, 0, sizeof(*frame));
            pop_ret = hifs_cmd_fifo_in_pop(&cmd, true);
            if (pop_ret == -ERESTARTSYS) {
                ret = pop_ret;
                goto out;
            }
            if (pop_ret)
                continue;

            if (cmd_equals(&cmd, HIFS_Q_PROTO_CMD_SB_RECV)) {
                ret = hifs_data_fifo_in_pop(frame, false);
                if (ret)
                    goto out;
                if (frame->len != sizeof(*msg_remote)) {
                    ret = -EINVAL;
                    goto out;
                }
                memcpy(msg_remote, frame->data, sizeof(*msg_remote));
                break;
            }
        }
        if (tries == 20) {
            ret = 0; /* No response; assume local is fine */
            goto out;
        }
    }

    /* Compare and update if remote is newer */
    if (hifs_compare_sb_newer(&msg_local->vsb, &msg_remote->vsb) > 0) {
        /* Update per-volume logical super and persist in volume table */
        info->vol_super = msg_remote->vsb;
        hifs_volume_save(sb, info);
        /* continue to root handshake even if super updated */
    }
    ret = hifs_handshake_rootdentry(sb);

out:
    kfree(frame);
    kfree(msg_remote);
    kfree(msg_local);
    return ret;
}

int hifs_handshake_rootdentry(struct super_block *sb)
{
    struct hifs_sb_info *info;
    struct hifs_cmds cmd;
    struct hifs_root_msg *msg_local = NULL;
    struct hifs_root_msg *msg_remote = NULL;
    struct hifs_data_frame *frame = NULL;
    int ret = 0;

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

    msg_local = kzalloc(sizeof(*msg_local), GFP_KERNEL);
    if (!msg_local)
        return -ENOMEM;
    msg_remote = kzalloc(sizeof(*msg_remote), GFP_KERNEL);
    if (!msg_remote) {
        ret = -ENOMEM;
        goto out;
    }
    frame = kmalloc(sizeof(*frame), GFP_KERNEL);
    if (!frame) {
        ret = -ENOMEM;
        goto out;
    }

    memset(msg_local, 0, sizeof(*msg_local));
    msg_local->volume_id = cpu_to_le64(info->volume_id);
    msg_local->root = info->root_dentry;

    ret = hifs_data_fifo_out_push_buf(msg_local, sizeof(*msg_local));
    if (ret)
        goto out;

    {
        int tries;
        for (tries = 0; tries < 20; tries++) {
            int pop_ret;

            memset(frame, 0, sizeof(*frame));
            pop_ret = hifs_cmd_fifo_in_pop(&cmd, true);
            if (pop_ret == -ERESTARTSYS) {
                ret = pop_ret;
                goto out;
            }
            if (pop_ret)
                continue;

            if (cmd_equals(&cmd, HIFS_Q_PROTO_CMD_ROOT_RECV)) {
                ret = hifs_data_fifo_in_pop(frame, false);
                if (ret)
                    goto out;
                if (frame->len != sizeof(*msg_remote)) {
                    ret = -EINVAL;
                    goto out;
                }
                memcpy(msg_remote, frame->data, sizeof(*msg_remote));
                break;
            }
        }
        if (tries == 20) {
            ret = 0;
            goto out;
        }
    }

    if (hifs_compare_root_newer(&info->root_dentry, &msg_remote->root) > 0) {
        info->root_dentry = msg_remote->root;
        hifs_volume_save(sb, info);
        ret = 1;
        goto out;
    }
    ret = 0;

out:
    kfree(frame);
    kfree(msg_remote);
    kfree(msg_local);
    return ret;
}

int hifs_publish_dentry(struct super_block *sb, uint64_t parent_ino, uint64_t child_ino,
			const char *name, u32 name_len, u32 type, bool request_only)
{
    struct hifs_sb_info *info;
    struct hifs_cmds cmd;
    struct hifs_dentry_msg *msg_local = NULL;
    struct hifs_dentry_msg *msg_remote = NULL;
    struct hifs_data_frame *frame = NULL;
    u32 copy_len;
    int ret = 0;

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

    msg_local = kzalloc(sizeof(*msg_local), GFP_KERNEL);
    if (!msg_local)
        return -ENOMEM;
    msg_remote = kzalloc(sizeof(*msg_remote), GFP_KERNEL);
    if (!msg_remote) {
        ret = -ENOMEM;
        goto out;
    }
    frame = kmalloc(sizeof(*frame), GFP_KERNEL);
    if (!frame) {
        ret = -ENOMEM;
        goto out;
    }

    msg_local->volume_id = cpu_to_le64(info->volume_id);
    msg_local->dentry.de_flags = cpu_to_le32(request_only ? HIFS_DENTRY_MSGF_REQUEST : 0);
    msg_local->dentry.de_parent = cpu_to_le64(parent_ino);
    msg_local->dentry.de_inode = cpu_to_le64(child_ino);
    msg_local->dentry.de_epoch = cpu_to_le32((u32)GET_TIME());
    msg_local->dentry.de_type = cpu_to_le32(type);
    copy_len = min_t(u32, name_len, (u32)sizeof(msg_local->dentry.de_name));
    memcpy(msg_local->dentry.de_name, name, copy_len);
    msg_local->dentry.de_name_len = cpu_to_le32(copy_len);

    ret = hifs_data_fifo_out_push_buf(msg_local, sizeof(*msg_local));
    if (ret)
        goto out;

    hifs_debug("publish dentry %llu/%llu name %.*s flags %#x",
               (unsigned long long)parent_ino,
               (unsigned long long)child_ino,
               name_len > 16 ? 16 : name_len, name,
               request_only ? HIFS_DENTRY_MSGF_REQUEST : 0);

    {
        int tries;
        for (tries = 0; tries < 20; tries++) {
            int pop_ret;

            memset(frame, 0, sizeof(*frame));
            pop_ret = hifs_cmd_fifo_in_pop(&cmd, true);
            if (pop_ret == -ERESTARTSYS) {
                ret = pop_ret;
                goto out;
            }
            if (pop_ret)
                continue;

            if (cmd_equals(&cmd, HIFS_Q_PROTO_CMD_DENTRY_RECV)) {
                ret = hifs_data_fifo_in_pop(frame, false);
                if (ret)
                    goto out;
                if (frame->len != sizeof(*msg_remote)) {
                    ret = -EINVAL;
                    goto out;
                }
                memcpy(msg_remote, frame->data, sizeof(*msg_remote));
                break;
            }
        }
        if (tries == 20) {
            ret = 0;
            goto out;
        }
    }

    if (hifs_compare_dentry_newer(&msg_local->dentry, &msg_remote->dentry) > 0) {
        hifs_apply_remote_dentry(sb, info, &msg_remote->dentry);
        ret = 1;
        goto out;
    }
    ret = 0;

out:
    kfree(frame);
    kfree(msg_remote);
    kfree(msg_local);
    return ret;
}

int hifs_publish_inode(struct super_block *sb, const struct hifs_inode *hii,
                       bool request_only)
{
    struct hifs_sb_info *info;
    struct hifs_cmds cmd;
    struct hifs_inode_msg *msg_local = NULL;
    struct hifs_inode_msg *msg_remote = NULL;
    struct hifs_inode_wire *wire_local = NULL;
    struct hifs_data_frame *frame = NULL;
    int ret = 0;

    if (!sb || !hii) {
        hifs_err("publish inode: invalid args sb=%p hii=%p", sb, hii);
        return -EINVAL;
    }

    info = (struct hifs_sb_info *)sb->s_fs_info;
    if (!info) {
        hifs_err("publish inode %llu: missing sb info", hii->i_ino);
        return -EINVAL;
    }

    wire_local = kzalloc(sizeof(*wire_local), GFP_KERNEL);
    if (!wire_local)
        return -ENOMEM;

    hifs_inode_host_to_wire(hii, wire_local,
                            request_only ? HIFS_INODE_MSGF_REQUEST : 0);

    memset(&cmd, 0, sizeof(cmd));
    strscpy(cmd.cmd, HIFS_Q_PROTO_CMD_INODE_SEND, sizeof(cmd.cmd));
    cmd.count = strlen(cmd.cmd);
    ret = hifs_cmd_fifo_out_push(&cmd);
    if (ret) {
        hifs_err("publish inode %llu: cmd enqueue failed rc=%d", hii->i_ino, ret);
        goto out;
    }

    msg_local = kzalloc(sizeof(*msg_local), GFP_KERNEL);
    if (!msg_local) {
        ret = -ENOMEM;
        goto out;
    }
    msg_remote = kzalloc(sizeof(*msg_remote), GFP_KERNEL);
    if (!msg_remote) {
        ret = -ENOMEM;
        goto out;
    }
    frame = kmalloc(sizeof(*frame), GFP_KERNEL);
    if (!frame) {
        ret = -ENOMEM;
        goto out;
    }

    msg_local->volume_id = cpu_to_le64(info->volume_id);
    msg_local->inode = *wire_local;

    hifs_debug("publish inode %llu flags %#x", hii->i_ino,
               request_only ? HIFS_INODE_MSGF_REQUEST : 0);

    ret = hifs_data_fifo_out_push_buf(msg_local, sizeof(*msg_local));
    if (ret) {
        hifs_err("publish inode %llu: data enqueue failed rc=%d", hii->i_ino, ret);
        goto out;
    }

    {
        int tries;
        for (tries = 0; tries < 20; tries++) {
            int pop_ret;

            memset(frame, 0, sizeof(*frame));
            pop_ret = hifs_cmd_fifo_in_pop(&cmd, true);
            if (pop_ret == -ERESTARTSYS) {
                ret = pop_ret;
                goto out;
            }
            if (pop_ret)
                continue;

            if (cmd_equals(&cmd, HIFS_Q_PROTO_CMD_INODE_RECV)) {
                ret = hifs_data_fifo_in_pop(frame, false);
                if (ret) {
                    hifs_err("publish inode %llu: recv frame failed rc=%d",
                             hii->i_ino, ret);
                    goto out;
                }
                if (frame->len != sizeof(*msg_remote)) {
                    ret = -EINVAL;
                    hifs_err("publish inode %llu: recv frame len %u expected %zu",
                             hii->i_ino, frame->len, sizeof(*msg_remote));
                    goto out;
                }
                memcpy(msg_remote, frame->data, sizeof(*msg_remote));
                break;
            }
        }
        if (tries == 20) {
            hifs_warning("publish inode %llu: no response after %d attempts (flags %#x)",
                         hii->i_ino, tries,
                         request_only ? HIFS_INODE_MSGF_REQUEST : 0);
            ret = 0;
            goto out;
        }
    }

    if (hifs_compare_inode_newer(&msg_local->inode, &msg_remote->inode) > 0) {
        hifs_debug("publish inode %llu: remote newer, applying", hii->i_ino);
        hifs_apply_remote_inode(sb, &msg_remote->inode);
        ret = 1;
        goto out;
    }

    hifs_debug("publish inode %llu: local accepted (flags %#x)",
               hii->i_ino, request_only ? HIFS_INODE_MSGF_REQUEST : 0);
    ret = 0;

out:
    kfree(frame);
    kfree(msg_remote);
    kfree(msg_local);
    kfree(wire_local);
    return ret;
}

int hifs_publish_block(struct super_block *sb, uint64_t block_no,
                       const void *data, u32 data_len, bool request_only,
                       u32 flags, const uint8_t *hash,
		       enum hifs_hash_algorithm hash_algo)
{
    struct hifs_sb_info *info;
    struct hifs_cmds cmd;
    struct hifs_block_msg *msg_local = NULL;
    struct hifs_block_msg *msg_remote = NULL;
    struct hifs_data_frame *frame = NULL;
    int ret = 0;

    if (!sb)
        return -EINVAL;
    if (!request_only && (!data || data_len == 0 || !hash))
        return -EINVAL;
    if (data_len > HIFS_DEFAULT_BLOCK_SIZE)
        return -EINVAL;

    info = (struct hifs_sb_info *)sb->s_fs_info;
    if (!info)
        return -EINVAL;

    memset(&cmd, 0, sizeof(cmd));
    strscpy(cmd.cmd, HIFS_Q_PROTO_CMD_BLOCK_SEND, sizeof(cmd.cmd));
    cmd.count = strlen(cmd.cmd);
    ret = hifs_cmd_fifo_out_push(&cmd);
    if (ret)
        return ret;

    msg_local = kzalloc(sizeof(*msg_local), GFP_KERNEL);
    if (!msg_local)
        return -ENOMEM;
    msg_remote = kzalloc(sizeof(*msg_remote), GFP_KERNEL);
    if (!msg_remote) {
        ret = -ENOMEM;
        goto out;
    }
    frame = kmalloc(sizeof(*frame), GFP_KERNEL);
    if (!frame) {
        ret = -ENOMEM;
        goto out;
    }

    msg_local->volume_id = cpu_to_le64(info->volume_id);
    msg_local->block_no = cpu_to_le64(block_no);
    msg_local->flags = cpu_to_le32((request_only ? HIFS_BLOCK_MSGF_REQUEST : 0) |
                                   (flags & (HIFS_BLOCK_MSGF_CONTIG_START |
                                             HIFS_BLOCK_MSGF_CONTIG_END)));
    msg_local->data_len = cpu_to_le32(request_only ? 0 : data_len);
    if (!request_only && hash) {
        msg_local->hash_algo = hash_algo;
        memcpy(msg_local->hash, hash, sizeof(msg_local->hash));
    } else {
        msg_local->hash_algo = HIFS_HASH_ALGO_NONE;
        memset(msg_local->hash, 0, sizeof(msg_local->hash));
    }

    hifs_debug("publish block %llu len %u flags %#x",
               (unsigned long long)block_no, data_len,
               request_only ? HIFS_BLOCK_MSGF_REQUEST : 0);

    ret = hifs_data_fifo_out_push_buf(msg_local, sizeof(*msg_local));
    if (ret)
        goto out;

    if (!request_only && data_len) {
        ret = hifs_data_fifo_out_push_buf(data, data_len);
        if (ret)
            goto out;
    }

    {
        int tries;
        for (tries = 0; tries < 20; tries++) {
            int pop_ret;

            memset(frame, 0, sizeof(*frame));
            pop_ret = hifs_cmd_fifo_in_pop(&cmd, true);
            if (pop_ret == -ERESTARTSYS) {
                ret = pop_ret;
                goto out;
            }
            if (pop_ret)
                continue;

            if (cmd_equals(&cmd, HIFS_Q_PROTO_CMD_BLOCK_RECV)) {
                struct hifs_data_frame *data_frame = NULL;
                u32 r_len;

                ret = hifs_data_fifo_in_pop(frame, false);
                if (ret)
                    goto out;

                memset(msg_remote, 0, sizeof(*msg_remote));
                if (frame->len != sizeof(*msg_remote)) {
                    ret = -EINVAL;
                    goto out;
                }

                memcpy(msg_remote, frame->data, sizeof(*msg_remote));
                r_len = le32_to_cpu(msg_remote->data_len);
                if (r_len > 0) {
                    data_frame = kmalloc(sizeof(*data_frame), GFP_KERNEL);
                    if (!data_frame) {
                        ret = -ENOMEM;
                        goto out;
                    }
                    ret = hifs_data_fifo_in_pop(data_frame, false);
                    if (ret) {
                        kfree(data_frame);
                        goto out;
                    }
                    if (data_frame->len != r_len) {
                        kfree(data_frame);
                        ret = -EINVAL;
                        goto out;
                    }
                    ret = hifs_apply_remote_block(sb,
                                                  le64_to_cpu(msg_remote->block_no),
                                                  data_frame->data, r_len);
                    kfree(data_frame);
                    if (!ret) {
                        ret = 1;
                        goto out;
                    }
                    goto out;
                }
                break;
            }
        }
        if (tries == 20) {
            ret = 0;
            goto out;
        }
    }

    ret = 0;

out:
    kfree(frame);
    kfree(msg_remote);
    kfree(msg_local);
    return ret;
}
struct hifs_flush_ctx {
	int rc;
};

static void hifs_flush_sync_super(struct super_block *sb, void *data)
{
	struct hifs_flush_ctx *ctx = data;
	int ret;

	if (!sb || sb->s_type != &hifs_type)
		return;

	ret = sync_filesystem(sb);
	if (ret && !ctx->rc)
		ctx->rc = ret;
}

int hifs_flush_dirty_cache_items(void)
{
	struct hifs_flush_ctx ctx = { .rc = 0 };

	hifs_debug("flush dirty cache items: begin");
	iterate_supers_type(&hifs_type, hifs_flush_sync_super, &ctx);
	if (ctx.rc)
		hifs_warning("flush dirty cache items: sync_filesystem rc=%d", ctx.rc);
	else
		hifs_debug("flush dirty cache items: complete");
	return ctx.rc;
}
