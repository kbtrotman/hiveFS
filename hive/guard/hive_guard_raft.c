/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

/**
 * Minimal pthread-based wrapper that starts the Raft protocol in a second thread.
 */

#include "hive_guard_raft.h"
#include "hive_guard_sql.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <uv.h>

struct raft_thread_ctx {
    struct hg_raft_config cfg;
    struct hg_raft_peer  *peers_copy;
    char                 *self_addr_copy;
    char                 *data_dir_copy;
};

static pthread_t            g_raft_thread;
static int                  g_thread_started = 0;
static struct raft_thread_ctx g_ctx;
static pthread_mutex_t      g_state_mu = PTHREAD_MUTEX_INITIALIZER;
static int                  g_is_leader = 0;
static int                  g_should_stop = 0;


/* Convert binary to hex string; out must be at least 2*len + 1 bytes. */
/* This helper function seems tyo be going everywhere since we sending */
/* and recieving in a lot of places.  */
static void bytes_to_hex(const uint8_t *in, size_t len, char *out)
{
    static const char hex_chars[] = "0123456789abcdef";
    for (size_t i = 0; i < len; ++i) {
        out[2 * i]     = hex_chars[(in[i] >> 4) & 0x0F];
        out[2 * i + 1] = hex_chars[in[i] & 0x0F];
    }
    out[2 * len] = '\0';
}

static void free_ctx(struct raft_thread_ctx *ctx)
{
    if (!ctx) return;
    free(ctx->self_addr_copy);
    free(ctx->data_dir_copy);
    free(ctx->peers_copy);
    memset(ctx, 0, sizeof(*ctx));
}

static void *raft_thread_main(void *arg)
{
    struct raft_thread_ctx *ctx = arg;

    fprintf(stderr,
            "hive_guard: Raft worker starting (self_id=%llu addr=%s data_dir=%s, peers=%u)\n",
            (unsigned long long)ctx->cfg.self_id,
            ctx->cfg.self_address ? ctx->cfg.self_address : "(null)",
            ctx->cfg.data_dir ? ctx->cfg.data_dir : "(null)",
            ctx->cfg.num_peers);

    /* TODO: integrate libraft/libuv here using ctx->cfg */

    while (!g_should_stop) {
        sleep(1);
    }

    fprintf(stderr, "hive_guard: Raft worker exiting\n");
    return NULL;
}

int hg_raft_init(const struct hg_raft_config *cfg)
{
    if (!cfg || !cfg->self_address || !cfg->data_dir) {
        fprintf(stderr, "hg_raft_init: invalid configuration\n");
        return -1;
    }
    if (g_thread_started) {
        return 0;
    }

    memset(&g_ctx, 0, sizeof(g_ctx));
    g_ctx.cfg = *cfg;

    if (cfg->self_address) {
        g_ctx.self_addr_copy = strdup(cfg->self_address);
        g_ctx.cfg.self_address = g_ctx.self_addr_copy;
    }
    if (cfg->data_dir) {
        g_ctx.data_dir_copy = strdup(cfg->data_dir);
        g_ctx.cfg.data_dir = g_ctx.data_dir_copy;
    }
    if (cfg->peers && cfg->num_peers) {
        g_ctx.peers_copy = calloc(cfg->num_peers, sizeof(*cfg->peers));
        if (!g_ctx.peers_copy) {
            free_ctx(&g_ctx);
            return -1;
        }
        memcpy(g_ctx.peers_copy, cfg->peers, cfg->num_peers * sizeof(*cfg->peers));
        g_ctx.cfg.peers = g_ctx.peers_copy;
    }

    g_should_stop = 0;
    int rc = pthread_create(&g_raft_thread, NULL, raft_thread_main, &g_ctx);
    if (rc != 0) {
        fprintf(stderr, "hg_raft_init: pthread_create failed: %d\n", rc);
        free_ctx(&g_ctx);
        return -1;
    }

    g_thread_started = 1;
    pthread_mutex_lock(&g_state_mu);
    g_is_leader = 1; /* until real election happens, assume leader */
    pthread_mutex_unlock(&g_state_mu);
    return 0;
}

void hg_raft_shutdown(void)
{
    if (!g_thread_started)
        return;
    g_should_stop = 1;
    pthread_join(g_raft_thread, NULL);
    g_thread_started = 0;
    pthread_mutex_lock(&g_state_mu);
    g_is_leader = 0;
    pthread_mutex_unlock(&g_state_mu);
    free_ctx(&g_ctx);
}

bool hg_guard_local_can_write(void)
{
    pthread_mutex_lock(&g_state_mu);
    bool leader = g_is_leader;
    pthread_mutex_unlock(&g_state_mu);
    return leader;
}

/* Serialize RaftPutBlock into a heap-allocated uv_buf_t.
 * Caller is responsible for free(buf->base) after submit/commit.
 */
static int raft_put_block_serialize(const struct RaftPutBlock *cmd,
                                    uv_buf_t *out_buf)
{
    if (!cmd || !out_buf) {
        return -1;
    }

    out_buf->len = sizeof(struct RaftPutBlock);
    out_buf->base = (char *)malloc(out_buf->len);
    if (!out_buf->base) {
        return -1;
    }

    memcpy(out_buf->base, cmd, out_buf->len);
    return 0;
}

/* Deserialize from uv_buf_t into a RaftPutBlock.
 * Returns 0 on success, -1 on size mismatch.
 */
static int raft_put_block_deserialize(struct RaftPutBlock *cmd,
                                      const uv_buf_t *buf)
{
    if (!cmd || !buf) {
        return -1;
    }
    if (buf->len != sizeof(struct RaftPutBlock)) {
        /* Version mismatch / corrupted entry */
        return -1;
    }
    memcpy(cmd, buf->base, sizeof(struct RaftPutBlock));
    return 0;
}

static int commitCb(struct uv_raft *raft,
                    int type,
                    const uv_buf_t *buf)
{
    (void)raft;

    if (type != RAFT_COMMAND || buf == NULL) {
        return 0;
    }

    struct RaftPutBlock cmd;
    if (raft_put_block_deserialize(&cmd, buf) != 0) {
        /* Unknown or corrupt entry; ignore */
        return 0;
    }

    if (cmd.op_type != OP_PUT_BLOCK) {
        return 0;  /* not our concern */
    }

    /* ---- 1) Hex encode the hash ---- */
    char hash_hex[65];  /* 32 bytes -> 64 hex chars + NUL */
    bytes_to_hex(cmd.hash, 32, hash_hex);

    /* ---- 2) Update hash_to_estripes (hash -> stripes, ref_count += 1) ---- */
    /* We expect 6 stripes in cmd.ec_stripes[0..5]. */
    char sql1[1024];

    /* We pass ref_count=1 so UPSERT will do: ref_count = ref_count + 1. */
    unsigned long long ref_increment = 1ULL;

    snprintf(sql1, sizeof(sql1),
             SQL_HASH_TO_ESTRIPES_UPSERT,
             (unsigned)cmd.hash_algo,
             hash_hex,
             ref_increment,
             (unsigned long long)cmd.ec_stripes[0].local_estripe_id,
             (unsigned long long)cmd.ec_stripes[1].local_estripe_id,
             (unsigned long long)cmd.ec_stripes[2].local_estripe_id,
             (unsigned long long)cmd.ec_stripes[3].local_estripe_id,
             (unsigned long long)cmd.ec_stripes[4].local_estripe_id,
             (unsigned long long)cmd.ec_stripes[5].local_estripe_id,
             (unsigned long long)0ULL  /* placeholder or spare stripe if you adapt schema */
    );

    if (hg_sql_exec(sql1) != 0) {
        fprintf(stderr, "commitCb: SQL_HASH_TO_ESTRIPES_UPSERT failed: %s\n", sql1);
        /* return error? Raft typically expects 0, but can log and keep going. */
    }

    /* ---- 3) Update volume_block_mappings (volume_id, block_no -> hash) here ---- */
    char sql2[512];

    snprintf(sql2, sizeof(sql2),
             SQL_VOLUME_BLOCK_MAP_UPSERT,
             (unsigned long long)cmd.volume_id,
             (unsigned long long)cmd.block_no,
             (unsigned)cmd.hash_algo,
             hash_hex);

    if (hg_sql_exec(sql2) != 0) {
        fprintf(stderr, "commitCb: SQL_VOLUME_BLOCK_MAP_UPSERT failed: %s\n", sql2);
    }
    /* ---- 3) Update volume_block_mappings (volume_id, block_no -> hash) here ---- */


    /* ----- 4) Update volume_inode_fingerprints here. Raft would need to carry inode + fp_index / block_no ----- */
    /* Need to add these to raft if we want to do this here.  Each RaftPutBlock should have
       command that also carries inode + fp_index / block_no, e.g.:

       hifs_meta_upsert_inode_fingerprint(cmd.volume_id, cmd.inode, cmd.fp_index,
                                          cmd.block_no, cmd.hash_algo, cmd.hash);

       using a SQL_VOLUME_INODE_FP_REPLACE macro.
    */

    return 0;
}

