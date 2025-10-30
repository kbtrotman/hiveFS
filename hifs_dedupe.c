/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

#include "hifs.h"
#include <linux/uaccess.h>
#include <linux/math64.h>

int hifs_dedupe_writes(struct super_block *sb, uint64_t block,
		       const void *data, size_t len,
		       uint8_t hash_out[HIFS_BLOCK_HASH_SIZE])
{
	if (!sb || !data || !len || !hash_out)
		return -EINVAL;

	/* TODO: perform block-level deduplication before persisting. */
	memset(hash_out, 0, HIFS_BLOCK_HASH_SIZE);
	return 0;
}


int hifs_rehydrate_reads(struct super_block *sb, uint64_t block,
		       const void *data, size_t len,
		       uint8_t hash_out[HIFS_BLOCK_HASH_SIZE])
{

    /* TODO: perform block-level re-hydration of de-duped data before return to reading process. */
    return 0;

}