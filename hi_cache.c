/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

#include "hifs.h"

struct hifs_inode *cache_get_inode(void)
{
	struct hifs_inode *hii;

	hii = kmem_cache_alloc(hifs_inode_cache, GFP_KERNEL);
	printk(KERN_INFO "#: hifs cache_get_inode : di=%p\n", hii);

	return hii;
}

void cache_put_inode(struct hifs_inode **hii)
{
	printk(KERN_INFO "#: hifs cache_put_inode : di=%p\n", *hii);
	kmem_cache_free(hifs_inode_cache, *hii);
	*hii = NULL;
}