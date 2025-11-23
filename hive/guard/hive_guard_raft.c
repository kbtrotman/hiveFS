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

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

static int commitCb(struct uv_raft *raft,
                    int type,
                    const uv_buf_t *buf)
{
    (void)raft;

    if (type != RAFT_COMMAND || buf == NULL || buf->len < sizeof(struct RaftPutBlock)) {
        return 0;
    }

    const struct RaftPutBlock *cmd = (const struct RaftPutBlock *)buf->base;
    if (cmd->op_type != OP_PUT_BLOCK) {
        return 0;
    }

    // Pseudo-code: insert into hash_to_estripes
    hifs_meta_insert_hash_to_estripes(
        cmd->hash_algo,
        cmd->hash,
        cmd->estripe_id[0],
        cmd->estripe_id[1],
        cmd->estripe_id[2],
        cmd->estripe_id[3],
        cmd->estripe_id[4],
        cmd->estripe_id[5]);

    return 0;
}

