/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>

#include <liberasurecode/erasurecode.h>

#include "hi_command.h"
#include "sql/hi_command_sql.h"

/* -------------------- module context -------------------- */
static ec_ctx_t g_ec = { .desc = -1, .initialized = 0 };


/* -------------------- helpers -------------------- */

static int ensure_initialized(size_t k, size_t m, size_t w, int checksum)
{
    if (g_ec.initialized) {
        // If already initialized with same params, reuse. Otherwise re-init.
        if (g_ec.k == k && g_ec.m == m && g_ec.w == w && g_ec.checksum == checksum)
            return 0;

        liberasurecode_instance_destroy(g_ec.desc);
        g_ec.initialized = 0;
        g_ec.desc = -1;
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
    args.hd = (int)m;       // RS codes: hd == m
    args.ct = checksum;     // e.g., CHKSUM_CRC32

    int desc = liberasurecode_instance_create(backend, &args);
    if (desc <= 0) {
        fprintf(stderr, "liberasurecode_instance_create failed\n");
        return -1;
    }

    g_ec.desc = desc;
    g_ec.k = k;
    g_ec.m = m;
    g_ec.w = w;
    g_ec.checksum = checksum;
    g_ec.initialized = 1;
    return 0;
}

/* -------------------- Erasure Encoding context API -------------------- */

int hicommand_erasure_coding_init(void)
{
    // Pick defaults you like; you can override per-encode call by passing num_*.
    size_t k = 6, m = 3, w = 8;
    int checksum = CHKSUM_CRC32;

    if (ensure_initialized(k, m, w, checksum) != 0)
        return 1;

    return 0;
}

int hicommand_erasure_coding_cleanup(void)
{
    if (!g_ec.initialized) return 0;

    liberasurecode_instance_destroy(g_ec.desc);
    g_ec.desc = -1;
    g_ec.initialized = 0;
    return 0;
}

/*
 * ENCODE:
 * - Caller must provide:
 *     - num_data_chunks (=k) and num_parity_chunks (=m)
 *     - encoded_chunks: an array of pointers of size k+m (we will malloc each [i])
 * - We set:
 *     - *chunk_size to the per-fragment size (including metadata)
 * - Return 0 on success.
 */
int hicommand_erasure_coding_encode(const uint8_t *data, size_t data_len,
                                    uint8_t **encoded_chunks, size_t *chunk_size,
                                    size_t num_data_chunks, size_t num_parity_chunks)
{
    if (!data || !encoded_chunks || !chunk_size) return -1;

    const size_t k = num_data_chunks;
    const size_t m = num_parity_chunks;
    const size_t w = 8;
    const int checksum = CHKSUM_CRC32;

    if (ensure_initialized(k, m, w, checksum) != 0) return -1;

    // Encode
    char **data_frags = NULL, **parity_frags = NULL;
    uint64_t frag_len = 0;

    int rc = liberasurecode_encode(g_ec.desc,
                                   (const char *)data, (uint64_t)data_len,
                                   &data_frags, &parity_frags, &frag_len);
    if (rc != 0) {
        fprintf(stderr, "encode failed (rc=%d)\n", rc);
        return -1;
    }

    // Copy out to caller-owned buffers and report fragment size
    const size_t total = k + m;
    for (size_t i = 0; i < total; ++i) {
        const char *src = (i < k) ? data_frags[i] : parity_frags[i - k];
        encoded_chunks[i] = (uint8_t *)malloc(frag_len);
        if (!encoded_chunks[i]) {
            fprintf(stderr, "malloc failed for fragment %zu\n", i);
            // free what we've allocated so far
            for (size_t j = 0; j < i; ++j) { free(encoded_chunks[j]); encoded_chunks[j] = NULL; }
            liberasurecode_encode_cleanup(g_ec.desc, data_frags, parity_frags);
            return -1;
        }
        memcpy(encoded_chunks[i], src, frag_len);
    }
    *chunk_size = (size_t)frag_len;

    // Free the library-owned fragment arrays/buffers
    liberasurecode_encode_cleanup(g_ec.desc, data_frags, parity_frags);
    return 0;
}

/*
 * DECODE (normal, all fragments present):
 * - Caller provides:
 *     - encoded_chunks: array of size k+m, none are NULL
 *     - chunk_size: size of each fragment (as returned by encode)
 *     - num_data_chunks (=k), num_parity_chunks (=m)
 *     - decoded_data: destination buffer (caller-allocated)
 *     - *data_len: on input, capacity of decoded_data; on output, actual length
 * - Returns 0 on success, or -2 if capacity is too small (and sets *data_len to required size).
 */
int hicommand_erasure_coding_decode(uint8_t **encoded_chunks, size_t chunk_size,
                                    size_t num_data_chunks, size_t num_parity_chunks,
                                    uint8_t *decoded_data, size_t *data_len)
{
    if (!encoded_chunks || !decoded_data || !data_len) return -1;

    const size_t k = num_data_chunks;
    const size_t m = num_parity_chunks;
    const size_t w = 8;
    const int checksum = CHKSUM_CRC32;

    if (ensure_initialized(k, m, w, checksum) != 0) return -1;

    const size_t total = k + m;

    // Build the "available" array expected by liberasurecode
    char **available = (char **)calloc(total, sizeof(char *));
    if (!available) return -1;
    for (size_t i = 0; i < total; ++i) {
        // All present in "normal" decode
        available[i] = (char *)encoded_chunks[i];
    }

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
        // Tell caller how much they need
        *data_len = (size_t)decoded_len;
        liberasurecode_decode_cleanup(g_ec.desc, decoded);
        return -2; // insufficient capacity
    }

    memcpy(decoded_data, decoded, (size_t)decoded_len);
    *data_len = (size_t)decoded_len;

    liberasurecode_decode_cleanup(g_ec.desc, decoded);
    return 0;
}

/*
 * REBUILD from partial (some fragments missing):
 * - Pass in encoded_chunks of size k+m where missing entries are NULL.
 * - We’ll attempt to recover the original payload.
 * - Same return conventions as normal decode.
 */
int hicommand_erasure_coding_rebuild_from_partial(uint8_t **encoded_chunks, size_t chunk_size,
                                                  size_t num_data_chunks, size_t num_parity_chunks,
                                                  uint8_t *decoded_data, size_t *data_len)
{
    if (!encoded_chunks || !decoded_data || !data_len) return -1;

    const size_t k = num_data_chunks;
    const size_t m = num_parity_chunks;
    const size_t w = 8;
    const int checksum = CHKSUM_CRC32;

    if (ensure_initialized(k, m, w, checksum) != 0) return -1;

    const size_t total = k + m;
    char **available = (char **)calloc(total, sizeof(char *));
    if (!available) return -1;
    for (size_t i = 0; i < total; ++i) {
        available[i] = (char *)encoded_chunks[i]; // may be NULL
    }

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
