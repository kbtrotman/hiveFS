/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */
// hifs_erasure.c


#include "hive_guard.h"
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


