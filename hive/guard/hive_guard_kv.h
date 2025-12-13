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

#ifdef __cplusplus
}
#endif

#endif /* HIVE_GUARD_KV_H */
