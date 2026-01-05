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

#include <errno.h>

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


#include "../../hifs_shared_defs.h"
#include "hive_guard_erasure_code.h"
#include "hive_guard_raft.h"
#include "hive_guard_sql.h"
#include "hive_guard_kv.h"
#include "hive_guard.h"
#include "hive_guard_sn_tcp.h"

struct uv_raft;

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
static pthread_mutex_t      g_log_mu = PTHREAD_MUTEX_INITIALIZER;
#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
static _Thread_local uint64_t g_thread_committed_index;
#else
static __thread uint64_t g_thread_committed_index;
#endif

struct raft_log_state {
    struct RaftCmd *entries;
    size_t          capacity;
    size_t          head;
    size_t          size;
    uint64_t        next_index;
    uint64_t        committed_index;
};

static struct raft_log_state g_raft_log = {
    .entries = NULL,
    .capacity = 0,
    .head = 0,
    .size = 0,
    .next_index = 1,
    .committed_index = 0,
};

static pthread_mutex_t g_snap_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_snap_cv = PTHREAD_COND_INITIALIZER;
static struct hg_local_snapshot_info g_last_snap = {
    .status = 1
};

extern MYSQL *hg_sql_get_db(void);

// Also provide these from config somewhere:
extern const char *g_snapshot_dir;         // e.g. "/var/lib/hivefs/snapshots"
extern const char *g_mysqldump_path;       // e.g. "/usr/bin/mysqldump"
extern const char *g_mysql_defaults_file;  // e.g. "/etc/hivefs/mysql-backup.cnf"
extern const char *g_mysql_db_name;        // e.g. "hive_meta"

/* Convert binary to hex string; out must be at least 2*len + 1 bytes. */
/* This helper function seems tyo be going everywhere since we sending */
/* and recieving in a lot of places.  */
static void __attribute__((unused)) bytes_to_hex(const uint8_t *in, size_t len, char *out)
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
    if (ctx->peers_copy) {
        for (unsigned i = 0; i < ctx->cfg.num_peers; ++i)
            free((char *)ctx->peers_copy[i].address);
    }
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
        for (unsigned i = 0; i < cfg->num_peers; ++i) {
            g_ctx.peers_copy[i] = cfg->peers[i];
            if (cfg->peers[i].address) {
                char *dup = strdup(cfg->peers[i].address);
                if (!dup) {
                    for (unsigned j = 0; j < i; ++j)
                        free((char *)g_ctx.peers_copy[j].address);
                    free(g_ctx.peers_copy);
                    g_ctx.peers_copy = NULL;
                    free_ctx(&g_ctx);
                    return -1;
                }
                g_ctx.peers_copy[i].address = dup;
            }
        }
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


/* The serialization/commit helpers below are sketches for the future Raft
 * integration. They are kept here for reference. */
static int __attribute__((unused))
raft_put_block_serialize(const struct RaftPutBlock *cmd,
                         uv_buf_t *out_buf)
{
    if (!cmd || !out_buf) {
        return -1;
    }

    struct RaftCmd rcmd = {0};
    rcmd.op_type = HG_OP_PUT_BLOCK;
    rcmd.u.block = *cmd;

    out_buf->len = sizeof(struct RaftCmd);
    out_buf->base = (char *)malloc(out_buf->len);
    if (!out_buf->base) {
        return -1;
    }

    memcpy(out_buf->base, &rcmd, out_buf->len);
    return 0;
}

static int __attribute__((unused))
raft_put_inode_serialize(const struct RaftPutInode *cmd,
                         uv_buf_t *out_buf)
{
    if (!cmd || !out_buf) {
        return -1;
    }

    struct RaftCmd rcmd = {0};
    rcmd.op_type   = HG_OP_PUT_INODE;
    rcmd.u.inode   = *cmd;

    out_buf->len = sizeof(struct RaftCmd);
    out_buf->base = (char *)malloc(out_buf->len);
    if (!out_buf->base) {
        return -1;
    }

    memcpy(out_buf->base, &rcmd, out_buf->len);
    return 0;
}

static int raft_put_deserialize(struct RaftCmd *cmd,
                                const uv_buf_t *buf)
{
    if (!cmd || !buf) {
        return -1;
    }
    if (buf->len != sizeof(struct RaftCmd)) {
        return -1;
    }
    memcpy(cmd, buf->base, sizeof(struct RaftCmd));
    return 0;
}

static int __attribute__((unused))
commitCb(struct uv_raft *raft,
         int type,
         const uv_buf_t *buf)
{
    (void)raft;

    if (type != RAFT_COMMAND || buf == NULL) {
        return 0;
    }

    struct RaftCmd cmd;
    if (raft_put_deserialize(&cmd, buf) != 0)
        return 0;

    switch (cmd.op_type) {

    case HG_OP_PUT_INODE: {
        const struct RaftPutInode *pi = &cmd.u.inode;
        if (!hifs_volume_inode_store(pi->volume_id, &pi->inode))
            return -EIO;
        if (pi->fp_index < HIFS_MAX_BLOCK_HASHES) {
            const struct hifs_block_fingerprint_wire *fp =
                &pi->inode.i_block_fingerprints[pi->fp_index];
            
            if (storage_node_id)
            if (!hifs_volume_inode_fp_replace(pi->volume_id,
                                              pi->inode_id,
                                              pi->fp_index,
                                              fp))
                return -EIO;
        }
        return 0;
    }

    case HG_OP_PUT_BLOCK: {
        ///// THIS NEEDS TO BE CHANGED TO HANDLE STRIPE LOCATIONS ////
        ///// BASED ON ERASURE CODING ////////////////////////////////
        /////////////////////////////////////////////////////////////
        ///// THE FOLLOWING IS A TEMPORARY PLACEHOLDER //////////////
        /////// FOR TESTING PURPOSES ONLY //////////////////////////
        hg_kv_apply_put_block(&cmd.u.block); /////TESTING ONLY/////
        ////////////////////////////////////////////////////////////
        struct stripe_location locs[HIFS_EC_STRIPES];
        for (size_t i = 0; i < HIFS_EC_STRIPES; ++i) {
            locs[i].stripe_index = i;
            locs[i].storage_node_id = cmd.u.block.ec_stripes[i].storage_node_id;
            locs[i].shard_id = cmd.u.block.ec_stripes[i].shard_id;
            locs[i].estripe_id = cmd.u.block.ec_stripes[i].estripe_id;
            locs[i].block_offset = cmd.u.block.ec_stripes[i].block_offset;
        }

        // TODO:
        //NO! This test is not actually right! It will just loop 6 times on every node
        //and still write on every node. Need to figure out how raft keeps an index
        // for the hosts in a cluster.
        if (locs[0].storage_node_id == storage_node_id) {
            /* This is the driod we're looking for */

            // Commented out for testing purposes only.
            //hg_kv_apply_put_block(storage_node_id, &cmd.u.block); 

            hifs_store_block_to_stripe_locations(cmd.u.block.volume_id,
                                            cmd.u.block.block_no,
                                            cmd.u.block.hash_algo,
                                            cmd.u.block.hash,
                                            locs,
                                            HIFS_EC_STRIPES);
        }
        
        hifs_guard_notify_write_ack(cmd.u.block.volume_id,
                                    cmd.u.block.block_no,
                                    cmd.u.block.hash,
                                    HIFS_BLOCK_HASH_SIZE);
        return 0;
    }
    case HG_OP_PUT_DIRENT: {
        const struct RaftPutDirent *pd = &cmd.u.dirent;
        if (!hifs_volume_dentry_store(pd->volume_id,
                                      (const struct hifs_volume_dentry *)&pd->dirent))
            return -EIO;
        return 0;
    }
    case HG_OP_SNAPSHOT_MARK: {
        MYSQL *db = hg_sql_get_db();
        if (!db) return -EIO;

        const uint64_t snap_id = cmd.u.snapshot.snap_id;

        /* Commit loop copies the index into a thread-local so each callback sees
         * the entry it is applying even when multiple threads are advancing the
         * log concurrently. */
        const uint64_t snap_index = g_thread_committed_index;

        // 1) Persist marker row (your existing bookkeeping function)
        int rc = hg_sql_snapshot_take(db, snap_id, snap_index);
        if (rc != 0) goto publish_fail;

        // 2) Create the actual artifact file
        char path[PATH_MAX];
        rc = hg_sql_snapshot_create_artifact(g_snapshot_dir,
                                            g_mysqldump_path,
                                            g_mysql_defaults_file,
                                            g_mysql_db_name,
                                            snap_id,
                                            snap_index,
                                            path,
                                            sizeof(path));
        if (rc != 0) goto publish_fail;

        // 3) Publish readiness so join orchestration can pick a source + transfer file to new node
        pthread_mutex_lock(&g_snap_mu);
        g_last_snap.snap_id = snap_id;
        g_last_snap.raft_index = snap_index;
        g_last_snap.status = 0;
        strncpy(g_last_snap.path, path, sizeof(g_last_snap.path)-1);
        g_last_snap.path[sizeof(g_last_snap.path)-1] = '\0';
        g_last_snap.size_bytes = 0; 
        // (optional: stat() the file and store size)
        pthread_cond_broadcast(&g_snap_cv);
        pthread_mutex_unlock(&g_snap_mu);

        return 0;

    publish_fail:
        pthread_mutex_lock(&g_snap_mu);
        g_last_snap.snap_id = snap_id;
        g_last_snap.raft_index = snap_index;
        g_last_snap.status = rc;
        g_last_snap.path[0] = '\0';
        g_last_snap.size_bytes = 0;
        pthread_cond_broadcast(&g_snap_cv);
        pthread_mutex_unlock(&g_snap_mu);
        return rc;
    }


    default:
        break;
    }
    return 0;
}

static int raft_log_ensure_capacity_locked(size_t extra)
{
    if (extra == 0)
        return 0;

    size_t needed = g_raft_log.size + extra;
    if (needed <= g_raft_log.capacity) {
        if (g_raft_log.size == 0)
            g_raft_log.head = 0;
        if (g_raft_log.head + needed <= g_raft_log.capacity || g_raft_log.size == 0)
            return 0;
        if (g_raft_log.size > 0) {
            memmove(g_raft_log.entries,
                    g_raft_log.entries + g_raft_log.head,
                    g_raft_log.size * sizeof(*g_raft_log.entries));
        }
        g_raft_log.head = 0;
        return 0;
    }

    size_t new_cap = g_raft_log.capacity ? g_raft_log.capacity : 16;
    while (new_cap < needed)
        new_cap *= 2;

    struct RaftCmd *new_entries = malloc(new_cap * sizeof(*new_entries));
    if (!new_entries)
        return -ENOMEM;

    if (g_raft_log.size > 0 && g_raft_log.entries) {
        memcpy(new_entries,
               g_raft_log.entries + g_raft_log.head,
               g_raft_log.size * sizeof(*new_entries));
    }

    free(g_raft_log.entries);
    g_raft_log.entries = new_entries;
    g_raft_log.capacity = new_cap;
    g_raft_log.head = 0;
    return 0;
}

static int raft_log_append_entry(const struct RaftCmd *cmd, uint64_t *out_idx)
{
    if (!cmd || !out_idx)
        return -EINVAL;

    pthread_mutex_lock(&g_log_mu);
    int rc = raft_log_ensure_capacity_locked(1);
    if (rc != 0) {
        pthread_mutex_unlock(&g_log_mu);
        return rc;
    }

    size_t pos = g_raft_log.head + g_raft_log.size;
    g_raft_log.entries[pos] = *cmd;
    uint64_t idx = g_raft_log.next_index++;
    g_raft_log.size++;
    pthread_mutex_unlock(&g_log_mu);

    *out_idx = idx;
    return 0;
}

static int raft_log_commit_up_to(uint64_t idx)
{
    if (idx == 0)
        return -EINVAL;

    uint64_t committed_idx = 0;
    for (;;) {
        struct RaftCmd entry;

        pthread_mutex_lock(&g_log_mu);
        uint64_t highest = g_raft_log.next_index ? (g_raft_log.next_index - 1) : 0;
        if (idx > highest) {
            pthread_mutex_unlock(&g_log_mu);
            return -EINVAL;
        }
        if (g_raft_log.committed_index >= idx) {
            pthread_mutex_unlock(&g_log_mu);
            break;
        }
        if (g_raft_log.size == 0) {
            pthread_mutex_unlock(&g_log_mu);
            return -EIO;
        }

        entry = g_raft_log.entries[g_raft_log.head];
        g_raft_log.head++;
        g_raft_log.size--;
        g_raft_log.committed_index++;
        committed_idx = g_raft_log.committed_index;
        if (g_raft_log.size == 0)
            g_raft_log.head = 0;
        pthread_mutex_unlock(&g_log_mu);

        g_thread_committed_index = committed_idx;
        uv_buf_t buf = {
            .base = (char *)&entry,
            .len = sizeof(entry),
        };
        int rc = commitCb(NULL, RAFT_COMMAND, &buf);
        if (rc != 0)
            return rc;
    }

    return 0;
}


int hifs_raft_submit_put_block(const struct RaftPutBlock *cmd)
{
    if (!cmd)
        return -EINVAL;

    if (!hg_guard_local_can_write())
        return -EAGAIN;

    struct RaftCmd entry = {0};
    entry.op_type = HG_OP_PUT_BLOCK;
    entry.u.block = *cmd;

    uint64_t idx = 0;
    int rc = raft_log_append_entry(&entry, &idx);
    if (rc != 0)
        return rc;

    return raft_log_commit_up_to(idx);
}

int hifs_raft_submit_put_dirent(const struct RaftPutDirent *cmd)
{
    if (!cmd)
        return -EINVAL;
    if (!hg_guard_local_can_write())
        return -EAGAIN;

    struct RaftCmd entry = {0};
    entry.op_type = HG_OP_PUT_DIRENT;
    entry.u.dirent = *cmd;

    uint64_t idx = 0;
    int rc = raft_log_append_entry(&entry, &idx);
    if (rc != 0)
        return rc;
    return raft_log_commit_up_to(idx);
}

// raft snapshot instance:
int hifs_raft_submit_snapshot_mark(uint64_t snap_id)
{
    if (!hg_guard_local_can_write())
        return -EAGAIN;

    struct RaftCmd entry = {0};
    entry.op_type = HG_OP_SNAPSHOT_MARK;
    entry.u.snapshot.snap_id = snap_id;

    uint64_t idx = 0;
    int rc = raft_log_append_entry(&entry, &idx);
    if (rc != 0)
        return rc;

    return raft_log_commit_up_to(idx);
}

static int hg_wait_local_snapshot_ready(uint64_t snap_id,
                                        uint64_t min_index,
                                        struct hg_local_snapshot_info *out,
                                        int timeout_ms)
{
    if (!out) return -EINVAL;

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_ms / 1000;
    ts.tv_nsec += (timeout_ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }

    pthread_mutex_lock(&g_snap_mu);
    for (;;) {
        if (g_last_snap.snap_id == snap_id &&
            g_last_snap.raft_index >= min_index &&
            g_last_snap.status == 0)
        {
            *out = g_last_snap;
            pthread_mutex_unlock(&g_snap_mu);
            return 0;
        }
        if (g_last_snap.snap_id == snap_id && g_last_snap.status < 0) {
            int err = g_last_snap.status;
            pthread_mutex_unlock(&g_snap_mu);
            return err;
        }

        int rc = pthread_cond_timedwait(&g_snap_cv, &g_snap_mu, &ts);
        if (rc == ETIMEDOUT) {
            pthread_mutex_unlock(&g_snap_mu);
            return -ETIMEDOUT;
        }
    }
}

int hg_prepare_snapshot_for_new_node(const struct hg_raft_config *cfg,
                                    uint64_t snap_id,
                                    const char *new_node_addr,
                                    struct hg_snapshot_source *out_src)
{
    if (!cfg || !out_src || !new_node_addr || !*new_node_addr) return -EINVAL;
    memset(out_src, 0, sizeof(*out_src));

    if (!hg_guard_local_can_write()) {
        // Only the leader should coordinate this
        return -EAGAIN;
    }

    // 1) Replicate snapshot marker to Raft log
    int rc = hifs_raft_submit_snapshot_mark(snap_id);
    if (rc != 0) {
        return rc;
    }

    struct hg_local_snapshot_info local = {0};
    rc = hg_wait_local_snapshot_ready(snap_id,
                                      0 /* any index >= the mark */,
                                      &local,
                                      60000);
    if (rc != 0)
        return rc;

    // 3) Pick a source node
    // This will always be the leader, unless leader changes during the process.
    out_src->snap_id = local.snap_id;
    out_src->raft_index = local.raft_index;
    strncpy(out_src->local_path, local.path, sizeof(out_src->local_path)-1);
    out_src->local_path[sizeof(out_src->local_path)-1] = '\0';
    strncpy(out_src->source_addr, cfg->self_address, sizeof(out_src->source_addr)-1);
    out_src->source_addr[sizeof(out_src->source_addr)-1] = '\0';

    // 4) Ready to transfer:
    return tcp_send_file_to_new_node(out_src->source_addr,
                                     out_src->local_path,
                                     new_node_addr);
}
