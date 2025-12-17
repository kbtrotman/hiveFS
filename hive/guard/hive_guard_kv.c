/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */



/*
 * hive_guard_kv.c
 *
 * Simple KV store interface using high-speed SSD-optimized RocksDB key/value store
 */

 #include <string.h>
#include <stdio.h>
#include <rocksdb/c.h>

#include "hive_guard_kv.h"
#include "hive_guard_erasure_code.h"
#include "hive_guard_stats.h"

static rocksdb_t *g_db = NULL;

int hg_kv_init(const char *path)
{
    if (g_db) return 0;

    char *err = NULL;
    rocksdb_options_t *opts = rocksdb_options_create();
    rocksdb_options_set_create_if_missing(opts, 1);

    g_db = rocksdb_open(opts, path, &err);
    rocksdb_options_destroy(opts);

    if (err != NULL) {
        fprintf(stderr, "rocksdb_open error: %s\n", err);
        free(err);
        return -1;
    }
    return 0;
}

void hg_kv_shutdown(void)
{
    if (g_db) {
        rocksdb_close(g_db);
        g_db = NULL;
    }
}

/* Key builder for h2s: [ 'h','2', algo, hash[32] ] */
static void make_h2s_key(uint8_t hash_algo,
                         const uint8_t hash[32],
                         char *key_out,
                         size_t *key_len_out)
{
    key_out[0] = 'h';
    key_out[1] = '2';
    key_out[2] = (char)hash_algo;
    memcpy(&key_out[3], hash, 32);
    *key_len_out = 3 + 32;
}

static void make_vb_key(uint64_t volume_id,
			uint64_t block_no,
			char *key_out,
			size_t *key_len_out)
{
	key_out[0] = 'v';
	key_out[1] = 'b';
	memcpy(&key_out[2], &volume_id, sizeof(volume_id));
	memcpy(&key_out[2 + sizeof(volume_id)], &block_no, sizeof(block_no));
	*key_len_out = 2 + sizeof(volume_id) + sizeof(block_no);
}

static void make_estripe_key(uint64_t estripe_id,
			     char *key_out,
			     size_t *key_len_out)
{
	key_out[0] = 'e';
	key_out[1] = 's';
	memcpy(&key_out[2], &estripe_id, sizeof(estripe_id));
	*key_len_out = 2 + sizeof(estripe_id);
}

static void make_estripe_data_key(uint64_t estripe_id,
				  char *key_out,
				  size_t *key_len_out)
{
	key_out[0] = 'e';
	key_out[1] = 'd';
	memcpy(&key_out[2], &estripe_id, sizeof(estripe_id));
	*key_len_out = 2 + sizeof(estripe_id);
}

static void make_vif_key(uint64_t volume_id,
			 uint64_t inode_id,
			 uint16_t fp_index,
			 char *key_out,
			 size_t *key_len_out)
{
	key_out[0] = 'v';
	key_out[1] = 'i';
	key_out[2] = 'f';
	size_t off = 3;
	memcpy(&key_out[off], &volume_id, sizeof(volume_id));
	off += sizeof(volume_id);
	memcpy(&key_out[off], &inode_id, sizeof(inode_id));
	off += sizeof(inode_id);
	memcpy(&key_out[off], &fp_index, sizeof(fp_index));
	off += sizeof(fp_index);
	*key_len_out = off;
}

int hg_kv_put_h2s(uint8_t hash_algo,
                  const uint8_t hash[32],
                  const struct H2SEntry *e)
{
    if (!g_db) return -1;

    char key[3 + 32];
    size_t klen = 0;
    make_h2s_key(hash_algo, hash, key, &klen);

    char *err = NULL;
    rocksdb_writeoptions_t *wopt = rocksdb_writeoptions_create();

    rocksdb_put(g_db, wopt,
                key, klen,
                (const char *)e, sizeof(*e),
                &err);

    rocksdb_writeoptions_destroy(wopt);

    if (err != NULL) {
        fprintf(stderr, "rocksdb_put error: %s\n", err);
        free(err);
        return -1;
    }
    return 0;
}

int hg_kv_get_h2s(uint8_t hash_algo,
                  const uint8_t hash[32],
                  struct H2SEntry *out)
{
    if (!g_db) return -1;

    char key[3 + 32];
    size_t klen = 0;
    make_h2s_key(hash_algo, hash, key, &klen);

    char *err = NULL;
    size_t vlen = 0;
    rocksdb_readoptions_t *ropt = rocksdb_readoptions_create();
    char *val = rocksdb_get(g_db, ropt,
                            key, klen,
                            &vlen, &err);
    rocksdb_readoptions_destroy(ropt);

    if (err != NULL) {
        fprintf(stderr, "rocksdb_get error: %s\n", err);
        free(err);
        return -1;
    }

    if (!val) {
        return -1; /* not found */
    }

    if (vlen != sizeof(*out)) {
        fprintf(stderr, "hg_kv_get_h2s: size mismatch (%zu != %zu)\n",
                vlen, sizeof(*out));
        free(val);
        return -1;
    }

    memcpy(out, val, sizeof(*out));
    free(val);
    return 0;
}

int hg_kv_get_vb_entry(uint64_t volume_id,
		       uint64_t block_no,
		       struct VbEntry *out)
{
	if (!g_db || !out)
		return -1;

	char key[2 + sizeof(uint64_t) * 2];
	size_t klen = 0;
	make_vb_key(volume_id, block_no, key, &klen);

	char *err = NULL;
	size_t vlen = 0;
	rocksdb_readoptions_t *ropt = rocksdb_readoptions_create();
	char *val = rocksdb_get(g_db, ropt,
				key, klen,
				&vlen, &err);
	rocksdb_readoptions_destroy(ropt);

	if (err != NULL) {
		fprintf(stderr, "hg_kv_get_vb_entry: rocksdb_get error: %s\n", err);
		free(err);
		return -1;
	}
	if (!val)
		return -1;
	if (vlen != sizeof(*out)) {
		fprintf(stderr, "hg_kv_get_vb_entry: size mismatch (%zu != %zu)\n",
			vlen, sizeof(*out));
		free(val);
		return -1;
	}
	memcpy(out, val, sizeof(*out));
	free(val);
	return 0;
}

int hg_kv_get_estripe_loc(uint64_t estripe_id,
			  struct EstripeLoc *out)
{
	if (!g_db || !out)
		return -1;

	char key[2 + sizeof(uint64_t)];
	size_t klen = 0;
	make_estripe_key(estripe_id, key, &klen);

	char *err = NULL;
	size_t vlen = 0;
	rocksdb_readoptions_t *ropt = rocksdb_readoptions_create();
	char *val = rocksdb_get(g_db, ropt,
				key, klen,
				&vlen, &err);
	rocksdb_readoptions_destroy(ropt);

	if (err != NULL) {
		fprintf(stderr, "hg_kv_get_estripe_loc: rocksdb_get error: %s\n", err);
		free(err);
		return -1;
	}
	if (!val)
		return -1;
	if (vlen != sizeof(*out)) {
		fprintf(stderr, "hg_kv_get_estripe_loc: size mismatch (%zu != %zu)\n",
			vlen, sizeof(*out));
		free(val);
		return -1;
	}
	memcpy(out, val, sizeof(*out));
	free(val);
	return 0;
}

int hg_kv_get_vif_entry(uint64_t volume_id,
			uint64_t inode_id,
			uint16_t fp_index,
			struct hifs_block_fingerprint_wire *out)
{
	if (!g_db || !out)
		return -1;

	char key[3 + sizeof(volume_id) + sizeof(inode_id) + sizeof(fp_index)];
	size_t klen = 0;
	make_vif_key(volume_id, inode_id, fp_index, key, &klen);

	char *err = NULL;
	size_t vlen = 0;
	rocksdb_readoptions_t *ropt = rocksdb_readoptions_create();
	char *val = rocksdb_get(g_db, ropt,
				key, klen,
				&vlen, &err);
	rocksdb_readoptions_destroy(ropt);

	if (err != NULL) {
		fprintf(stderr, "hg_kv_get_vif_entry: rocksdb_get error: %s\n", err);
		free(err);
		return -1;
	}
	if (!val)
		return -1;
	if (vlen != sizeof(*out)) {
		fprintf(stderr, "hg_kv_get_vif_entry: size mismatch (%zu != %zu)\n",
			vlen, sizeof(*out));
		free(val);
		return -1;
	}
	memcpy(out, val, sizeof(*out));
	free(val);
	return 0;
}

int hg_kv_put_estripe_chunk(uint64_t estripe_id,
			    const uint8_t *data,
			    size_t len)
{
	if (!g_db || !data || len == 0)
		return -1;

	char key[2 + sizeof(uint64_t)];
	size_t klen = 0;
	make_estripe_data_key(estripe_id, key, &klen);

	char *err = NULL;
	rocksdb_writeoptions_t *wopt = rocksdb_writeoptions_create();
	rocksdb_put(g_db, wopt,
		    key, klen,
		    (const char *)data, len,
		    &err);
	rocksdb_writeoptions_destroy(wopt);
	if (err != NULL) {
		fprintf(stderr, "hg_kv_put_estripe_chunk: rocksdb_put error: %s\n", err);
		free(err);
		return -1;
	}
	return 0;
}

int hg_kv_get_estripe_chunk(uint64_t estripe_id,
			    uint8_t **out_data,
			    size_t *out_len)
{
	if (!g_db || !out_data || !out_len)
		return -1;

	char key[2 + sizeof(uint64_t)];
	size_t klen = 0;
	make_estripe_data_key(estripe_id, key, &klen);

	char *err = NULL;
	size_t vlen = 0;
	rocksdb_readoptions_t *ropt = rocksdb_readoptions_create();
	char *val = rocksdb_get(g_db, ropt,
				key, klen,
				&vlen, &err);
	rocksdb_readoptions_destroy(ropt);

	if (err != NULL) {
		fprintf(stderr, "hg_kv_get_estripe_chunk: rocksdb_get error: %s\n", err);
		free(err);
		return -1;
	}
	if (!val)
		return -1;

	*out_data = (uint8_t *)val;
	*out_len = vlen;
	return 0;
}

int hg_kv_apply_put_block(const struct RaftPutBlock *cmd)
{
	if (!g_db || !cmd)
		return -1;

	struct H2SEntry h2s = {0};
	bool have_existing = (hg_kv_get_h2s(cmd->hash_algo, cmd->hash, &h2s) == 0);

	if (have_existing) {
		if (h2s.ref_count < UINT64_MAX)
			h2s.ref_count++;
	} else {
		h2s.ref_count = 1;
		for (size_t i = 0; i < HIFS_EC_STRIPES; ++i)
			h2s.estripe_ids[i] = cmd->ec_stripes[i].estripe_id;
	}

	struct VbEntry vb = {0};
	vb.hash_algo = cmd->hash_algo;
	memcpy(vb.block_hash, cmd->hash, HIFS_BLOCK_HASH_SIZE);

	rocksdb_writebatch_t *wb = rocksdb_writebatch_create();

    char h_key[3 + 32];
	size_t h_key_len = 0;
	make_h2s_key(cmd->hash_algo, cmd->hash, h_key, &h_key_len);
	rocksdb_writebatch_put(wb,
			       h_key, h_key_len,
			       (const char *)&h2s, sizeof(h2s));

	char vb_key[2 + sizeof(uint64_t) * 2];
	size_t vb_key_len = 0;
	make_vb_key(cmd->volume_id, cmd->block_no, vb_key, &vb_key_len);
	rocksdb_writebatch_put(wb,
			       vb_key, vb_key_len,
			       (const char *)&vb, sizeof(vb));

#if !HIFS_DEBUG_DISABLE_STRIPES
	for (size_t i = 0; i < HIFS_EC_STRIPES; ++i) {
		struct EstripeLoc loc = {
			.shard_id = cmd->ec_stripes[i].shard_id,
			.storage_node_id = cmd->ec_stripes[i].storage_node_id,
			.block_offset = cmd->ec_stripes[i].block_offset,
		};
		char es_key[2 + sizeof(uint64_t)];
		size_t es_key_len = 0;
		make_estripe_key(cmd->ec_stripes[i].estripe_id, es_key, &es_key_len);
		rocksdb_writebatch_put(wb,
				       es_key, es_key_len,
				       (const char *)&loc, sizeof(loc));
	}
#endif

	uint64_t t0 = hg_now_ns();

	atomic_fetch_add(&g_stats.kv_putblock_calls, 1);
	atomic_fetch_add(&g_stats.kv_putblock_bytes, HIFS_DEFAULT_BLOCK_SIZE );

	if (have_existing) {
		atomic_fetch_add(&g_stats.kv_putblock_dedup_hits, 1);
	} else {
		atomic_fetch_add(&g_stats.kv_putblock_dedup_misses, 1);
	}


	char *err = NULL;
	rocksdb_writeoptions_t *wopt = rocksdb_writeoptions_create();
	rocksdb_write(g_db, wopt, wb, &err);
	rocksdb_writeoptions_destroy(wopt);
	rocksdb_writebatch_destroy(wb);

	if (err != NULL) {
		fprintf(stderr, "hg_kv_apply_put_block: rocksdb_write error: %s\n", err);
		free(err);
		return -1;
	}

	uint64_t t1 = hg_now_ns();
	atomic_fetch_add(&g_stats.kv_rocksdb_writes, 1);
	atomic_fetch_add(&g_stats.kv_rocksdb_write_ns, (t1 - t0));

	return 0;
}

bool hifs_volume_inode_fp_replace(uint64_t volume_id,
				  uint64_t inode_id,
				  uint16_t fp_index,
				  const struct hifs_block_fingerprint_wire *fp)
{
	if (!g_db || fp_index >= HIFS_MAX_BLOCK_HASHES)
		return false;

	char key[3 + sizeof(volume_id) + sizeof(inode_id) + sizeof(fp_index)];
	size_t key_len = 0;
	make_vif_key(volume_id, inode_id, fp_index, key, &key_len);

	char *err = NULL;
	rocksdb_writeoptions_t *wopt = rocksdb_writeoptions_create();
	bool do_delete = (!fp) ||
			 ((fp->block_no == 0) && (fp->hash_algo == 0));

	if (do_delete) {
		rocksdb_delete(g_db, wopt, key, key_len, &err);
	} else {
		rocksdb_put(g_db, wopt,
			    key, key_len,
			    (const char *)fp, sizeof(*fp),
			    &err);
	}

	rocksdb_writeoptions_destroy(wopt);

	if (err != NULL) {
		fprintf(stderr,
			"hifs_volume_inode_fp_replace: rocksdb_%s error: %s\n",
			do_delete ? "delete" : "put",
			err);
		free(err);
		return false;
	}

	return true;
}

bool hifs_volume_inode_fp_sync(uint64_t volume_id,
			       uint64_t inode_id,
			       const struct hifs_block_fingerprint_wire *fps,
			       uint16_t fp_count)
{
	if (!g_db) {
		fprintf(stderr,
			"hifs_volume_inode_fp_sync: RocksDB unavailable (volume %llu inode %llu)\n",
			(unsigned long long)volume_id,
			(unsigned long long)inode_id);
		return false;
	}

	for (uint16_t i = 0; i < HIFS_MAX_BLOCK_HASHES; ++i) {
		const struct hifs_block_fingerprint_wire *fp = NULL;

		if (fps && i < fp_count)
			fp = &fps[i];

            
		if (!hifs_volume_inode_fp_replace(volume_id, inode_id, i, fp))
			return false;
	}

	return true;
}

 long long get_next_auto_increment_id(rocksdb_t* db, rocksdb_readoptions_t* ropts, rocksdb_writeoptions_t* wopts) {
    char* err = NULL;
    size_t len;
    char* counter_str = rocksdb_get(db, ropts, "next_id_counter", strlen("next_id_counter"), &len, &err);

    long long current_id = 0;
    if (err == NULL && counter_str != NULL) {
        current_id = atoll(counter_str);
        free(counter_str);
    } else if (err != NULL) {
        fprintf(stderr, "Error reading counter: %s\n", err);
        rocksdb_free(err);
        return -1; // Indicate error
    }

    long long next_id = current_id + 1;

    // Convert next_id to string for storage
    char next_id_buf[32];
    sprintf(next_id_buf, "%lld", next_id);

    // Atomically update the counter
    err = NULL;
    rocksdb_put(db, wopts, "next_id_counter", strlen("next_id_counter"), next_id_buf, strlen(next_id_buf), &err);
    if (err != NULL) {
        fprintf(stderr, "Error updating counter: %s\n", err);
        rocksdb_free(err);
        return -1; // Indicate error
    }

    return next_id;
}
