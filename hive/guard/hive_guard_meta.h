#pragma once

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../../hifs_shared_defs.h"

#define HIFS_META_OBJECT_ID_SIZE 32u
#define HIFS_META_MAP_LEVEL_ROOT UINT32_MAX

typedef struct {
    uint8_t bytes[HIFS_META_OBJECT_ID_SIZE];
} hifs_object_id_t;

struct hifs_meta_committed_map;

struct hifs_meta_ctx {
    pthread_mutex_t mu;
    struct hifs_meta_committed_map *maps;
    size_t map_count;
    size_t map_cap;
    bool initialized;
};

struct hifs_meta_map_delta {
    uint64_t block_no;
    uint32_t generation;
    uint8_t content_hash[HIFS_BLOCK_HASH_SIZE];
    uint8_t stripe_id[HIFS_STRIPE_ID_SIZE];
};

struct hifs_meta_map_builder {
    uint64_t volume_id;
    uint64_t inode_key;
    uint64_t map_epoch;
    struct hifs_meta_map_delta *deltas;
    size_t delta_count;
    size_t delta_cap;
    uint32_t map_level;
    bool sorted;
};

int hifs_meta_ctx_init(struct hifs_meta_ctx *ctx);
void hifs_meta_ctx_destroy(struct hifs_meta_ctx *ctx);

int hifs_meta_map_begin(struct hifs_meta_ctx *ctx,
                        uint64_t volume_id,
                        uint64_t inode_key,
                        struct hifs_meta_map_builder *out);

int hifs_meta_map_apply_delta(struct hifs_meta_map_builder *bld,
                              uint64_t block_no,
                              const uint8_t *content_hash,
                              const uint8_t *stripe_id,
                              uint32_t generation);

int hifs_meta_map_commit(struct hifs_meta_ctx *ctx,
                         struct hifs_meta_map_builder *bld,
                         hifs_object_id_t *new_map_root_id);

void hifs_meta_make_volume_root_id(uint64_t volume_id,
                                   uint64_t sb_epoch,
                                   const hifs_object_id_t *root_inode_id,
                                   const hifs_object_id_t *root_dir_node_id,
                                   const hifs_object_id_t *policy_hash,
                                   hifs_object_id_t *out);

void hifs_meta_make_dir_node_id(uint64_t volume_id,
                                uint64_t dir_inode_key,
                                uint64_t dir_epoch,
                                const hifs_object_id_t *entries_hash,
                                hifs_object_id_t *out);

void hifs_meta_make_dentry_id(uint64_t volume_id,
                              uint64_t parent_inode_key,
                              const char *name,
                              size_t name_len,
                              uint64_t dentry_epoch,
                              uint64_t target_inode_key,
                              hifs_object_id_t *out);

void hifs_meta_make_inode_id(uint64_t volume_id,
                             uint64_t inode_key,
                             uint64_t inode_epoch,
                             const hifs_object_id_t *inode_meta_hash,
                             const hifs_object_id_t *file_map_root_id,
                             hifs_object_id_t *out);

void hifs_meta_make_map_node_id(uint64_t volume_id,
                                uint64_t inode_key,
                                uint32_t map_level,
                                uint64_t map_index,
                                uint64_t map_epoch,
                                const hifs_object_id_t *child_list_hash,
                                hifs_object_id_t *out);

void hifs_meta_compute_content_hash(const void *block_bytes,
                                    size_t block_len,
                                    uint8_t out_hash[HIFS_BLOCK_HASH_SIZE]);

void hifs_meta_make_stripe_id(const char *layout_ver,
                              uint64_t volume_id,
                              uint64_t inode_key,
                              uint64_t block_no,
                              uint32_t generation,
                              const uint8_t content_hash[HIFS_BLOCK_HASH_SIZE],
                              uint64_t placement_epoch,
                              uint8_t out_stripe_id[HIFS_STRIPE_ID_SIZE]);

void hifs_meta_make_shard_key(const uint8_t stripe_id[HIFS_STRIPE_ID_SIZE],
                              uint32_t shard_ordinal,
                              const char *codec_name,
                              size_t codec_name_len,
                              uint32_t codec_version,
                              hifs_object_id_t *out);
