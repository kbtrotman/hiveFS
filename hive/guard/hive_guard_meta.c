#include <endian.h>
#include <errno.h>
#include <openssl/sha.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "hive_guard_meta.h"
#include "hive_guard_mcl.h"

struct hifs_meta_committed_map {
    uint64_t volume_id;
    uint64_t inode_key;
    hifs_object_id_t map_root_id;
    uint64_t last_commit_ns;
    uint32_t commit_count;
};

static void hifs_meta_sha256_begin_tagged(SHA256_CTX *ctx, const char *tag);
static void hifs_meta_sha256_finish(SHA256_CTX *ctx, hifs_object_id_t *out);
static void hifs_meta_sha256_finish_bytes(SHA256_CTX *ctx, uint8_t *out_bytes);
static void hifs_meta_sha256_update_id(SHA256_CTX *ctx, const hifs_object_id_t *id);
static void hifs_meta_sha256_update_u64(SHA256_CTX *ctx, uint64_t value);
static void hifs_meta_sha256_update_u32(SHA256_CTX *ctx, uint32_t value);

static void hifs_meta_make_volume_identity(uint64_t volume_id,
                                           uint64_t sb_epoch,
                                           const hifs_object_id_t *root_inode_id,
                                           const hifs_object_id_t *root_dir_node_id,
                                           const hifs_object_id_t *policy_hash,
                                           hifs_object_id_t *root_id_out,
                                           hifs_object_id_t *volume_id_out)
{
    SHA256_CTX ctx;
    hifs_object_id_t digest;

    hifs_meta_sha256_begin_tagged(&ctx, "VOLROOT");
    hifs_meta_sha256_update_u64(&ctx, volume_id);
    hifs_meta_sha256_update_u64(&ctx, sb_epoch);
    hifs_meta_sha256_update_id(&ctx, root_inode_id);
    hifs_meta_sha256_update_id(&ctx, root_dir_node_id);
    hifs_meta_sha256_update_id(&ctx, policy_hash);
    hifs_meta_sha256_finish(&ctx, &digest);

    if (root_id_out) {
        *root_id_out = digest;
    }
    if (volume_id_out) {
        *volume_id_out = digest;
    }
}

static int hifs_meta_log_deltas_to_mcl(const struct hifs_meta_map_builder *bld,
                                       uint64_t txn_base)
{
    struct hifs_mcl_map_delta_rec rec;

    if (!bld || !bld->deltas || bld->delta_count == 0) {
        errno = EINVAL;
        return -1;
    }

    for (size_t i = 0; i < bld->delta_count; i++) {
        const struct hifs_meta_map_delta *delta = &bld->deltas[i];

        memset(&rec, 0, sizeof(rec));
        rec.txn_id = txn_base + (uint64_t)i;
        rec.volume_id = bld->volume_id;
        rec.inode_key = bld->inode_key;
        rec.block_no = delta->block_no;
        rec.generation = delta->generation;
        memcpy(rec.content_hash, delta->content_hash, HIFS_BLOCK_HASH_SIZE);
        memcpy(rec.stripe_id, delta->stripe_id, HIFS_STRIPE_ID_SIZE);

        if (hifs_mcl_append(&g_hive_guard_mcl_ctx,
                             CHANGE_MAP_DELTA,
                             HIFS_MCL_DELTA_MAP_APPEND,
                             &rec,
                             sizeof(rec),
                             rec.txn_id,
                             bld->inode_key) != 0) {
            return -1;
        }
    }

    return 0;
}

static uint64_t hifs_meta_now_ns(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void hifs_meta_sha256_update_u64(SHA256_CTX *ctx, uint64_t value)
{
    const uint64_t le = htole64(value);
    SHA256_Update(ctx, &le, sizeof(le));
}

static void hifs_meta_sha256_update_u32(SHA256_CTX *ctx, uint32_t value)
{
    const uint32_t le = htole32(value);
    SHA256_Update(ctx, &le, sizeof(le));
}

static int hifs_meta_ctx_reserve(struct hifs_meta_ctx *ctx, size_t needed)
{
    size_t new_cap;
    struct hifs_meta_committed_map *new_maps;

    if (ctx->map_cap >= needed) {
        return 0;
    }

    new_cap = ctx->map_cap ? ctx->map_cap * 2u : 8u;
    if (new_cap < needed) {
        new_cap = needed;
    }

    new_maps = realloc(ctx->maps, new_cap * sizeof(*new_maps));
    if (!new_maps) {
        return -1;
    }

    ctx->maps = new_maps;
    ctx->map_cap = new_cap;
    return 0;
}

static ssize_t hifs_meta_ctx_find_map(struct hifs_meta_ctx *ctx,
                                      uint64_t volume_id,
                                      uint64_t inode_key)
{
    for (size_t i = 0; i < ctx->map_count; i++) {
        if (ctx->maps[i].volume_id == volume_id &&
            ctx->maps[i].inode_key == inode_key) {
            return (ssize_t)i;
        }
    }

    return -1;
}

static int hifs_meta_builder_reserve(struct hifs_meta_map_builder *bld, size_t needed)
{
    size_t new_cap;
    struct hifs_meta_map_delta *new_deltas;

    if (bld->delta_cap >= needed) {
        return 0;
    }

    new_cap = bld->delta_cap ? bld->delta_cap * 2u : 16u;
    if (new_cap < needed) {
        new_cap = needed;
    }

    new_deltas = realloc(bld->deltas, new_cap * sizeof(*new_deltas));
    if (!new_deltas) {
        return -1;
    }

    bld->deltas = new_deltas;
    bld->delta_cap = new_cap;
    return 0;
}

static int hifs_meta_delta_cmp(const void *a, const void *b)
{
    const struct hifs_meta_map_delta *da = a;
    const struct hifs_meta_map_delta *db = b;

    if (da->block_no < db->block_no) {
        return -1;
    }
    if (da->block_no > db->block_no) {
        return 1;
    }
    if (da->generation > db->generation) {
        return -1;
    }
    if (da->generation < db->generation) {
        return 1;
    }
    return 0;
}

static void hifs_meta_builder_sort_and_dedup(struct hifs_meta_map_builder *bld)
{
    size_t write_idx;

    if (!bld || bld->delta_count == 0) {
        return;
    }

    qsort(bld->deltas, bld->delta_count, sizeof(*bld->deltas), hifs_meta_delta_cmp);

    write_idx = 0;
    for (size_t i = 1; i < bld->delta_count; i++) {
        if (bld->deltas[write_idx].block_no == bld->deltas[i].block_no) {
            if (bld->deltas[i].generation >= bld->deltas[write_idx].generation) {
                bld->deltas[write_idx] = bld->deltas[i];
            }
        } else {
            write_idx++;
            if (write_idx != i) {
                bld->deltas[write_idx] = bld->deltas[i];
            }
        }
    }

    bld->delta_count = write_idx + 1;
    bld->sorted = true;
}

static void hifs_meta_builder_compute_id(const struct hifs_meta_map_builder *bld,
                                         hifs_object_id_t *out)
{
    SHA256_CTX ctx;

    hifs_meta_sha256_begin_tagged(&ctx, "MAPROOT");
    hifs_meta_sha256_update_u64(&ctx, bld->volume_id);
    hifs_meta_sha256_update_u64(&ctx, bld->inode_key);
    hifs_meta_sha256_update_u32(&ctx, bld->map_level);
    hifs_meta_sha256_update_u64(&ctx, bld->map_epoch);
    hifs_meta_sha256_update_u64(&ctx, bld->delta_count);

    for (size_t i = 0; i < bld->delta_count; i++) {
        const struct hifs_meta_map_delta *delta = &bld->deltas[i];

        hifs_meta_sha256_update_u64(&ctx, delta->block_no);
        hifs_meta_sha256_update_u32(&ctx, delta->generation);
        SHA256_Update(&ctx, delta->content_hash, sizeof(delta->content_hash));
        SHA256_Update(&ctx, delta->stripe_id, sizeof(delta->stripe_id));
    }

    hifs_meta_sha256_finish(&ctx, out);
}

int hifs_meta_ctx_init(struct hifs_meta_ctx *ctx)
{
    if (!ctx) {
        errno = EINVAL;
        return -1;
    }

    memset(ctx, 0, sizeof(*ctx));
    if (pthread_mutex_init(&ctx->mu, NULL) != 0) {
        return -1;
    }

    ctx->initialized = true;
    return 0;
}

void hifs_meta_ctx_destroy(struct hifs_meta_ctx *ctx)
{
    if (!ctx) {
        return;
    }

    if (ctx->initialized) {
        free(ctx->maps);
        pthread_mutex_destroy(&ctx->mu);
    }

    memset(ctx, 0, sizeof(*ctx));
}

int hifs_meta_map_begin(struct hifs_meta_ctx *ctx,
                        uint64_t volume_id,
                        uint64_t inode_key,
                        struct hifs_meta_map_builder *out)
{
    if (!ctx || !out || !ctx->initialized) {
        errno = EINVAL;
        return -1;
    }

    memset(out, 0, sizeof(*out));
    out->volume_id = volume_id;
    out->inode_key = inode_key;
    out->map_level = HIFS_META_MAP_LEVEL_ROOT;
    return 0;
}

int hifs_meta_map_apply_delta(struct hifs_meta_map_builder *bld,
                              uint64_t block_no,
                              const uint8_t *content_hash,
                              const uint8_t *stripe_id,
                              uint32_t generation)
{
    if (!bld || !content_hash || !stripe_id) {
        errno = EINVAL;
        return -1;
    }

    if (hifs_meta_builder_reserve(bld, bld->delta_count + 1u) < 0) {
        return -1;
    }

    bld->deltas[bld->delta_count].block_no = block_no;
    bld->deltas[bld->delta_count].generation = generation;
    memcpy(bld->deltas[bld->delta_count].content_hash, content_hash, HIFS_BLOCK_HASH_SIZE);
    memcpy(bld->deltas[bld->delta_count].stripe_id, stripe_id, HIFS_STRIPE_ID_SIZE);
    bld->delta_count++;
    bld->sorted = false;
    if (generation > bld->map_epoch) {
        bld->map_epoch = generation;
    }

    return 0;
}
int hifs_meta_map_commit(struct hifs_meta_ctx *ctx,
                         struct hifs_meta_map_builder *bld,
                         hifs_object_id_t *new_map_root_id)
{
    hifs_object_id_t root_id;
    ssize_t idx;
    uint64_t now_ns;

    if (!ctx || !bld || !ctx->initialized) {
        errno = EINVAL;
        return -1;
    }

    if (bld->delta_count == 0) {
        errno = EINVAL;
        return -1;
    }

    hifs_meta_builder_sort_and_dedup(bld);
    hifs_meta_builder_compute_id(bld, &root_id);
    now_ns = hifs_meta_now_ns();

    pthread_mutex_lock(&ctx->mu);
    idx = hifs_meta_ctx_find_map(ctx, bld->volume_id, bld->inode_key);
    if (idx < 0) {
        if (hifs_meta_ctx_reserve(ctx, ctx->map_count + 1u) < 0) {
            pthread_mutex_unlock(&ctx->mu);
            return -1;
        }
        idx = (ssize_t)ctx->map_count++;
        ctx->maps[idx].volume_id = bld->volume_id;
        ctx->maps[idx].inode_key = bld->inode_key;
        memset(&ctx->maps[idx].map_root_id, 0, sizeof(ctx->maps[idx].map_root_id));
        ctx->maps[idx].commit_count = 0;
    }
    pthread_mutex_unlock(&ctx->mu);

    if (hifs_meta_log_deltas_to_mcl(bld, now_ns) < 0) {
        return -1;
    }

    pthread_mutex_lock(&ctx->mu);
    ctx->maps[idx].map_root_id = root_id;
    ctx->maps[idx].last_commit_ns = now_ns;
    ctx->maps[idx].commit_count++;
    pthread_mutex_unlock(&ctx->mu);

    if (new_map_root_id) {
        *new_map_root_id = root_id;
    }

    return 0;
}

static void hifs_meta_sha256_begin_tagged(SHA256_CTX *ctx, const char *tag)
{
    SHA256_Init(ctx);
    SHA256_Update(ctx, tag, strlen(tag));
}

static void hifs_meta_sha256_finish(SHA256_CTX *ctx, hifs_object_id_t *out)
{
    unsigned char digest[SHA256_DIGEST_LENGTH];

    SHA256_Final(digest, ctx);
    memcpy(out->bytes, digest, HIFS_META_OBJECT_ID_SIZE);
}

static void hifs_meta_sha256_finish_bytes(SHA256_CTX *ctx, uint8_t *out_bytes)
{
    unsigned char digest[SHA256_DIGEST_LENGTH];

    SHA256_Final(digest, ctx);
    memcpy(out_bytes, digest, SHA256_DIGEST_LENGTH);
}

static void hifs_meta_sha256_update_id(SHA256_CTX *ctx, const hifs_object_id_t *id)
{
    if (id) {
        SHA256_Update(ctx, id->bytes, sizeof(id->bytes));
    } else {
        static const uint8_t zero[HIFS_META_OBJECT_ID_SIZE] = {0};
        SHA256_Update(ctx, zero, sizeof(zero));
    }
}

void hifs_meta_make_volume_root_id(uint64_t volume_id,
                                   uint64_t sb_epoch,
                                   const hifs_object_id_t *root_inode_id,
                                   const hifs_object_id_t *root_dir_node_id,
                                   const hifs_object_id_t *policy_hash,
                                   hifs_object_id_t *out)
{
    hifs_meta_make_volume_identity(volume_id,
                                   sb_epoch,
                                   root_inode_id,
                                   root_dir_node_id,
                                   policy_hash,
                                   out,
                                   NULL);
}

void hifs_meta_make_volume_id(uint64_t volume_id,
                              uint64_t sb_epoch,
                              const hifs_object_id_t *root_inode_id,
                              const hifs_object_id_t *root_dir_node_id,
                              const hifs_object_id_t *policy_hash,
                              hifs_object_id_t *out)
{
    hifs_meta_make_volume_identity(volume_id,
                                   sb_epoch,
                                   root_inode_id,
                                   root_dir_node_id,
                                   policy_hash,
                                   NULL,
                                   out);
}

void hifs_meta_make_dir_node_id(uint64_t volume_id,
                                uint64_t dir_inode_key,
                                uint64_t dir_epoch,
                                const hifs_object_id_t *entries_hash,
                                hifs_object_id_t *out)
{
    SHA256_CTX ctx;

    hifs_meta_sha256_begin_tagged(&ctx, "DIRNODE");
    hifs_meta_sha256_update_u64(&ctx, volume_id);
    hifs_meta_sha256_update_u64(&ctx, dir_inode_key);
    hifs_meta_sha256_update_u64(&ctx, dir_epoch);
    hifs_meta_sha256_update_id(&ctx, entries_hash);
    hifs_meta_sha256_finish(&ctx, out);
}

void hifs_meta_make_dentry_id(uint64_t volume_id,
                              uint64_t parent_inode_key,
                              const char *name,
                              size_t name_len,
                              uint64_t dentry_epoch,
                              uint64_t target_inode_key,
                              hifs_object_id_t *out)
{
    SHA256_CTX ctx;

    if (!name) {
        name = "";
        name_len = 0;
    }

    hifs_meta_sha256_begin_tagged(&ctx, "DENTRY");
    hifs_meta_sha256_update_u64(&ctx, volume_id);
    hifs_meta_sha256_update_u64(&ctx, parent_inode_key);
    SHA256_Update(&ctx, name, name_len);
    hifs_meta_sha256_update_u64(&ctx, dentry_epoch);
    hifs_meta_sha256_update_u64(&ctx, target_inode_key);
    hifs_meta_sha256_finish(&ctx, out);
}

void hifs_meta_make_inode_id(uint64_t volume_id,
                             uint64_t inode_key,
                             uint64_t inode_epoch,
                             const hifs_object_id_t *inode_meta_hash,
                             const hifs_object_id_t *file_map_root_id,
                             hifs_object_id_t *out)
{
    SHA256_CTX ctx;

    hifs_meta_sha256_begin_tagged(&ctx, "INODE");
    hifs_meta_sha256_update_u64(&ctx, volume_id);
    hifs_meta_sha256_update_u64(&ctx, inode_key);
    hifs_meta_sha256_update_u64(&ctx, inode_epoch);
    hifs_meta_sha256_update_id(&ctx, inode_meta_hash);
    hifs_meta_sha256_update_id(&ctx, file_map_root_id);
    hifs_meta_sha256_finish(&ctx, out);
}

void hifs_meta_make_map_node_id(uint64_t volume_id,
                                uint64_t inode_key,
                                uint32_t map_level,
                                uint64_t map_index,
                                uint64_t map_epoch,
                                const hifs_object_id_t *child_list_hash,
                                hifs_object_id_t *out)
{
    SHA256_CTX ctx;

    hifs_meta_sha256_begin_tagged(&ctx, "MAP");
    hifs_meta_sha256_update_u64(&ctx, volume_id);
    hifs_meta_sha256_update_u64(&ctx, inode_key);
    hifs_meta_sha256_update_u32(&ctx, map_level);
    hifs_meta_sha256_update_u64(&ctx, map_index);
    hifs_meta_sha256_update_u64(&ctx, map_epoch);
    hifs_meta_sha256_update_id(&ctx, child_list_hash);
    hifs_meta_sha256_finish(&ctx, out);
}

void hifs_meta_compute_content_hash(const void *block_bytes,
                                    size_t block_len,
                                    uint8_t out_hash[HIFS_BLOCK_HASH_SIZE])
{
    SHA256_CTX ctx;

    SHA256_Init(&ctx);
    if (block_bytes && block_len > 0) {
        SHA256_Update(&ctx, block_bytes, block_len);
    }
    hifs_meta_sha256_finish_bytes(&ctx, out_hash);
}

void hifs_meta_make_stripe_id(const char *layout_ver,
                              uint64_t volume_id,
                              uint64_t inode_key,
                              uint64_t block_no,
                              uint32_t generation,
                              const uint8_t content_hash[HIFS_BLOCK_HASH_SIZE],
                              uint64_t placement_epoch,
                              uint8_t out_stripe_id[HIFS_STRIPE_ID_SIZE])
{
    SHA256_CTX ctx;
    const uint8_t *hash_ptr = content_hash;
    uint8_t zero_hash[HIFS_BLOCK_HASH_SIZE];
    const char *tag = "STRIPE";

    if (!hash_ptr) {
        memset(zero_hash, 0, sizeof(zero_hash));
        hash_ptr = zero_hash;
    }

    SHA256_Init(&ctx);
    SHA256_Update(&ctx, tag, strlen(tag));
    if (layout_ver) {
        SHA256_Update(&ctx, layout_ver, strlen(layout_ver));
    }
    hifs_meta_sha256_update_u64(&ctx, volume_id);
    hifs_meta_sha256_update_u64(&ctx, inode_key);
    hifs_meta_sha256_update_u64(&ctx, block_no);
    hifs_meta_sha256_update_u32(&ctx, generation);
    SHA256_Update(&ctx, hash_ptr, HIFS_BLOCK_HASH_SIZE);
    hifs_meta_sha256_update_u64(&ctx, placement_epoch);
    hifs_meta_sha256_finish_bytes(&ctx, out_stripe_id);
}

void hifs_meta_make_shard_key(const uint8_t stripe_id[HIFS_STRIPE_ID_SIZE],
                              uint32_t shard_ordinal,
                              const char *codec_name,
                              size_t codec_name_len,
                              uint32_t codec_version,
                              hifs_object_id_t *out)
{
    SHA256_CTX ctx;

    hifs_meta_sha256_begin_tagged(&ctx, "SHARD");
    SHA256_Update(&ctx, stripe_id, HIFS_STRIPE_ID_SIZE);
    hifs_meta_sha256_update_u32(&ctx, shard_ordinal);
    if (codec_name && codec_name_len > 0) {
        SHA256_Update(&ctx, codec_name, codec_name_len);
    }
    hifs_meta_sha256_update_u32(&ctx, codec_version);
    hifs_meta_sha256_finish(&ctx, out);
}
