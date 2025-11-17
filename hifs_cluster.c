/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */



/**
 * HiveFS cluster/consistency stubs
 *
 * These are placeholders for future fencing/lease logic. For now they simply
 * succeed so VFS open/close work without additional wiring.
 */

#include "hifs.h"

int hifs_cluster_on_open(struct inode *inode, struct file *file)
{
	(void)inode;
	(void)file;
	return 0;
}

void hifs_cluster_on_close(struct inode *inode, struct file *file)
{
	(void)inode;
	(void)file;
	/* no-op for now */
}
