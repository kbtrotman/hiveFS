/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

 
#ifndef HIVE_GUARD_KV_H
#define HIVE_GUARD_KV_H

#include <stdint.h>
#include <stddef.h>

#include "hive_guard_raft.h"
#include "hive_guard_meta.h"

#ifdef __cplusplus
extern "C" {
#endif



/* structs, in C types (Rocks is inherantly C++, but has C interface.) */

struct Estripes {
    uint64_t stripe_id;
    uint8_t storage_node_id;
    uint64_t version_epoch;
    uint64_t ec_stripe;
};
struct H2SEntry {
    uint64_t ref_count;
    uint64_t estripe_ids[HIFS_EC_STRIPES];
    uint8_t  has_backup_hash;
    uint8_t  reserved[7];
    uint8_t  block_bck_hash[32];
};

struct VbEntry {
    uint8_t  hash_algo;
    uint8_t  reserved[7];
    uint8_t  block_hash[32];
    uint8_t  block_bck_hash[32];
};

struct VifEntry {
    uint64_t block_no;
};

struct EstripeLoc {
    uint32_t shard_id;
    uint32_t storage_node_id;
    uint64_t block_offset;
};

#define HG_META_MAGIC                0x48474d54u
#define HG_META_VERSION              1u
#define HG_META_POLICY_BLOB_MAX      1024u
#define HG_META_MAP_NODE_FANOUT      16u
#define HG_META_MAX_STRIPE_SHARDS    HIFS_EC_STRIPES
#define HG_META_SHARD_ID_SIZE        32u

enum hg_meta_obj_type {
    HG_META_OBJ_VOLUME_ROOT = 1,
    HG_META_OBJ_SUPERBLOCK = 2,
    HG_META_OBJ_ROOT_EXPORT = 3,
    HG_META_OBJ_POLICY = 4,
    HG_META_OBJ_DIRECTORY_NODE = 5,
    HG_META_OBJ_DENTRY = 6,
    HG_META_OBJ_INODE = 7,
    HG_META_OBJ_MAP_ROOT = 8,
    HG_META_OBJ_MAP_NODE = 9,
    HG_META_OBJ_BLOCK_IDENTITY = 10,
    HG_META_OBJ_STRIPE = 11,
    HG_META_OBJ_STRIPE_MANIFEST = 12,
    HG_META_OBJ_SHARD = 13,
};

struct hg_meta_obj_hdr {
    uint32_t magic;
    uint16_t version;
    uint16_t type;
    uint32_t payload_len;
    uint64_t obj_epoch;
    uint8_t  self_id[HIFS_META_OBJECT_ID_SIZE];
    uint8_t  volume_id[HIFS_META_OBJECT_ID_SIZE];
};

struct hg_meta_superblock_obj {
    struct hg_meta_obj_hdr hdr;
    uint64_t volume_numeric_id;
    struct hifs_volume_superblock vsb;
};

struct hg_meta_volume_root_obj {
    struct hg_meta_obj_hdr hdr;
    uint64_t volume_numeric_id;
    hifs_object_id_t superblock_id;
    hifs_object_id_t root_export_id;
    hifs_object_id_t policy_obj_id;
    hifs_object_id_t root_dir_node_id;
    uint64_t commit_epoch;
};

struct hg_meta_root_export_obj {
    struct hg_meta_obj_hdr hdr;
    uint64_t volume_numeric_id;
    hifs_object_id_t root_dentry_id;
    hifs_object_id_t root_dir_node_id;
    hifs_object_id_t primary_inode_id;
    uint32_t export_flags;
};

struct hg_meta_policy_obj {
    struct hg_meta_obj_hdr hdr;
    uint32_t policy_len;
    uint8_t  policy_blob[HG_META_POLICY_BLOB_MAX];
};

struct hg_meta_directory_node_obj {
    struct hg_meta_obj_hdr hdr;
    uint64_t volume_numeric_id;
    uint64_t inode_key;
    uint64_t child_epoch;
    uint32_t child_count;
    hifs_object_id_t entries_hash;
};

struct hg_meta_dentry_obj {
    struct hg_meta_obj_hdr hdr;
    uint64_t volume_numeric_id;
    uint64_t parent_inode_key;
    uint64_t target_inode_key;
    uint64_t dentry_epoch;
    uint64_t name_hash;
    uint16_t name_len;
    uint16_t dentry_type;
    uint32_t flags;
    char     name[HIFS_MAX_NAME_SIZE];
};

struct hg_meta_inode_obj {
    struct hg_meta_obj_hdr hdr;
    uint64_t volume_numeric_id;
    uint64_t inode_key;
    hifs_object_id_t file_map_root_id;
    struct hifs_inode_wire inode;
};

struct hg_meta_map_root_obj {
    struct hg_meta_obj_hdr hdr;
    uint64_t volume_numeric_id;
    uint64_t inode_key;
    uint64_t map_epoch;
    uint64_t logical_bytes;
    uint32_t level_count;
    hifs_object_id_t root_node_id;
};

struct hg_meta_map_node_obj {
    struct hg_meta_obj_hdr hdr;
    uint64_t volume_numeric_id;
    uint64_t inode_key;
    uint64_t map_index;
    uint64_t map_epoch;
    uint32_t map_level;
    uint32_t child_count;
    hifs_object_id_t children[HG_META_MAP_NODE_FANOUT];
};

struct hg_meta_block_identity_obj {
    struct hg_meta_obj_hdr hdr;
    uint64_t volume_numeric_id;
    uint64_t block_no;
    uint8_t  content_hash[HIFS_BLOCK_HASH_SIZE];
    uint8_t  stripe_id[HIFS_STRIPE_ID_SIZE];
};

struct hg_meta_stripe_obj {
    struct hg_meta_obj_hdr hdr;
    uint8_t  stripe_id[HIFS_STRIPE_ID_SIZE];
    uint32_t shard_count;
    uint32_t placement_epoch;
    hifs_object_id_t manifest_id;
};

struct hg_meta_stripe_manifest_obj {
    struct hg_meta_obj_hdr hdr;
    uint8_t  stripe_id[HIFS_STRIPE_ID_SIZE];
    uint32_t shard_count;
    hifs_object_id_t shard_ids[HG_META_MAX_STRIPE_SHARDS];
};

struct hg_meta_shard_obj {
    struct hg_meta_obj_hdr hdr;
    uint8_t  shard_id[HG_META_SHARD_ID_SIZE];
    uint32_t storage_node_id;
    uint32_t codec_version;
    uint64_t block_offset;
    uint64_t length_bytes;
};

/*  operations  */

int hg_kv_init(const char *path);
void hg_kv_shutdown(void);

int hg_kv_put_h2s(uint8_t hash_algo,
                  const uint8_t hash[32],
                  const struct H2SEntry *e);

int hg_kv_get_h2s(uint8_t hash_algo,
                  const uint8_t hash[32],
                  struct H2SEntry *out);  /* returns 0 on success, <0 on error */

int hg_kv_apply_put_block(const struct RaftPutBlock *cmd);
int hg_kv_get_vb_entry(uint64_t volume_id,
		       uint64_t block_no,
		       struct VbEntry *out);
int hg_kv_get_estripe_loc(uint64_t estripe_id,
			  struct EstripeLoc *out);
int hg_kv_get_vif_entry(uint64_t volume_id,
			uint64_t inode_id,
			uint16_t fp_index,
			struct hifs_block_fingerprint_wire *out);
int hg_kv_put_estripe_chunk(uint64_t estripe_id,
			    const uint8_t *data,
			    size_t len);
int hg_kv_get_estripe_chunk(uint64_t estripe_id,
			    uint8_t **out_data,
			    size_t *out_len);

bool hifs_volume_inode_fp_replace(uint64_t volume_id,
                                  uint64_t inode_id,
                                  uint16_t fp_index,
                                  const struct hifs_block_fingerprint_wire *fp);
bool hifs_volume_inode_fp_sync(uint64_t volume_id,
                               uint64_t inode_id,
                               const struct hifs_block_fingerprint_wire *fps,
                               uint16_t fp_count);

int hg_kv_put_superblock(const struct hg_meta_superblock_obj *obj);
int hg_kv_get_superblock(const uint8_t superblock_id[HIFS_META_OBJECT_ID_SIZE],
                         struct hg_meta_superblock_obj *out);
int hg_kv_put_volume_super(uint64_t volume_id,
                           const struct hifs_volume_superblock *vsb);
int hg_kv_get_volume_super(uint64_t volume_id,
                           struct hifs_volume_superblock *out);
int hg_kv_put_volume_root(const struct hg_meta_volume_root_obj *vr);
int hg_kv_get_volume_root(const uint8_t volume_root_id[HIFS_META_OBJECT_ID_SIZE],
                          struct hg_meta_volume_root_obj *out);
int hg_kv_put_root_dentry(uint64_t volume_id,
                          const struct hifs_volume_root_dentry *root);
int hg_kv_get_root_dentry(uint64_t volume_id,
                          struct hifs_volume_root_dentry *out);
int hg_kv_put_volume_root_ptr(uint64_t volume_id,
                              const uint8_t volume_root_id[HIFS_META_OBJECT_ID_SIZE]);
int hg_kv_get_volume_root_ptr(uint64_t volume_id,
                              uint8_t volume_root_id[HIFS_META_OBJECT_ID_SIZE]);
int hg_kv_put_dentry(const struct hg_meta_dentry_obj *de);
int hg_kv_get_dentry(const uint8_t dentry_id[HIFS_META_OBJECT_ID_SIZE],
                     struct hg_meta_dentry_obj *out);
int hg_kv_put_dentry_ptr(uint64_t volume_id,
                         uint64_t parent_inode_key,
                         uint64_t name_hash,
                         const uint8_t dentry_id[HIFS_META_OBJECT_ID_SIZE]);
int hg_kv_delete_dentry_ptr(uint64_t volume_id,
                            uint64_t parent_inode_key,
                            uint64_t name_hash);
int hg_kv_put_inode_obj(const struct hg_meta_inode_obj *obj);
int hg_kv_get_inode_obj(const uint8_t inode_obj_id[HIFS_META_OBJECT_ID_SIZE],
                        struct hg_meta_inode_obj *out);
int hg_kv_put_inode_ptr(uint64_t volume_id,
                        uint64_t inode_key,
                        const uint8_t inode_obj_id[HIFS_META_OBJECT_ID_SIZE]);
int hg_kv_get_inode_ptr(uint64_t volume_id,
                        uint64_t inode_key,
                        uint8_t inode_obj_id[HIFS_META_OBJECT_ID_SIZE]);
int hg_kv_put_volume_dentry(uint64_t volume_id,
                            uint64_t inode_key,
                            uint64_t parent_inode_key,
                            uint64_t name_hash,
                            const struct hifs_volume_dentry *dent);
int hg_kv_get_volume_dentry_by_inode(uint64_t volume_id,
                                     uint64_t inode_key,
                                     struct hifs_volume_dentry *out);
int hg_kv_get_volume_dentry_by_name(uint64_t volume_id,
                                    uint64_t parent_inode_key,
                                    uint64_t name_hash,
                                    struct hifs_volume_dentry *out);
int hg_kv_put_volume_inode(uint64_t volume_id,
                           const struct hifs_inode_wire *inode);
int hg_kv_get_volume_inode(uint64_t volume_id,
                           uint64_t inode_key,
                           struct hifs_inode_wire *out);
int hg_kv_put_block_metadata_batch(const struct hifs_meta_block_record *records,
                                   size_t record_count);

int hg_kv_get_raw(const char *key, size_t key_len,
                  void *out_buf, size_t buf_len);

#ifdef __cplusplus
}
#endif

#endif /* HIVE_GUARD_KV_H */
