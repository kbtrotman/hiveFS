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
#include <sys/stat.h>
#include <unistd.h>

#define OPENSSL_SUPPRESS_DEPRECATED
#include <openssl/sha.h>


#include "../../hifs_shared_defs.h"
#include "../common/hive_common.h"
#include "hive_guard_erasure_code.h"
#include "hive_guard_raft.h"
#include "hive_guard_sql.h"
#include "hive_guard_kv.h"
#include "hive_guard.h"
#include "hive_guard_auth.h"
#include "hive_guard_leasing.h"
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

struct hg_guard_session_entry {
    struct RaftPutSession session;
};

static pthread_mutex_t g_session_mu = PTHREAD_MUTEX_INITIALIZER;
static struct hg_guard_session_entry *g_sessions;
static size_t g_session_count;
static size_t g_session_capacity;

struct hg_guard_token_entry {
    struct RaftTokenCommand token;
};

static pthread_mutex_t g_token_mu = PTHREAD_MUTEX_INITIALIZER;
static struct hg_guard_token_entry *g_tokens;
static size_t g_token_count;
static size_t g_token_capacity;

static int raft_log_append_entry(const struct RaftCmd *cmd, uint64_t *out_idx);
static int raft_log_commit_up_to(uint64_t idx);

static void hg_guard_session_remove_locked(size_t idx)
{
    if (idx >= g_session_count)
        return;
    size_t last = g_session_count - 1;
    if (idx != last)
        g_sessions[idx] = g_sessions[last];
    g_session_count--;
}

int hifs_raft_submit_session(const struct RaftPutSession *session)
{
    if (!session)
        return -EINVAL;
    if (!hg_guard_local_can_write())
        return -EAGAIN;

    struct RaftCmd entry = {0};
    entry.op_type = HG_OP_PUT_SESSION;
    entry.u.session_put = *session;

    uint64_t idx = 0;
    int rc = raft_log_append_entry(&entry, &idx);
    if (rc != 0)
        return rc;
    return raft_log_commit_up_to(idx);
}

static int hg_guard_session_store(const struct RaftPutSession *session)
{
    if (!session)
        return -EINVAL;

    pthread_mutex_lock(&g_session_mu);
    for (size_t i = 0; i < g_session_count; ++i) {
        if (g_sessions[i].session.session_id == session->session_id) {
            g_sessions[i].session = *session;
            pthread_mutex_unlock(&g_session_mu);
            return 0;
        }
    }

    if (g_session_count == g_session_capacity) {
        size_t new_cap = g_session_capacity ? g_session_capacity * 2 : 16;
        struct hg_guard_session_entry *new_entries =
            realloc(g_sessions, new_cap * sizeof(*new_entries));
        if (!new_entries) {
            pthread_mutex_unlock(&g_session_mu);
            return -ENOMEM;
        }
        g_sessions = new_entries;
        g_session_capacity = new_cap;
    }

    g_sessions[g_session_count].session = *session;
    g_session_count++;
    pthread_mutex_unlock(&g_session_mu);
    return 0;
}

static int hg_guard_session_cleanup(const struct RaftSessionCleanup *cleanup)
{
    if (!cleanup)
        return -EINVAL;

    bool have_user = cleanup->user_name[0] != '\0';
    bool have_session = cleanup->session_id != 0;
    if (!have_user && !have_session)
        return -EINVAL;

    pthread_mutex_lock(&g_session_mu);
    size_t i = 0;
    while (i < g_session_count) {
        bool remove = false;
        const struct RaftPutSession *sess = &g_sessions[i].session;

        if (have_session && sess->session_id == cleanup->session_id)
            remove = true;
        if (!remove && have_user &&
            strncmp(sess->user_name,
                    cleanup->user_name,
                    sizeof(sess->user_name)) == 0)
            remove = true;

        if (remove) {
            hg_guard_session_remove_locked(i);
            continue;
        }
        ++i;
    }
    pthread_mutex_unlock(&g_session_mu);
    return 0;
}

static int hg_guard_token_store(const struct RaftTokenCommand *token)
{
    if (!token || token->token[0] == '\0')
        return -EINVAL;

    pthread_mutex_lock(&g_token_mu);
    for (size_t i = 0; i < g_token_count; ++i) {
        bool match = false;
        if (token->token_id != 0 &&
            g_tokens[i].token.token_id == token->token_id) {
            match = true;
        } else if (token->token[0] != '\0' &&
                   strncmp(g_tokens[i].token.token,
                           token->token,
                           sizeof(token->token)) == 0) {
            match = true;
        }
        if (match) {
            g_tokens[i].token = *token;
            pthread_mutex_unlock(&g_token_mu);
            return 0;
        }
    }

    if (g_token_count == g_token_capacity) {
        size_t new_cap = g_token_capacity ? g_token_capacity * 2 : 16;
        struct hg_guard_token_entry *new_entries =
            realloc(g_tokens, new_cap * sizeof(*new_entries));
        if (!new_entries) {
            pthread_mutex_unlock(&g_token_mu);
            return -ENOMEM;
        }
        g_tokens = new_entries;
        g_token_capacity = new_cap;
    }

    g_tokens[g_token_count].token = *token;
    g_token_count++;
    pthread_mutex_unlock(&g_token_mu);
    return 0;
}

static int hg_guard_token_expire(const struct RaftTokenExpireCommand *cmd)
{
    if (!cmd)
        return -EINVAL;

    pthread_mutex_lock(&g_token_mu);
    size_t i = 0;
    bool removed = false;
    while (i < g_token_count) {
        const struct RaftTokenCommand *entry = &g_tokens[i].token;
        bool match = false;
        if (cmd->token_id != 0 && entry->token_id == cmd->token_id)
            match = true;
        else if (cmd->token[0] != '\0' &&
                 strncmp(entry->token, cmd->token, sizeof(cmd->token)) == 0)
            match = true;

        if (match) {
            size_t last = g_token_count - 1;
            if (i != last)
                g_tokens[i] = g_tokens[last];
            g_token_count--;
            removed = true;
            continue;
        }
        ++i;
    }
    pthread_mutex_unlock(&g_token_mu);
    return removed ? 0 : -ENOENT;
}

static int hg_guard_refresh_tokens_from_sql(void)
{
    struct RaftTokenCommand *tokens = NULL;
    size_t count = 0;
    int rc = hg_sql_load_tokens(&tokens, &count);
    if (rc != 0)
        return rc;

    pthread_mutex_lock(&g_token_mu);
    free(g_tokens);
    g_tokens = NULL;
    g_token_count = 0;
    g_token_capacity = 0;
    pthread_mutex_unlock(&g_token_mu);

    for (size_t i = 0; i < count; ++i) {
        int store_rc = hg_guard_token_store(&tokens[i]);
        if (store_rc != 0) {
            hifs_warning("hg_guard_refresh_tokens_from_sql: store failed idx=%zu rc=%d",
                         i, store_rc);
            rc = store_rc;
            break;
        }
    }

    free(tokens);
    return rc;
}

extern MYSQL *hg_sql_get_db(void);

// Also provide these from config somewhere:
extern const char *g_snapshot_dir;         // e.g. "/var/lib/hivefs/snapshots"
extern const char *g_mysqldump_path;       // e.g. "/usr/bin/mysqldump"
extern const char *g_mysql_defaults_file;  // e.g. "/etc/hivefs/mysql-backup.cnf"
extern const char *g_mysql_db_name;        // e.g. "hive_meta"

static const char *hg_snapshot_base_dir_path(void)
{
    if (g_snapshot_dir && *g_snapshot_dir)
        return g_snapshot_dir;
    return HIVE_GUARD_SNAPSHOT_BASE_DIR;
}

static const char *hg_snapshot_meta_filename(void)
{
    const char *slash = strrchr(HIVE_GUARD_SNAPSHOT_META_FILE, '/');
    return slash ? slash + 1 : HIVE_GUARD_SNAPSHOT_META_FILE;
}

static int hg_snapshot_meta_path(char *buf, size_t buf_sz)
{
    if (!buf || buf_sz == 0)
        return -EINVAL;
    const char *base = hg_snapshot_base_dir_path();
    size_t base_len = strlen(base);
    bool has_sep = (base_len > 0 && base[base_len - 1] == '/');
    int written = snprintf(buf,
                           buf_sz,
                           "%s%s%s",
                           base,
                           has_sep ? "" : "/",
                           hg_snapshot_meta_filename());
    if (written < 0 || (size_t)written >= buf_sz)
        return -ENAMETOOLONG;
    return 0;
}

static int hg_snapshot_ensure_dir(const char *dir, mode_t mode)
{
    if (!dir || !*dir)
        return -EINVAL;
    if (mkdir(dir, mode) != 0 && errno != EEXIST)
        return -errno;
    return 0;
}

/* Convert binary to hex string; out must be at least 2*len + 1 bytes. */
static void __attribute__((unused)) bytes_to_hex(const uint8_t *in, size_t len, char *out)
{
    static const char hex_chars[] = "0123456789abcdef";
    for (size_t i = 0; i < len; ++i) {
        out[2 * i]     = hex_chars[(in[i] >> 4) & 0x0F];
        out[2 * i + 1] = hex_chars[in[i] & 0x0F];
    }
    out[2 * len] = '\0';
}

static int hg_snapshot_sha256_hex(const char *path, char *out_hex, size_t out_len)
{
    if (!path || !out_hex || out_len < (SHA256_DIGEST_LENGTH * 2 + 1))
        return -EINVAL;

    FILE *fp = fopen(path, "rb");
    if (!fp)
        return -errno;

    SHA256_CTX ctx;
    if (SHA256_Init(&ctx) != 1) {
        fclose(fp);
        return -EIO;
    }

    unsigned char buf[256 * 1024];
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        if (SHA256_Update(&ctx, buf, n) != 1) {
            fclose(fp);
            return -EIO;
        }
    }
    if (ferror(fp)) {
        int err = -errno;
        fclose(fp);
        return err;
    }
    fclose(fp);

    unsigned char digest[SHA256_DIGEST_LENGTH];
    if (SHA256_Final(digest, &ctx) != 1)
        return -EIO;
    bytes_to_hex(digest, SHA256_DIGEST_LENGTH, out_hex);
    return 0;
}

static int hg_snapshot_write_meta(const struct hg_local_snapshot_info *snap,
                                  char *out_path,
                                  size_t out_path_sz)
{
    if (!snap || !out_path || out_path_sz == 0)
        return -EINVAL;

    int rc = hg_snapshot_meta_path(out_path, out_path_sz);
    if (rc != 0)
        return rc;

    const char *base_dir = hg_snapshot_base_dir_path();
    rc = hg_snapshot_ensure_dir(base_dir, 0755);
    if (rc != 0)
        return rc;

    char checksum[SHA256_DIGEST_LENGTH * 2 + 1];
    rc = hg_snapshot_sha256_hex(snap->path, checksum, sizeof(checksum));
    if (rc != 0)
        return rc;

    const char *relative = snap->path;
    size_t base_len = strlen(base_dir);
    if (base_len > 0 &&
        strncmp(snap->path, base_dir, base_len) == 0) {
        relative = snap->path + base_len;
        if (*relative == '/')
            ++relative;
    }
    if (!relative || !*relative) {
        const char *slash = strrchr(snap->path, '/');
        relative = slash ? slash + 1 : snap->path;
    }

    char tmp_template[PATH_MAX];
    rc = snprintf(tmp_template, sizeof(tmp_template), "%s.tmpXXXXXX", out_path);
    if (rc < 0 || (size_t)rc >= sizeof(tmp_template))
        return -ENAMETOOLONG;

    int fd = mkstemp(tmp_template);
    if (fd < 0)
        return -errno;

    FILE *fp = fdopen(fd, "w");
    if (!fp) {
        int err = -errno;
        close(fd);
        unlink(tmp_template);
        return err;
    }

    int write_rc = 0;
    if (fprintf(fp,
                "{\"relative_path\":\"%s\",\"snapshot_id\":%llu,"
                "\"raft_index\":%llu,\"sha256\":\"%s\"}\n",
                relative,
                (unsigned long long)snap->snap_id,
                (unsigned long long)snap->raft_index,
                checksum) < 0) {
        write_rc = -EIO;
    } else if (fflush(fp) != 0) {
        write_rc = -errno;
    }

    if (write_rc == 0 && fsync(fileno(fp)) != 0)
        write_rc = -errno;

    if (fclose(fp) != 0 && write_rc == 0)
        write_rc = -errno;

    if (write_rc == 0) {
        if (rename(tmp_template, out_path) != 0)
            write_rc = -errno;
    }

    if (write_rc != 0)
        unlink(tmp_template);

    return write_rc;
}

static void hg_raft_copy_str(char *dst, size_t len, const char *src)
{
    if (!dst || len == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    size_t copy_len = len - 1;
    size_t src_len = strlen(src);
    if (copy_len > src_len)
        copy_len = src_len;
    if (copy_len > 0)
        memcpy(dst, src, copy_len);
    dst[copy_len] = '\0';
}

static const char *hg_raft_op_name(uint8_t op_type)
{
    switch (op_type) {
    case HG_OP_PUT_BLOCK: return "HG_OP_PUT_BLOCK";
    case HG_OP_PUT_INODE: return "HG_OP_PUT_INODE";
    case HG_OP_PUT_DIRENT: return "HG_OP_PUT_DIRENT";
    case HG_OP_PUT_SETTING: return "HG_OP_PUT_SETTING";
    case HG_OP_PUT_CLUSTER_CERT: return "HG_OP_PUT_CLUSTER_CERT";
    case HG_OP_PUT_CLUSTER_AUDIT: return "HG_OP_PUT_CLUSTER_AUDIT";
    case HG_OP_ATOMIC_INODE_UPDATE: return "HG_OP_ATOMIC_INODE_UPDATE";
    case HG_OP_DELETE_BLOCK: return "HG_OP_DELETE_BLOCK";
    case HG_OP_DELETE_INODE: return "HG_OP_DELETE_INODE/HG_OP_ATOMIC_RENAME";
    case HG_OP_PUT_SESSION: return "HG_OP_PUT_SESSION";
    case HG_OP_SESSION_CLEANUP: return "HG_OP_SESSION_CLEANUP";
    case HG_OP_SET_NODE_FULL_SYNC: return "HG_OP_SET_NODE_FULL_SYNC";
    case HG_OP_SET_NODE_TO_LEARNER: return "HG_OP_SET_NODE_TO_LEARNER";
    case HG_OP_PUT_JOIN_NODE: return "HG_OP_PUT_JOIN_NODE";
    case HG_OP_PUT_NODE_DOWN: return "HG_OP_PUT_NODE_DOWN";
    case HG_OP_CLUSTER_NODE_UP: return "HG_OP_CLUSTER_NODE_UP";
    case HG_OP_CLUSTER_FORCE_HEARTBEAT: return "HG_OP_CLUSTER_FORCE_HEARTBEAT";
    case HG_OP_CLUSTER_DOWN: return "HG_OP_CLUSTER_DOWN";
    case HG_OP_CLUSTER_UP: return "HG_OP_CLUSTER_UP";
    case HG_OP_CLUSTER_INIT: return "HG_OP_CLUSTER_INIT";
    case HG_OP_CLUSTER_NODE_FENCE: return "HG_OP_CLUSTER_NODE_FENCE";
    case HG_OP_STORAGE_NODE_UPDATE: return "HG_OP_STORAGE_NODE_UPDATE";
    case HG_OP_SNAPSHOT_MARK: return "HG_OP_SNAPSHOT_MARK";
    case HG_OP_LEASE_MAKE: return "HG_OP_LEASE_MAKE";
    case HG_OP_LEASE_RENEW: return "HG_OP_LEASE_RENEW";
    case HG_OP_LEASE_RELEASE: return "HG_OP_LEASE_RELEASE";
    case HG_OP_CACHE_CHECK: return "HG_OP_CACHE_CHECK";
    case HG_OP_CACHE_CLEAR: return "HG_OP_CACHE_CLEAR";
    case HG_OP_CACHE_EVICTION: return "HG_OP_CACHE_EVICTION";
    case HG_OP_CACHE_PURGE: return "HG_OP_CACHE_PURGE";
    case HG_OP_NEW_TOKEN: return "HG_OP_NEW_TOKEN";
    case HG_OP_EXPIRE_TOKEN: return "HG_OP_EXPIRE_TOKEN";
    default:
        break;
    }
    return "HG_OP_UNKNOWN";
}

static int hg_raft_handle_unimplemented(const struct RaftCmd *cmd)
{
    if (!cmd)
        return -EINVAL;

    const char *name = hg_raft_op_name(cmd->op_type);
    hifs_warning("hive_guard_raft: unimplemented raft op %s (%u)",
                 name,
                 (unsigned)cmd->op_type);
    return -ENOSYS;
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
    case HG_OP_PUT_SETTING:

    case HG_OP_PUT_CLUSTER_CERT:

    case HG_OP_PUT_CLUSTER_AUDIT:

    case HG_OP_ATOMIC_INODE_UPDATE:

    case HG_OP_DELETE_BLOCK:

    case HG_OP_DELETE_INODE:

    case HG_OP_PUT_SESSION:
        return hg_guard_session_store(&cmd.u.session_put);

    case HG_OP_SESSION_CLEANUP:
        return hg_guard_session_cleanup(&cmd.u.session_cleanup);

    case HG_OP_SET_NODE_FULL_SYNC: {
        (void)cmd.u.node_full_sync;
        return 0;
    }

    case HG_OP_PUT_JOIN_NODE: {
        const struct RaftJoinSec *src = &cmd.u.join_sec;
        struct hive_guard_sock_join_sec req = {0};

        req.cluster_id = src->cluster_id;
        req.node_id = src->node_id;
        req.min_nodes_req = src->min_nodes_req;
        req.raft_replay = src->raft_replay;
        hg_raft_copy_str(req.cluster_name, sizeof(req.cluster_name), src->cluster_name);
        hg_raft_copy_str(req.cluster_desc, sizeof(req.cluster_desc), src->cluster_desc);
        hg_raft_copy_str(req.cluster_state, sizeof(req.cluster_state), src->cluster_state);
        hg_raft_copy_str(req.database_state, sizeof(req.database_state), src->database_state);
        hg_raft_copy_str(req.kv_state, sizeof(req.kv_state), src->kv_state);
        hg_raft_copy_str(req.cont1_state, sizeof(req.cont1_state), src->cont1_state);
        hg_raft_copy_str(req.cont2_state, sizeof(req.cont2_state), src->cont2_state);
        hg_raft_copy_str(req.bootstrap_token, sizeof(req.bootstrap_token), src->bootstrap_token);
        hg_raft_copy_str(req.first_boot_ts, sizeof(req.first_boot_ts), src->first_boot_ts);
        hg_raft_copy_str(req.config_status, sizeof(req.config_status), src->config_status);
        hg_raft_copy_str(req.config_progress, sizeof(req.config_progress), src->config_progress);
        hg_raft_copy_str(req.config_msg, sizeof(req.config_msg), src->config_msg);
        hg_raft_copy_str(req.hive_version, sizeof(req.hive_version), src->hive_version);
        hg_raft_copy_str(req.hive_patch_level, sizeof(req.hive_patch_level), src->hive_patch_level);
        hg_raft_copy_str(req.pub_key, sizeof(req.pub_key), src->pub_key);
        hg_raft_copy_str(req.cduid, sizeof(req.cduid), src->cduid);
        hg_raft_copy_str(req.machine_uid, sizeof(req.machine_uid), src->machine_uid);
        hg_raft_copy_str(req.action, sizeof(req.action), src->action);
        hg_raft_copy_str(req.user_id, sizeof(req.user_id), src->user_id);
        return hive_guard_apply_join_sec(&req);
    }

    case HG_OP_PUT_NODE_DOWN: {
        (void)cmd.u.node_down;
        return 0;
    }

    case HG_OP_CLUSTER_NODE_UP: {
        (void)cmd.u.cluster_node_up;
        return 0;
    }

    case HG_OP_CLUSTER_FORCE_HEARTBEAT: {
        (void)cmd.u.cluster_force_heartbeat;
        return 0;
    }

    case HG_OP_CLUSTER_DOWN: {
        (void)cmd.u.cluster_down;
        return 0;
    }

    case HG_OP_CLUSTER_UP: {
        (void)cmd.u.cluster_up;
        return 0;
    }

    case HG_OP_CLUSTER_INIT: {
        (void)cmd.u.cluster_init;
        return 0;
    }

    case HG_OP_CLUSTER_NODE_FENCE: {
        (void)cmd.u.cluster_node_fence;
        return 0;
    }

    case HG_OP_SET_NODE_TO_LEARNER: {
        (void)cmd.u.node_to_learner;
        return 0;
    }

    case HG_OP_LEASE_MAKE:
        return hg_leasing_apply_lease_make(&cmd.u.lease);

    case HG_OP_LEASE_RENEW:
        return hg_leasing_apply_lease_renew(&cmd.u.lease);

    case HG_OP_LEASE_RELEASE:
        return hg_leasing_apply_lease_release(&cmd.u.lease);

    case HG_OP_NEW_TOKEN: {
        int rc = hg_sql_update_token_entry(&cmd.u.new_token.meta);
        if (rc != 0)
            return rc;

        rc = hg_guard_refresh_tokens_from_sql();
        if (rc != 0) {
            hifs_warning("hg_guard_refresh_tokens_from_sql failed rc=%d, using local token cache",
                         rc);
            int store_rc = hg_guard_token_store(&cmd.u.new_token);
            return store_rc != 0 ? store_rc : rc;
        }
        return 0;
    }

    case HG_OP_EXPIRE_TOKEN:
        return hg_guard_token_expire(&cmd.u.expire_token);

    case HG_OP_CACHE_CHECK:

    case HG_OP_CACHE_CLEAR:
    
    case HG_OP_CACHE_EVICTION:

    case HG_OP_CACHE_PURGE:

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

    case HG_OP_STORAGE_NODE_UPDATE: {
        const struct hive_guard_storage_update_cmd *su =
            &cmd.u.storage_update;
        hive_guard_apply_storage_node_update(su);
        return 0;
    }

    default:
        return hg_raft_handle_unimplemented(&cmd);
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

int hifs_raft_submit_join_sec(const struct hive_guard_join_context *ctx)
{
    if (!ctx)
        return -EINVAL;
    if (!hg_guard_local_can_write())
        return -EAGAIN;

    struct RaftCmd entry = {0};
    entry.op_type = HG_OP_PUT_JOIN_NODE;
    struct RaftJoinSec *dst = &entry.u.join_sec;

    dst->cluster_id = ctx->cluster_id;
    dst->node_id = ctx->node_id;
    dst->min_nodes_req = ctx->min_nodes_required;
    dst->flags = ctx->flags;
    dst->reserved = 0;

    hg_raft_copy_str(dst->cluster_state, sizeof(dst->cluster_state), ctx->cluster_state);
    hg_raft_copy_str(dst->database_state, sizeof(dst->database_state), ctx->database_state);
    hg_raft_copy_str(dst->kv_state, sizeof(dst->kv_state), ctx->kv_state);
    hg_raft_copy_str(dst->cont1_state, sizeof(dst->cont1_state), ctx->cont1_state);
    hg_raft_copy_str(dst->cont2_state, sizeof(dst->cont2_state), ctx->cont2_state);
    hg_raft_copy_str(dst->cluster_name, sizeof(dst->cluster_name), ctx->cluster_name);
    hg_raft_copy_str(dst->cluster_desc, sizeof(dst->cluster_desc), ctx->cluster_desc);
    hg_raft_copy_str(dst->bootstrap_token, sizeof(dst->bootstrap_token), NULL);
    hg_raft_copy_str(dst->first_boot_ts, sizeof(dst->first_boot_ts), ctx->first_boot_ts);
    hg_raft_copy_str(dst->config_status, sizeof(dst->config_status), ctx->config_status);
    hg_raft_copy_str(dst->config_progress, sizeof(dst->config_progress), ctx->config_progress);
    hg_raft_copy_str(dst->config_msg, sizeof(dst->config_msg), ctx->config_message);
    hg_raft_copy_str(dst->hive_version, sizeof(dst->hive_version), ctx->hive_version);
    hg_raft_copy_str(dst->hive_patch_level, sizeof(dst->hive_patch_level), ctx->hive_patch_level);
    hg_raft_copy_str(dst->pub_key, sizeof(dst->pub_key), NULL);
    hg_raft_copy_str(dst->cduid, sizeof(dst->cduid), ctx->cduid);

    const char *machine_uid = ctx->machine_uid;
    if ((!machine_uid || !machine_uid[0]) &&
        ctx->node_record && ctx->node_record->uid[0])
        machine_uid = ctx->node_record->uid;
    hg_raft_copy_str(dst->machine_uid, sizeof(dst->machine_uid), machine_uid);
    hg_raft_copy_str(dst->action, sizeof(dst->action), ctx->action);
    hg_raft_copy_str(dst->user_id, sizeof(dst->user_id), ctx->user_id);

    dst->raft_replay = ctx->raft_replay;
    dst->reserved_i32 = 0;

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

int hifs_raft_submit_storage_update(const struct hive_guard_storage_update_cmd *cmd)
{
    if (!cmd)
        return -EINVAL;
    if (!hg_guard_local_can_write())
        return -EAGAIN;

    struct RaftCmd entry = {0};
    entry.op_type = HG_OP_STORAGE_NODE_UPDATE;
    entry.u.storage_update = *cmd;

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

    char meta_path[PATH_MAX];
    rc = hg_snapshot_write_meta(&local, meta_path, sizeof(meta_path));
    if (rc != 0)
        return rc;

    rc = tcp_send_file_to_new_node(out_src->source_addr,
                                   meta_path,
                                   new_node_addr);
    if (rc != 0)
        return rc;

    // 4) Ready to transfer:
    return tcp_send_file_to_new_node(out_src->source_addr,
                                     out_src->local_path,
                                     new_node_addr);
}

static uint64_t
hg_token_metadata_make_id(const struct hive_guard_token_metadata *meta)
{
    if (!meta)
        return 0;

    const struct {
        const char *value;
        size_t max_len;
    } fields[] = {
        { meta->tid, sizeof(meta->tid) },
        { meta->bootstrap_token, sizeof(meta->bootstrap_token) },
        { meta->host_mid, sizeof(meta->host_mid) },
        { meta->host_uuid, sizeof(meta->host_uuid) },
    };

    for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i) {
        const char *val = fields[i].value;
        if (!val || val[0] == '\0')
            continue;
        uint64_t hash = 1469598103934665603ULL;
        size_t len = strnlen(val, fields[i].max_len);
        for (size_t j = 0; j < len; ++j) {
            hash ^= (uint64_t)(unsigned char)val[j];
            hash *= 1099511628211ULL;
        }
        if (hash != 0)
            return hash;
    }
    return 0;
}

static uint32_t hg_token_metadata_type_code(const char *type)
{
    if (!type || type[0] == '\0')
        return 0;

    uint32_t hash = 2166136261u;
    size_t len = strnlen(type, HIVE_GUARD_TOKEN_TYPE_LEN);
    for (size_t i = 0; i < len; ++i) {
        hash ^= (uint32_t)(unsigned char)type[i];
        hash *= 16777619u;
    }
    return hash;
}

static const char *
hg_token_metadata_pick_uid(const struct hive_guard_token_metadata *meta)
{
    if (!meta)
        return NULL;
    if (meta->host_mid[0] != '\0')
        return meta->host_mid;
    if (meta->host_uuid[0] != '\0')
        return meta->host_uuid;
    return NULL;
}

int hg_raft_call_update_token(const struct hive_guard_token_metadata *meta)
{
    if (!meta || meta->bootstrap_token[0] == '\0')
        return -EINVAL;
    if (!hg_guard_local_can_write())
        return -EAGAIN;

    struct RaftCmd entry = {0};
    entry.op_type = HG_OP_NEW_TOKEN;
    struct RaftTokenCommand *dst = &entry.u.new_token;

    dst->token_id = hg_token_metadata_make_id(meta);
    dst->expires_ns = 0;
    dst->token_type = hg_token_metadata_type_code(meta->token_type);
    dst->flags = meta->has_cluster_id ? 1u : 0u;
    hg_raft_copy_str(dst->machine_uid,
                     sizeof(dst->machine_uid),
                     hg_token_metadata_pick_uid(meta));
    hg_raft_copy_str(dst->token, sizeof(dst->token), meta->bootstrap_token);
    dst->meta = *meta;

    uint64_t idx = 0;
    int rc = raft_log_append_entry(&entry, &idx);
    if (rc != 0)
        return rc;
    return raft_log_commit_up_to(idx);
}
