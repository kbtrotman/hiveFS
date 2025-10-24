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

    ret = hifs_inode_fifo_out_push_from_inode(&inode);
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
