/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */
// hifs_erasure.c

#include <errno.h>
#include <rocksdb/c.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/sha.h>
#include "hive_guard.h"
#include "hive_guard_kv.h"
#include "hive_guard_sn_tcp.h"
#include "hive_guard_sql.h"
#include "hive_guard_raft.h"
#include "hive_guard_erasure_code.h"


static ec_ctx_t g_ec = { .desc = -1, .initialized = 0 };
int last_node_in_cascade;
int cascade_length;

/* ======================= Internal helpers ======================= */

static int hicomm_ec_ensure_initialized(size_t k, size_t m, size_t w, int checksum)
{
    if (g_ec.initialized) {
        /* Reuse if params match; otherwise re-init */
        if (g_ec.k == k && g_ec.m == m && g_ec.w == w && g_ec.checksum == checksum)
            return 0;

        liberasurecode_instance_destroy(g_ec.desc);
        g_ec.desc = -1;
        g_ec.initialized = 0;
    }

    ec_backend_id_t backend = EC_BACKEND_ISA_L_RS_VAND;
    if (!liberasurecode_backend_available(backend)) {
        fprintf(stderr, "ISA-L backend not available (plugin or isa-l missing?)\n");
        return -1;
    }

    struct ec_args args = {0};
    args.k  = (int)k;
    args.m  = (int)m;
    args.w  = (int)w;
    args.hd = (int)m;           /* RS codes: hd == m */
    args.ct = checksum;

    int desc = liberasurecode_instance_create(backend, &args);
    if (desc <= 0) {
        fprintf(stderr, "liberasurecode_instance_create failed\n");
        return -1;
    }

    g_ec.desc      = desc;
    g_ec.k         = k;
    g_ec.m         = m;
    g_ec.w         = w;
    g_ec.checksum  = checksum;
    g_ec.initialized = 1;
    return 0;
}

/* One-liner that always initializes with the module's profile */
int hifs_ec_ensure_init(void)
{
    return hicomm_ec_ensure_initialized(HIFS_EC_K, HIFS_EC_M, HIFS_EC_W, HIFS_EC_CHECKSUM);
}

/* ======================= Public API ======================= */

int hicomm_erasure_coding_init(void)
{
    return hifs_ec_ensure_init();
}

int hicomm_erasure_coding_cleanup(void)
{
    if (!g_ec.initialized) return 0;
    liberasurecode_instance_destroy(g_ec.desc);
    g_ec.desc = -1;
    g_ec.initialized = 0;
    return 0;
}

/*
 * ENCODE:
 * - Caller supplies:
 *     - num_data_chunks (=k) and num_parity_chunks (=m) — validated against module profile
 *     - encoded_chunks: array of size k+m (we malloc() each element)
 * - We set:
 *     - *chunk_size to per-fragment size (including metadata)
 * - Returns 0 on success; negative on error.
 */
int hicomm_erasure_coding_encode(const uint8_t *data, size_t data_len,
                                 uint8_t **encoded_chunks, size_t *chunk_size,
                                 size_t num_data_chunks, size_t num_parity_chunks)
{
    if (!data || !encoded_chunks || !chunk_size) return -1;

    /* Enforce single profile to avoid mismatches and needless re-inits */
    if (num_data_chunks != HIFS_EC_K || num_parity_chunks != HIFS_EC_M) {
        fprintf(stderr, "encode: (k,m) mismatch (got %zu,%zu; expected %d,%d)\n",
                num_data_chunks, num_parity_chunks, HIFS_EC_K, HIFS_EC_M);
        return -1;
    }

    if (hifs_ec_ensure_init() != 0) return -1;

    char **data_frags = NULL, **parity_frags = NULL;
    uint64_t frag_len = 0;

    int rc = liberasurecode_encode(g_ec.desc,
                                   (const char *)data, (uint64_t)data_len,
                                   &data_frags, &parity_frags, &frag_len);
    if (rc != 0) {
        fprintf(stderr, "encode failed (rc=%d)\n", rc);
        return -1;
    }

    const size_t total = HIFS_EC_K + HIFS_EC_M;
    for (size_t i = 0; i < total; ++i) {
        const char *src = (i < HIFS_EC_K) ? data_frags[i] : parity_frags[i - HIFS_EC_K];
        encoded_chunks[i] = (uint8_t *)malloc(frag_len);
        if (!encoded_chunks[i]) {
            fprintf(stderr, "malloc failed for fragment %zu\n", i);
            for (size_t j = 0; j < i; ++j) { free(encoded_chunks[j]); encoded_chunks[j] = NULL; }
            liberasurecode_encode_cleanup(g_ec.desc, data_frags, parity_frags);
            return -1;
        }
        memcpy(encoded_chunks[i], src, (size_t)frag_len);
    }
    *chunk_size = (size_t)frag_len;

    liberasurecode_encode_cleanup(g_ec.desc, data_frags, parity_frags);
    return 0;
}

/*
 * DECODE (all fragments present)
 * - encoded_chunks: array of size k+m, none NULL
 * - chunk_size: size returned by encode
 * - decoded_data: caller buffer
 * - *data_len: in=capacity, out=actual size; returns -2 if capacity too small (and sets required size)
 */
int hicomm_erasure_coding_decode(uint8_t **encoded_chunks, size_t chunk_size,
                                 size_t num_data_chunks, size_t num_parity_chunks,
                                 uint8_t *decoded_data, size_t *data_len)
{
    if (!encoded_chunks || !decoded_data || !data_len) return -1;
    if (num_data_chunks != HIFS_EC_K || num_parity_chunks != HIFS_EC_M) {
        fprintf(stderr, "decode: (k,m) mismatch (got %zu,%zu; expected %d,%d)\n",
                num_data_chunks, num_parity_chunks, HIFS_EC_K, HIFS_EC_M);
        return -1;
    }
    if (hifs_ec_ensure_init() != 0) return -1;

    const size_t total = HIFS_EC_K + HIFS_EC_M;

    /* Build "available" array (library expects char** of length k+m) */
    char **available = (char **)calloc(total, sizeof(char *));
    if (!available) return -1;
    for (size_t i = 0; i < total; ++i) available[i] = (char *)encoded_chunks[i];

    char *decoded = NULL;
    uint64_t decoded_len = 0;

    int rc = liberasurecode_decode(g_ec.desc, available, (int)total,
                                   (uint64_t)chunk_size,
                                   /*force_metadata_checks=*/1,
                                   &decoded, &decoded_len);
    free(available);

    if (rc != 0) {
        fprintf(stderr, "decode failed (rc=%d)\n", rc);
        return -1;
    }

    if ((size_t)decoded_len > *data_len) {
        *data_len = (size_t)decoded_len;
        liberasurecode_decode_cleanup(g_ec.desc, decoded);
        return -2; /* insufficient capacity */
    }

    memcpy(decoded_data, decoded, (size_t)decoded_len);
    *data_len = (size_t)decoded_len;

    liberasurecode_decode_cleanup(g_ec.desc, decoded);
    return 0;
}

/*
 * REBUILD from partial (some fragments missing: pass NULLs)
 * - same return rules as normal decode
 */
int hicomm_erasure_coding_rebuild_from_partial(uint8_t **encoded_chunks, size_t chunk_size,
                                               size_t num_data_chunks, size_t num_parity_chunks,
                                               uint8_t *decoded_data, size_t *data_len)
{
    if (!encoded_chunks || !decoded_data || !data_len) return -1;
    if (num_data_chunks != HIFS_EC_K || num_parity_chunks != HIFS_EC_M) {
        fprintf(stderr, "rebuild: (k,m) mismatch (got %zu,%zu; expected %d,%d)\n",
                num_data_chunks, num_parity_chunks, HIFS_EC_K, HIFS_EC_M);
        return -1;
    }
    if (hifs_ec_ensure_init() != 0) return -1;

    const size_t total = HIFS_EC_K + HIFS_EC_M;
    char **available = (char **)calloc(total, sizeof(char *));
    if (!available) return -1;
    for (size_t i = 0; i < total; ++i) available[i] = (char *)encoded_chunks[i]; /* may be NULL */

    char *decoded = NULL;
    uint64_t decoded_len = 0;

    int rc = liberasurecode_decode(g_ec.desc, available, (int)total,
                                   (uint64_t)chunk_size,
                                   /*force_metadata_checks=*/1,
                                   &decoded, &decoded_len);
    free(available);

    if (rc != 0) {
        fprintf(stderr, "partial decode failed (rc=%d)\n", rc);
        return -1;
    }

    if ((size_t)decoded_len > *data_len) {
        *data_len = (size_t)decoded_len;
        liberasurecode_decode_cleanup(g_ec.desc, decoded);
        return -2;
    }

    memcpy(decoded_data, decoded, (size_t)decoded_len);
    *data_len = (size_t)decoded_len;

    liberasurecode_decode_cleanup(g_ec.desc, decoded);
    return 0;
}


/* ======================= Internal helpers ======================= */

/* Decide which storage node & shard each stripe goes to */
void hifs_ec_choose_placement(uint64_t volume_id,
                              uint64_t block_no,
                              struct HifsEstripeLocations out_stripes[HIFS_EC_STRIPES])
{
	(void)volume_id;

	if (cascade_length <= 0)
		cascade_length = 1;
	if (last_node_in_cascade <= 0)
		last_node_in_cascade = 1;

	memset(out_stripes, 0, sizeof(struct HifsEstripeLocations) * HIFS_EC_STRIPES);

	for (size_t i = 0; i < HIFS_EC_STRIPES; ++i) {
		uint32_t node = (uint32_t)(((last_node_in_cascade - 1 + (int)i) % cascade_length) + 1);
		out_stripes[i].storage_node_id = node;
		out_stripes[i].shard_id = (uint32_t)((block_no + i) % HIFS_SHARDS_PER_NODE);
		out_stripes[i].estripe_id = hifs_alloc_estripe_id();
		out_stripes[i].block_offset = 0;
	}

	NEXT_STRIPE_NODE(last_node_in_cascade, cascade_length);
}


bool hifs_volume_block_ec_encode(const uint8_t *buf, uint32_t len,
				 enum hifs_hash_algorithm algo,
				 struct hifs_ec_stripe_set *out)
{
	const size_t total = HIFS_EC_K + HIFS_EC_M;
	uint8_t *encoded_chunks[HIFS_EC_K + HIFS_EC_M] = {0};
	size_t frag_size = 0;

	if (!buf || !out || len == 0 || len > HIFS_DEFAULT_BLOCK_SIZE)
		return false;
	if (algo != HIFS_HASH_ALGO_SHA256) {
		hifs_warning("Unsupported hash algorithm %u", (unsigned)algo);
		return false;
	}

	memset(out, 0, sizeof(*out));

	if (hifs_ec_ensure_init() != 0 &&
	    hicomm_erasure_coding_init() != 0) {
		hifs_warning("EC init failed in encode");
		return false;
	}

	if (hicomm_erasure_coding_encode(buf, len, encoded_chunks,
					 &frag_size, HIFS_EC_K, HIFS_EC_M) != 0) {
		hifs_warning("EC encode failed");
		goto fail;
	}

	out->chunks = calloc(total, sizeof(uint8_t *));
	if (!out->chunks)
		goto fail;

	for (size_t i = 0; i < total; ++i) {
		out->chunks[i] = encoded_chunks[i];
		encoded_chunks[i] = NULL;
	}
	out->chunk_count = total;
	out->chunk_len = frag_size;
	out->hash_algo = algo;

	unsigned char digest[HIFS_BLOCK_HASH_SIZE];
	SHA256(buf, len, digest);
	memcpy(out->hash, digest, sizeof(out->hash));

	return true;

fail:
	for (size_t i = 0; i < total; ++i) {
		free(encoded_chunks[i]);
	}
	free(out->chunks);
	memset(out, 0, sizeof(*out));
	return false;
}

void hifs_volume_block_ec_free(struct hifs_ec_stripe_set *ec)
{
	if (!ec)
		return;
	if (ec->chunks) {
		for (size_t i = 0; i < ec->chunk_count; ++i)
			free(ec->chunks[i]);
		free(ec->chunks);
	}
	memset(ec, 0, sizeof(*ec));
}

bool hifs_volume_block_store(uint64_t volume_id, uint64_t block_no,
			     const uint8_t *buf, uint32_t len)
{
	struct hifs_ec_stripe_set ec = {0};
	bool ok = false;

	if (!buf || len == 0)
		return false;

	if (!hifs_volume_block_ec_encode(buf, len, HIFS_HASH_ALGO_SHA256, &ec))
		return false;

	if (hifs_put_block_stripes(volume_id, block_no, &ec, ec.hash_algo) == 0)
		ok = true;

	hifs_volume_block_ec_free(&ec);
	return ok;
}

int hifs_put_block(uint64_t volume_id, uint64_t block_no,
		   const void *data, size_t len,
		   enum hifs_hash_algorithm algo)
{
	struct hifs_ec_stripe_set ec = {0};
	int rc;

	if (!hg_guard_local_can_write())
		return -EAGAIN;
	if (!data || len == 0 || len > HIFS_DEFAULT_BLOCK_SIZE)
		return -EINVAL;

	if (!hifs_volume_block_ec_encode(data, (uint32_t)len, algo, &ec))
		return -EIO;

	rc = hifs_put_block_stripes(volume_id, block_no, &ec, algo);
	hifs_volume_block_ec_free(&ec);
	return rc;
}

int hifs_put_block_stripes(uint64_t volume_id, uint64_t block_no,
			   const struct hifs_ec_stripe_set *ec,
			   enum hifs_hash_algorithm algo)
{
	struct HifsEstripeLocations stripes[HIFS_EC_STRIPES];
	struct RaftPutBlock cmd;

	if (!ec || !ec->chunks || ec->chunk_count != HIFS_EC_STRIPES)
		return -EINVAL;
	if (!hg_guard_local_can_write())
		return -EAGAIN;

	hifs_ec_choose_placement(volume_id, block_no, stripes);

	for (size_t i = 0; i < HIFS_EC_STRIPES; ++i) {
		uint64_t offset = 0;
		int rc = hifs_send_stripe_to_node(stripes[i].storage_node_id,
						  stripes[i].shard_id,
						  stripes[i].estripe_id,
						  ec->chunks[i],
						  (uint32_t)ec->chunk_len,
						  &offset);
		if (rc != 0)
			return -EIO;
		stripes[i].block_offset = offset;
		if (hg_kv_put_estripe_chunk(stripes[i].estripe_id,
					    ec->chunks[i],
					    ec->chunk_len) != 0) {
			return -EIO;
		}
	}

	memset(&cmd, 0, sizeof(cmd));
	cmd.hash_algo = (uint8_t)algo;
	cmd.volume_id = volume_id;
	cmd.block_no = block_no;
	memcpy(cmd.hash, ec->hash, HIFS_BLOCK_HASH_SIZE);

	for (size_t i = 0; i < HIFS_EC_STRIPES; ++i) {
		cmd.ec_stripes[i].storage_node_id = stripes[i].storage_node_id;
		cmd.ec_stripes[i].shard_id = stripes[i].shard_id;
		cmd.ec_stripes[i].estripe_id = stripes[i].estripe_id;
		cmd.ec_stripes[i].block_offset = stripes[i].block_offset;
	}

	return hifs_raft_submit_put_block(&cmd);
}

int hifs_send_stripe_to_node(uint32_t storage_node_id,
                             uint32_t shard_id,
                             uint64_t estripe_id,
                             const uint8_t *data,
                             uint32_t len,
                             uint64_t *out_block_offset)
{
    int rc = hifs_sn_tcp_send(storage_node_id, shard_id, estripe_id, data, len);
    if (rc == 0 && out_block_offset)
        *out_block_offset = 0;
    return rc;
}

uint64_t hifs_alloc_estripe_id(void)
{
    static uint64_t next_id = 1;
    return next_id++;
}

int hifs_recv_stripe_from_node(uint32_t storage_node_id,
                               uint32_t shard_id,
                               uint64_t estripe_id,
                               const uint8_t *data,
                               uint32_t len,
                               uint64_t *out_block_offset)
{
    (void)storage_node_id;
    (void)shard_id;
    (void)estripe_id;
    (void)data;
    (void)len;
    if (out_block_offset)
        *out_block_offset = hifs_alloc_estripe_id();
    return 0;
}
