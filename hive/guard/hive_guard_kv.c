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
 * Simple KV store interface using RocksDB
 */
static int apply_put_block_to_kv(const struct RaftPutBlock *cmd)
{
    // build vb: key
    char vb_key[128];
    int vb_len = snprintf(vb_key, sizeof(vb_key),
                          "vb:%016llx:%016llx",
                          (unsigned long long)cmd->volume_id,
                          (unsigned long long)cmd->block_no);

    // build h2s: key + value struct
    char hash_hex[65];
    hifs_hash_to_hex(cmd->hash, hash_hex, sizeof(hash_hex));

    char h2s_key[80];
    int h2s_len = snprintf(h2s_key, sizeof(h2s_key),
                           "h2s:%s", hash_hex);

                           // build h2s: value here!!!!!!!!!!!!!!!!!!!
    struct H2SValue val = { ... fill from cmd.ec_stripes ... };

    // use WriteBatch if you want both to be atomic
    rocksdb_writebatch_t *wb = rocksdb_writebatch_create();
    rocksdb_writebatch_put(wb, vb_key, vb_len,
                           hash_hex, strlen(hash_hex));
    rocksdb_writebatch_put(wb, h2s_key, h2s_len,
                           (const char *)&val, sizeof(val));

    char *err = NULL;
    rocksdb_writeoptions_t *wopts = rocksdb_writeoptions_create();
    rocksdb_write(g_kv_db, wopts, wb, &err);
    rocksdb_writeoptions_destroy(wopts);
    rocksdb_writebatch_destroy(wb);

    if (err != NULL) {
        fprintf(stderr, "rocksdb_write error: %s\n", err);
        free(err);
        return -1;
    }
    return 0;
}

/* Simple KV store interface using RocksDB, non-atomic */
int kv_put(const char *key, size_t key_len,
           const char *val, size_t val_len)
{
    char *err = NULL;
    rocksdb_writeoptions_t *wopts = rocksdb_writeoptions_create();
    // tweak sync/fsync here if needed; default is async.
    rocksdb_put(g_kv_db, wopts, key, key_len, val, val_len, &err);
    rocksdb_writeoptions_destroy(wopts);

    if (err != NULL) {
        fprintf(stderr, "rocksdb_put error: %s\n", err);
        free(err);
        return -1;
    }
    return 0;
}

int kv_get(const char *key, size_t key_len,
           char **val_out, size_t *val_len_out)
{
    char *err = NULL;
    rocksdb_readoptions_t *ropts = rocksdb_readoptions_create();
    size_t vlen = 0;
    char *val  = rocksdb_get(g_kv_db, ropts, key, key_len, &vlen, &err);
    rocksdb_readoptions_destroy(ropts);

    if (err != NULL) {
        fprintf(stderr, "rocksdb_get error: %s\n", err);
        free(err);
        return -1;
    }

    *val_out      = val;    // caller must free() val
    *val_len_out  = vlen;
    return 0;
}

#include <rocksdb/c.h>

static rocksdb_t *g_kv_db = NULL;

int kv_init(const char *db_path)
{
    char *err = NULL;
    rocksdb_options_t *opts = rocksdb_options_create();
    rocksdb_options_set_create_if_missing(opts, 1);
    // Additional tuning: block cache, write buffer size, etc. later.

    g_kv_db = rocksdb_open(opts, db_path, &err);
    rocksdb_options_destroy(opts);

    if (err != NULL) {
        fprintf(stderr, "rocksdb_open error: %s\n", err);
        free(err);
        return -1;
    }

    return 0;
}

void kv_shutdown(void)
{
    if (g_kv_db != NULL) {
        rocksdb_close(g_kv_db);
        g_kv_db = NULL;
    }
}
