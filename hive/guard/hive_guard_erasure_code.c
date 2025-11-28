/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */
// hifs_erasure.c

#include <rocksdb/c.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hive_guard.h"
#include "hive_guard_sn_tcp.h"
#include "hive_guard_sql.h"
#include "hive_guard_raft.h"
#include "hive_guard_erasure_code.h"


static ec_ctx_t g_ec = { .desc = -1, .initialized = 0 };

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
    

    memset(out_stripes, 0, sizeof(struct HifsEstripeLocations) * HIFS_EC_STRIPES);

    for (size_t i = 0; i < HIFS_EC_STRIPES; ++i) {
        out_stripes[i].storage_node_id = (uint32_t)last_node_in_cascade + i;
        out_stripes[i].shard_id = (uint32_t)((block_no + i) % HIFS_SHARDS_PER_NODE);
        out_stripes[i].estripe_id = hifs_alloc_estripe_id();
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

bool hifs_volume_block_store(uint64_t volume_id, uint64_t block_no,
                             const uint8_t *buf, uint32_t len)
{
    /* 1) Compute content hash (global dedupe key) */
    uint8_t hash[HIFS_HASH_LEN];
    uint8_t hash_algo = HIFS_HASH_ALGO_SHA256; /* whatever constant you use */

    if (!hifs_compute_hash(hash_algo, buf, len, hash)) {
        return false;
    }

    /* 2) EC-encode into 6 stripes */
    uint8_t *encoded[HIFS_EC_STRIPES] = {0};
    uint32_t encoded_len[HIFS_EC_STRIPES] = {0};

    if (!hifs_volume_block_ec_encode(buf, len, encoded, encoded_len)) {
        /* free encoded[] if allocated inside encode */
        return false;
    }

    /* 3) Choose placement & assign stripe IDs */
    struct HifsEstripeLocations stripes[HIFS_EC_STRIPES];
    memset(stripes, 0, sizeof(stripes));

    hifs_ec_choose_placement(volume_id, block_no, stripes);
    for (int i = 0; i < HIFS_EC_STRIPES; ++i) {
        stripes[i].estripe_id = hifs_alloc_estripe_id();
        /* hifs_ec_choose_placement should have already filled storage_node_id + shard_id */
    }

    /* 4) Send stripes to target nodes over TCP */
    for (int i = 0; i < HIFS_EC_STRIPES; ++i) {
        uint64_t offset = 0;
        int rc = hifs_send_stripe_to_node(stripes[i].storage_node_id,
                                          stripes[i].shard_id,
                                          stripes[i].estripe_id,
                                          encoded[i],
                                          encoded_len[i],
                                          &offset);
        if (rc != 0) {
            /* TODO: handle partial success / cleanup / retry */
            /* For now, fail the whole operation */
            return false;
        }
        stripes[i].block_offset = offset;
    }

    /* 5) Build RaftPutBlock command */
    struct RaftPutBlock cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.op_type   = OP_PUT_BLOCK;
    cmd.hash_algo = hash_algo;
    cmd.volume_id = volume_id;
    cmd.block_no  = block_no;
    memcpy(cmd.block_hash, hash, HIFS_HASH_LEN);

    for (int i = 0; i < HIFS_EC_STRIPES; ++i) {
        cmd.stripes[i] = stripes[i];
    }

    /* 6) Submit to Raft leader for metadata replication */
    if (hifs_raft_submit_put_block(&cmd) != 0) {
        /* You may choose to keep stripes and retry later, or GC them. */
        return false;
    }

    /* 7) Free EC buffers if they were heap-allocated */
    for (int i = 0; i < HIFS_EC_STRIPES; ++i) {
        if (encoded[i]) {
            free(encoded[i]);
        }
    }

    return true;
}

int hg_kv_apply_put_block(const struct RaftPutBlock *cmd)
{
    if (!g_db || !cmd) return -1;

    /* 1) Build/update hash -> stripes mapping (h2s:) */
    struct H2SEntry h2s;
    memset(&h2s, 0, sizeof(h2s));
    h2s.ref_count = 1;  /* you can merge if it already exists, or rely on Raft semantics */

    for (int i = 0; i < HIFS_EC_STRIPES; ++i) {
        h2s.estripe_ids[i] = cmd->stripes[i].estripe_id;
    }

    /* 2) Build volume/block -> hash mapping (vb:) */
    struct VbEntry vb;
    memset(&vb, 0, sizeof(vb));
    vb.hash_algo = cmd->hash_algo;
    memcpy(vb.block_hash, cmd->block_hash, HIFS_HASH_LEN);

    /* 3) Prepare write batch */
    rocksdb_writebatch_t *wb = rocksdb_writebatch_create();

    /* Put h2s: */
    char h_key[3 + HIFS_HASH_LEN];
    size_t h_key_len = 0;
    make_h2s_key(cmd->hash_algo, cmd->block_hash, h_key, &h_key_len);
    rocksdb_writebatch_put(wb,
                           h_key, h_key_len,
                           (const char *)&h2s, sizeof(h2s));

    /* Put vb: */
    char vb_key[2 + 8 + 8];
    size_t vb_key_len = 0;
    make_vb_key(cmd->volume_id, cmd->block_no, vb_key, &vb_key_len);
    rocksdb_writebatch_put(wb,
                           vb_key, vb_key_len,
                           (const char *)&vb, sizeof(vb));

    /* Put estripe: entries for each stripe */
    for (int i = 0; i < HIFS_EC_STRIPES; ++i) {
        struct EstripeLoc loc;
        loc.shard_id        = cmd->stripes[i].shard_id;
        loc.storage_node_id = cmd->stripes[i].storage_node_id;
        loc.block_offset    = cmd->stripes[i].block_offset;

        char es_key[2 + 8];
        size_t es_key_len = 0;
        make_estripe_key(cmd->stripes[i].estripe_id, es_key, &es_key_len);

        rocksdb_writebatch_put(wb,
                               es_key, es_key_len,
                               (const char *)&loc, sizeof(loc));
    }

    /* 4) Commit batch */
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

    return 0;
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
