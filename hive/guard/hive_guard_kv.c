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
#include "hive_guard_kv.h"
#include <string.h>
#include <stdio.h>
#include <rocksdb/c.h>

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

/* Now that we've de-coupled rocks, I need to re-make the SQL commitcb raft routine so that it calls
 * these routines and also updates the bits of mariaDB sql metadata we still have in Maria. This is
 * the bulk of the data, however, and will be very fast compared to the overhead of using SQL to 
 * talk to rocks.
 * */