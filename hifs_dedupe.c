/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

#include "hifs.h"
#include <linux/slab.h>
#include <linux/hashtable.h>
#include <linux/mutex.h>
#include <linux/random.h>
#include <linux/ktime.h>
#include <linux/err.h>
#include <linux/byteorder/generic.h>
#include <linux/string.h>
#include <linux/siphash.h>
#include <linux/printk.h>
#include <crypto/hash.h>
#include <crypto/sha2.h>

#define HIFS_DEDUPE_HASH_BITS 11

struct hifs_dedupe_entry {
	struct hlist_node hnode;
	u64 block_no;
	u64 last_update_ns;
	bool dirty;
	bool have_hash;
	u8 algo;
	u8 hash[HIFS_BLOCK_HASH_SIZE];
};

struct hifs_dedupe_ctx {
	struct mutex lock;
	DECLARE_HASHTABLE(entries, HIFS_DEDUPE_HASH_BITS);
	struct crypto_shash *shash;
	siphash_key_t fallback_key_primary;
	siphash_key_t fallback_key_secondary;
};

static inline struct hifs_dedupe_ctx *hifs_sb_dedupe_ctx(struct super_block *sb)
{
	struct hifs_sb_info *info = sb ? (struct hifs_sb_info *)sb->s_fs_info : NULL;
	return info ? info->dedupe : NULL;
}

static struct hifs_dedupe_entry *hifs_dedupe_find_locked(struct hifs_dedupe_ctx *ctx,
							 u64 block_no)
{
	struct hifs_dedupe_entry *entry;

	hash_for_each_possible(ctx->entries, entry, hnode, block_no) {
		if (entry->block_no == block_no)
			return entry;
	}
	return NULL;
}

static void hifs_dedupe_store_u128(u8 dst[HIFS_BLOCK_HASH_SIZE], u64 h1, u64 h2)
{
	__le64 le1 = cpu_to_le64(h1);
	__le64 le2 = cpu_to_le64(h2);

	memcpy(dst, &le1, sizeof(le1));
	memcpy(dst + sizeof(le1), &le2, sizeof(le2));
}

static int hifs_dedupe_hash_block(struct hifs_dedupe_ctx *ctx, const void *data,
				  size_t len, u8 out[HIFS_BLOCK_HASH_SIZE],
				  enum hifs_hash_algorithm *algo_out)
{
	u8 digest[SHA256_DIGEST_SIZE];
	siphash_key_t key_a, key_b;
	u64 h1, h2;
	int ret;

	if (!data || !len || !out)
		return -EINVAL;

	if (ctx && ctx->shash) {
		SHASH_DESC_ON_STACK(desc, ctx->shash);

		desc->tfm = ctx->shash;
		ret = crypto_shash_digest(desc, data, len, digest);
		if (ret)
			return ret;

		memcpy(out, digest, HIFS_BLOCK_HASH_SIZE);
		memzero_explicit(digest, sizeof(digest));
		if (algo_out)
			*algo_out = HIFS_HASH_ALGO_SHA256;
		return 0;
	}

	if (ctx) {
		key_a = ctx->fallback_key_primary;
		key_b = ctx->fallback_key_secondary;
	} else {
		static const siphash_key_t default_key_a = {
			.key = { 0x7c1e9d6a0b4f25d1ULL, 0x51a8f2c3b476e980ULL }
		};
		static const siphash_key_t default_key_b = {
			.key = { 0x3141592653589793ULL, 0x2718281828459045ULL }
		};

		key_a = default_key_a;
		key_b = default_key_b;
	}

	h1 = siphash(data, len, &key_a);
	h2 = siphash(data, len, &key_b);

	hifs_dedupe_store_u128(out, h1, h2);
	if (algo_out)
		*algo_out = HIFS_HASH_ALGO_SIPHASH;
	return 0;
}

static int hifs_dedupe_update_entry(struct hifs_dedupe_ctx *ctx, u64 block_no,
				      const u8 hash[HIFS_BLOCK_HASH_SIZE],
				      enum hifs_hash_algorithm algo,
				      bool *changed)
{
	struct hifs_dedupe_entry *entry;
	u64 now_ns = ktime_get_coarse_ns();
	bool local_changed = true;
	int ret = 0;

	if (!ctx)
		return 0;

	mutex_lock(&ctx->lock);
	entry = hifs_dedupe_find_locked(ctx, block_no);
	if (!entry) {
		entry = kzalloc(sizeof(*entry), GFP_KERNEL);
		if (!entry) {
			ret = -ENOMEM;
			goto out_unlock;
		}
		entry->block_no = block_no;
		entry->dirty = true;
		entry->have_hash = true;
		entry->algo = algo;
		entry->last_update_ns = now_ns;
		memcpy(entry->hash, hash, HIFS_BLOCK_HASH_SIZE);
		hash_add(ctx->entries, &entry->hnode, block_no);
		hifs_debug("dedupe: track block %llu", (unsigned long long)block_no);
	} else {
		if (entry->have_hash && entry->algo == algo &&
		    !memcmp(entry->hash, hash, HIFS_BLOCK_HASH_SIZE)) {
			local_changed = false;
			entry->dirty = false;
		} else {
			memcpy(entry->hash, hash, HIFS_BLOCK_HASH_SIZE);
			entry->dirty = true;
			entry->have_hash = true;
			entry->algo = algo;
		}
		entry->last_update_ns = now_ns;
	}

out_unlock:
	mutex_unlock(&ctx->lock);
	if (!ret && changed)
		*changed = local_changed;
	return ret;
}

static void hifs_dedupe_note_read(struct hifs_dedupe_ctx *ctx, u64 block_no,
			  const u8 hash[HIFS_BLOCK_HASH_SIZE],
			  enum hifs_hash_algorithm algo)
{
	struct hifs_dedupe_entry *entry;
	u64 now_ns = ktime_get_coarse_ns();

	if (!ctx)
		return;

	mutex_lock(&ctx->lock);
	entry = hifs_dedupe_find_locked(ctx, block_no);
	if (!entry) {
		entry = kzalloc(sizeof(*entry), GFP_KERNEL);
		if (!entry)
			goto out_unlock;
		entry->block_no = block_no;
		entry->dirty = false;
		entry->have_hash = true;
		entry->last_update_ns = now_ns;
		memcpy(entry->hash, hash, HIFS_BLOCK_HASH_SIZE);
		entry->algo = algo;
		hash_add(ctx->entries, &entry->hnode, block_no);
	} else {
		if (entry->have_hash &&
		    (entry->algo != algo ||
		     memcmp(entry->hash, hash, HIFS_BLOCK_HASH_SIZE))) {
			hifs_warning("dedupe: block %llu hash mismatch during read",
			     (unsigned long long)block_no);
			memcpy(entry->hash, hash, HIFS_BLOCK_HASH_SIZE);
			entry->dirty = true;
		} else {
			entry->dirty = false;
		}
		entry->have_hash = true;
		entry->algo = algo;
		entry->last_update_ns = now_ns;
	}

out_unlock:
	mutex_unlock(&ctx->lock);
}

int hifs_dedupe_init(struct hifs_sb_info *info)
{
	struct hifs_dedupe_ctx *ctx;

	if (!info)
		return -EINVAL;
	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	mutex_init(&ctx->lock);
	hash_init(ctx->entries);

	ctx->shash = crypto_alloc_shash("sha256", 0, 0);
	if (IS_ERR(ctx->shash)) {
		hifs_warning("dedupe: sha256 unavailable (%ld); using siphash fallback",
			     PTR_ERR(ctx->shash));
		ctx->shash = NULL;
	}

	get_random_bytes(&ctx->fallback_key_primary,
			 sizeof(ctx->fallback_key_primary));
	get_random_bytes(&ctx->fallback_key_secondary,
			 sizeof(ctx->fallback_key_secondary));

	info->dedupe = ctx;
	return 0;
}

void hifs_dedupe_shutdown(struct hifs_sb_info *info)
{
	struct hifs_dedupe_ctx *ctx;
	struct hifs_dedupe_entry *entry;
	struct hlist_node *tmp;
	int bkt;

	if (!info)
		return;

	ctx = info->dedupe;
	if (!ctx)
		return;

	mutex_lock(&ctx->lock);
	hash_for_each_safe(ctx->entries, bkt, tmp, entry, hnode) {
		hash_del(&entry->hnode);
		kfree(entry);
	}
	mutex_unlock(&ctx->lock);

	if (ctx->shash)
		crypto_free_shash(ctx->shash);

	info->dedupe = NULL;
	kfree(ctx);
}

bool hifs_dedupe_should_push(struct super_block *sb, uint64_t block_no)
{
	struct hifs_dedupe_ctx *ctx = hifs_sb_dedupe_ctx(sb);
	struct hifs_dedupe_entry *entry;
	bool dirty = true;

	if (!ctx)
		return true;

	mutex_lock(&ctx->lock);
	entry = hifs_dedupe_find_locked(ctx, block_no);
	if (entry && entry->have_hash)
		dirty = entry->dirty;
	mutex_unlock(&ctx->lock);

	return dirty;
}

void hifs_dedupe_mark_clean(struct super_block *sb, uint64_t block_no, bool success)
{
	struct hifs_dedupe_ctx *ctx = hifs_sb_dedupe_ctx(sb);
	struct hifs_dedupe_entry *entry;

	if (!ctx)
		return;

	mutex_lock(&ctx->lock);
	entry = hifs_dedupe_find_locked(ctx, block_no);
	if (entry) {
		if (success)
			entry->dirty = false;
		entry->last_update_ns = ktime_get_coarse_ns();
	}
	mutex_unlock(&ctx->lock);
}

int hifs_dedupe_writes(struct super_block *sb, uint64_t block,
	       const void *data, size_t len,
	       uint8_t hash_out[HIFS_BLOCK_HASH_SIZE],
	       enum hifs_hash_algorithm *algo_out)
{
	struct hifs_dedupe_ctx *ctx;
	u8 hash[HIFS_BLOCK_HASH_SIZE];
	bool changed = true;
	enum hifs_hash_algorithm algo = HIFS_HASH_ALGO_NONE;
	int ret;

	if (!sb || !data || !len || !hash_out)
		return -EINVAL;

	ctx = hifs_sb_dedupe_ctx(sb);
	ret = hifs_dedupe_hash_block(ctx, data, len, hash, &algo);
	if (ret)
		return ret;

	memcpy(hash_out, hash, HIFS_BLOCK_HASH_SIZE);

	ret = hifs_dedupe_update_entry(ctx, block, hash, algo, &changed);
	if (ret)
		return ret;

	if (!changed)
		hifs_debug("dedupe: block %llu unchanged", (unsigned long long)block);

	if (algo_out)
		*algo_out = algo;

	return 0;
}

int hifs_rehydrate_reads(struct super_block *sb, uint64_t block,
	       const void *data, size_t len,
	       uint8_t hash_out[HIFS_BLOCK_HASH_SIZE],
	       enum hifs_hash_algorithm *algo_out)
{
	struct hifs_dedupe_ctx *ctx;
	u8 hash[HIFS_BLOCK_HASH_SIZE];
	enum hifs_hash_algorithm algo = HIFS_HASH_ALGO_NONE;
	int ret;

	if (!sb || !data || !len || !hash_out)
		return -EINVAL;

	ctx = hifs_sb_dedupe_ctx(sb);
	ret = hifs_dedupe_hash_block(ctx, data, len, hash, &algo);
	if (ret)
		return ret;

	memcpy(hash_out, hash, HIFS_BLOCK_HASH_SIZE);
	hifs_dedupe_note_read(ctx, block, hash, algo);

	if (algo_out)
		*algo_out = algo;

	return 0;
}
