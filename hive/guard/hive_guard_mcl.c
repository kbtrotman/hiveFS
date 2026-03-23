/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>

#include "hive_guard_mcl.h"

#define HIFS_MCL_MEM_MAX_RECORDS 1000u

#define HIFS_MCL_MEM_TABLE_SIZE 2048u

enum hive_mcl_index_kind {
    HIVE_MCL_KEY_MAP_DELTA,
    HIVE_MCL_KEY_INODE,
    HIVE_MCL_KEY_DIR,
};

enum hive_mcl_slot_state {
    HIVE_MCL_SLOT_EMPTY = 0,
    HIVE_MCL_SLOT_OCCUPIED,
    HIVE_MCL_SLOT_TOMBSTONE,
};

enum hive_mcl_delta_flags {
    HIVE_MCL_INDEX_FLAG_DIRTY     = 1u << 0,
    HIVE_MCL_INDEX_FLAG_APPLIED   = 1u << 1,
    HIVE_MCL_INDEX_FLAG_OBSOLETE  = 1u << 2,
};

struct hive_mcl_index_key {
    enum hive_mcl_index_kind kind;
    union {
        struct {
            uint64_t inode_key;
            uint64_t block_no;
        } map;
        struct {
            uint64_t inode_key;
        } inode;
        struct {
            uint64_t dir_inode_key;
            uint16_t name_len;
            char name[HIFS_MAX_NAME_SIZE];
        } dir;
    } u;
};

union hive_mcl_mem_payload {
    struct hifs_mcl_map_delta_rec map_delta;
};

struct hive_mcl_mem_table_entry {
    struct hive_mcl_index_key key;
    union hive_mcl_mem_payload payload;
    enum hifs_meta_change_item item;
    enum hifs_mcl_change_type change_type;
    enum hifs_mcl_delta_kind delta_kind;
    uint64_t txn_id;
    uint32_t generation;
    uint64_t last_update_ns;
    uint32_t flags;
    enum hive_mcl_slot_state slot_state;
    uint64_t hash;
};

static struct hive_mcl_mem_table_entry g_mcl_mem_table[HIFS_MCL_MEM_TABLE_SIZE];
static size_t g_mcl_mem_count;
static pthread_mutex_t g_mcl_mem_mu = PTHREAD_MUTEX_INITIALIZER;

struct hifs_mcl_pending_delta {
    uint64_t volume_id;
    uint64_t inode_key;
    uint64_t block_no;
    uint32_t generation;
    uint64_t txn_id;
    uint8_t content_hash[HIFS_BLOCK_HASH_SIZE];
    uint8_t stripe_id[HIFS_STRIPE_ID_SIZE];
};

static bool hive_mcl_entry_is_pending_map_delta(const struct hive_mcl_mem_table_entry *entry)
{
    if (!entry || entry->slot_state != HIVE_MCL_SLOT_OCCUPIED) {
        return false;
    }

    if (entry->item != CHANGE_MAP_DELTA) {
        return false;
    }

    if (!(entry->flags & HIVE_MCL_INDEX_FLAG_DIRTY)) {
        return false;
    }

    if (entry->flags & HIVE_MCL_INDEX_FLAG_OBSOLETE) {
        return false;
    }

    return true;
}

static size_t hive_mcl_collect_pending_deltas(struct hifs_mcl_pending_delta **out)
{
    struct hifs_mcl_pending_delta *deltas = NULL;
    size_t count = 0;
    size_t idx = 0;

    if (out) {
        *out = NULL;
    }

    pthread_mutex_lock(&g_mcl_mem_mu);
    for (size_t i = 0; i < HIFS_MCL_MEM_TABLE_SIZE; i++) {
        if (hive_mcl_entry_is_pending_map_delta(&g_mcl_mem_table[i])) {
            count++;
        }
    }

    if (count == 0) {
        pthread_mutex_unlock(&g_mcl_mem_mu);
        return 0;
    }

    deltas = malloc(count * sizeof(*deltas));
    if (!deltas) {
        pthread_mutex_unlock(&g_mcl_mem_mu);
        return 0;
    }

    for (size_t i = 0; i < HIFS_MCL_MEM_TABLE_SIZE; i++) {
        struct hive_mcl_mem_table_entry *entry = &g_mcl_mem_table[i];

        if (!hive_mcl_entry_is_pending_map_delta(entry)) {
            continue;
        }

        deltas[idx].volume_id = entry->payload.map_delta.volume_id;
        deltas[idx].inode_key = entry->key.u.map.inode_key;
        deltas[idx].block_no = entry->key.u.map.block_no;
        deltas[idx].generation = entry->payload.map_delta.generation;
        deltas[idx].txn_id = entry->payload.map_delta.txn_id;
        memcpy(deltas[idx].content_hash,
               entry->payload.map_delta.content_hash,
               sizeof(deltas[idx].content_hash));
        memcpy(deltas[idx].stripe_id,
               entry->payload.map_delta.stripe_id,
               sizeof(deltas[idx].stripe_id));
        idx++;
    }
    pthread_mutex_unlock(&g_mcl_mem_mu);

    if (out) {
        *out = deltas;
    } else {
        free(deltas);
    }

    return idx;
}

static int hive_mcl_pending_delta_cmp(const void *a, const void *b)
{
    const struct hifs_mcl_pending_delta *da = a;
    const struct hifs_mcl_pending_delta *db = b;

    if (da->volume_id < db->volume_id) {
        return -1;
    }
    if (da->volume_id > db->volume_id) {
        return 1;
    }
    if (da->inode_key < db->inode_key) {
        return -1;
    }
    if (da->inode_key > db->inode_key) {
        return 1;
    }
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
    if (da->txn_id < db->txn_id) {
        return -1;
    }
    if (da->txn_id > db->txn_id) {
        return 1;
    }
    return 0;
}

static void hive_mcl_mem_index_mark_map_delta_applied(const struct hifs_mcl_pending_delta *delta)
{
    struct hive_mcl_index_key key;
    struct hive_mcl_mem_table_entry *entry;

    if (!delta) {
        return;
    }

    key = hive_mcl_make_map_key(delta->inode_key, delta->block_no);

    pthread_mutex_lock(&g_mcl_mem_mu);
    entry = hive_mcl_mem_index_lookup_slot_locked(&key, false, NULL);
    if (entry &&
        entry->payload.map_delta.generation == delta->generation &&
        entry->payload.map_delta.txn_id == delta->txn_id) {
        entry->flags &= ~HIVE_MCL_INDEX_FLAG_DIRTY;
        entry->flags |= HIVE_MCL_INDEX_FLAG_APPLIED;
        entry->last_update_ns = hive_mcl_now_ns();
    }
    pthread_mutex_unlock(&g_mcl_mem_mu);
}

void hifs_mcl_coalescer_run(struct hifs_mcl_ctx *mcl,
                            struct hifs_meta_ctx *meta)
{
    struct hifs_mcl_pending_delta *deltas = NULL;
    size_t delta_count;

    if (!mcl || !meta) {
        return;
    }

    delta_count = hive_mcl_collect_pending_deltas(&deltas);
    if (delta_count == 0 || !deltas) {
        free(deltas);
        return;
    }

    qsort(deltas, delta_count, sizeof(*deltas), hive_mcl_pending_delta_cmp);

    for (size_t i = 0; i < delta_count;) {
        const uint64_t volume_id = deltas[i].volume_id;
        const uint64_t inode_key = deltas[i].inode_key;
        size_t group_start = i;
        struct hifs_meta_map_builder builder = {0};
        bool builder_ok = hifs_meta_map_begin(meta, volume_id, inode_key, &builder) == 0;

        while (i < delta_count &&
               deltas[i].volume_id == volume_id &&
               deltas[i].inode_key == inode_key) {
            if (builder_ok) {
                if (hifs_meta_map_apply_delta(&builder,
                                              deltas[i].block_no,
                                              deltas[i].content_hash,
                                              deltas[i].stripe_id,
                                              deltas[i].generation) != 0) {
                    builder_ok = false;
                }
            }
            i++;
        }

        size_t group_end = i;

        if (builder_ok) {
            if (hifs_meta_map_commit(meta, &builder, NULL) == 0) {
                for (size_t j = group_start; j < group_end; j++) {
                    hive_mcl_mem_index_mark_map_delta_applied(&deltas[j]);
                }
            } else {
                builder_ok = false;
            }
        }

        free(builder.deltas);
        builder.deltas = NULL;
        builder.delta_count = 0;
        builder.delta_cap = 0;
    }

    free(deltas);
    (void)mcl;
}

int hifs_mcl_replay_record(const struct hifs_mcl_hdr *hdr,
                           const void *payload,
                           void *arg)
{
    const struct hifs_mcl_record_prefix *prefix;
    const uint8_t *rec_bytes;
    size_t rec_len;

    (void)arg;

    if (!hdr || !payload) {
        errno = EINVAL;
        return -1;
    }

    if (hdr->length < sizeof(*prefix)) {
        errno = EINVAL;
        return -1;
    }

    prefix = payload;
    rec_bytes = (const uint8_t *)payload + sizeof(*prefix);
    rec_len = (size_t)hdr->length - sizeof(*prefix);

    switch ((enum hifs_meta_change_item)prefix->item) {
    case CHANGE_MAP_DELTA: {
        const struct hifs_mcl_map_delta_rec *rec;
        enum hifs_mcl_change_type change_type;

        if (rec_len != sizeof(struct hifs_mcl_map_delta_rec)) {
            errno = EINVAL;
            return -1;
        }

        rec = (const void *)rec_bytes;
        change_type = (enum hifs_mcl_change_type)prefix->change_type;

        hive_mcl_mem_index_track_map_delta(rec,
                                           change_type,
                                           HIFS_MCL_DELTA_MAP_APPEND);
        break;
    }
    default:
        break;
    }

    return 0;
}

static inline uint64_t hive_mcl_mix64(uint64_t x)
{
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccduLL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53uLL;
    x ^= x >> 33;
    return x;
}

static inline uint64_t hive_mcl_hash_bytes(const void *data, size_t len, uint64_t seed)
{
    const uint8_t *p = data;

    for (size_t i = 0; i < len; i++) {
        seed ^= (uint64_t)p[i] + 0x9e3779b185ebca87ull + (seed << 6) + (seed >> 2);
    }

    return hive_mcl_mix64(seed);
}

static inline struct hive_mcl_index_key hive_mcl_make_map_key(uint64_t inode_key, uint64_t block_no)
{
    struct hive_mcl_index_key key = {0};

    key.kind = HIVE_MCL_KEY_MAP_DELTA;
    key.u.map.inode_key = inode_key;
    key.u.map.block_no = block_no;

    return key;
}

static inline struct hive_mcl_index_key hive_mcl_make_inode_key(uint64_t inode_key)
{
    struct hive_mcl_index_key key = {0};

    key.kind = HIVE_MCL_KEY_INODE;
    key.u.inode.inode_key = inode_key;

    return key;
}

static inline struct hive_mcl_index_key hive_mcl_make_dir_key(uint64_t dir_inode_key,
                                                              const char *name,
                                                              size_t name_len)
{
    struct hive_mcl_index_key key = {0};

    key.kind = HIVE_MCL_KEY_DIR;
    key.u.dir.dir_inode_key = dir_inode_key;
    if (name_len > HIFS_MAX_NAME_SIZE) {
        name_len = HIFS_MAX_NAME_SIZE;
    }
    key.u.dir.name_len = (uint16_t)name_len;
    memcpy(key.u.dir.name, name, name_len);
    if (name_len < HIFS_MAX_NAME_SIZE) {
        key.u.dir.name[name_len] = '\0';
    }

    return key;
}

static inline bool hive_mcl_keys_equal(const struct hive_mcl_index_key *a,
                                       const struct hive_mcl_index_key *b)
{
    if (a->kind != b->kind) {
        return false;
    }

    switch (a->kind) {
    case HIVE_MCL_KEY_MAP_DELTA:
        return a->u.map.inode_key == b->u.map.inode_key &&
               a->u.map.block_no == b->u.map.block_no;
    case HIVE_MCL_KEY_INODE:
        return a->u.inode.inode_key == b->u.inode.inode_key;
    case HIVE_MCL_KEY_DIR:
        if (a->u.dir.dir_inode_key != b->u.dir.dir_inode_key ||
            a->u.dir.name_len != b->u.dir.name_len) {
            return false;
        }
        return memcmp(a->u.dir.name, b->u.dir.name, a->u.dir.name_len) == 0;
    }

    return false;
}

static inline uint64_t hive_mcl_key_hash(const struct hive_mcl_index_key *key)
{
    switch (key->kind) {
    case HIVE_MCL_KEY_MAP_DELTA: {
        uint64_t seed = key->u.map.inode_key * 0x9e3779b185ebca87ull;
        seed ^= key->u.map.block_no + (seed << 7);
        return hive_mcl_mix64(seed);
    }
    case HIVE_MCL_KEY_INODE:
        return hive_mcl_mix64(key->u.inode.inode_key | 1ull);
    case HIVE_MCL_KEY_DIR: {
        uint64_t seed = hive_mcl_hash_bytes(key->u.dir.name, key->u.dir.name_len, key->u.dir.dir_inode_key);
        return hive_mcl_mix64(seed ^ 0x74626c6469726575ull);
    }
    }

    return 0;
}

static inline uint64_t hive_mcl_now_ns(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static struct hive_mcl_mem_table_entry *
hive_mcl_mem_index_lookup_slot_locked(const struct hive_mcl_index_key *key,
                                      bool create,
                                      bool *is_new)
{
    uint64_t hash = hive_mcl_key_hash(key);
    size_t idx = (size_t)(hash & (HIFS_MCL_MEM_TABLE_SIZE - 1u));
    struct hive_mcl_mem_table_entry *first_tombstone = NULL;

    if (is_new) {
        *is_new = false;
    }

    for (size_t probe = 0; probe < HIFS_MCL_MEM_TABLE_SIZE; probe++) {
        struct hive_mcl_mem_table_entry *entry = &g_mcl_mem_table[idx];

        if (entry->slot_state == HIVE_MCL_SLOT_EMPTY) {
            if (!create) {
                return NULL;
            }

            entry = first_tombstone ? first_tombstone : entry;
            entry->slot_state = HIVE_MCL_SLOT_OCCUPIED;
            entry->key = *key;
            entry->hash = hash;
            if (is_new) {
                *is_new = true;
            }
            return entry;
        }

        if (entry->slot_state == HIVE_MCL_SLOT_TOMBSTONE) {
            if (!first_tombstone) {
                first_tombstone = entry;
            }
        } else if (entry->hash == hash && hive_mcl_keys_equal(&entry->key, key)) {
            return entry;
        }

        idx = (idx + 1u) & (HIFS_MCL_MEM_TABLE_SIZE - 1u);
    }

    if (create && first_tombstone) {
        first_tombstone->slot_state = HIVE_MCL_SLOT_OCCUPIED;
        first_tombstone->key = *key;
        first_tombstone->hash = hash;
        if (is_new) {
            *is_new = true;
        }
        return first_tombstone;
    }

    return NULL;
}

static void hive_mcl_mem_index_remove_entry_unlocked(struct hive_mcl_mem_table_entry *entry)
{
    memset(entry, 0, sizeof(*entry));
    entry->slot_state = HIVE_MCL_SLOT_TOMBSTONE;
}

static void hive_mcl_mem_index_evict_unlocked(void)
{
    size_t victim = HIFS_MCL_MEM_TABLE_SIZE;
    uint64_t oldest_ns = UINT64_MAX;

    for (size_t i = 0; i < HIFS_MCL_MEM_TABLE_SIZE; i++) {
        struct hive_mcl_mem_table_entry *entry = &g_mcl_mem_table[i];

        if (entry->slot_state != HIVE_MCL_SLOT_OCCUPIED) {
            continue;
        }

        if (entry->flags & HIVE_MCL_INDEX_FLAG_DIRTY) {
            continue;
        }

        if (entry->last_update_ns < oldest_ns) {
            oldest_ns = entry->last_update_ns;
            victim = i;
        }
    }

    if (victim == HIFS_MCL_MEM_TABLE_SIZE) {
        oldest_ns = UINT64_MAX;
        for (size_t i = 0; i < HIFS_MCL_MEM_TABLE_SIZE; i++) {
            struct hive_mcl_mem_table_entry *entry = &g_mcl_mem_table[i];

            if (entry->slot_state != HIVE_MCL_SLOT_OCCUPIED) {
                continue;
            }

            if (entry->last_update_ns < oldest_ns) {
                oldest_ns = entry->last_update_ns;
                victim = i;
            }
        }
    }

    if (victim < HIFS_MCL_MEM_TABLE_SIZE) {
        hive_mcl_mem_index_remove_entry_unlocked(&g_mcl_mem_table[victim]);
        if (g_mcl_mem_count > 0) {
            g_mcl_mem_count--;
        }
    }
}

static void hive_mcl_mem_index_update_flags(const struct hive_mcl_index_key *key,
                                            uint32_t set_mask,
                                            uint32_t clear_mask)
{
    pthread_mutex_lock(&g_mcl_mem_mu);
    struct hive_mcl_mem_table_entry *entry = hive_mcl_mem_index_lookup_slot_locked(key, false, NULL);

    if (entry) {
        entry->flags &= ~clear_mask;
        entry->flags |= set_mask;
        entry->last_update_ns = hive_mcl_now_ns();
    }

    pthread_mutex_unlock(&g_mcl_mem_mu);
}

static void hive_mcl_mem_index_track_map_delta(const struct hifs_mcl_map_delta_rec *rec,
                                               enum hifs_mcl_change_type change_type,
                                               enum hifs_mcl_delta_kind delta_kind)
{
    struct hive_mcl_index_key key = hive_mcl_make_map_key(rec->inode_key, rec->block_no);
    bool is_new = false;

    pthread_mutex_lock(&g_mcl_mem_mu);
    if (g_mcl_mem_count >= HIFS_MCL_MEM_MAX_RECORDS) {
        hive_mcl_mem_index_evict_unlocked();
    }

    struct hive_mcl_mem_table_entry *entry = hive_mcl_mem_index_lookup_slot_locked(&key, true, &is_new);
    if (!entry) {
        pthread_mutex_unlock(&g_mcl_mem_mu);
        return;
    }

    entry->item = CHANGE_MAP_DELTA;
    entry->change_type = change_type;
    entry->delta_kind = delta_kind;
    entry->txn_id = rec->txn_id;
    entry->generation = rec->generation;
    entry->payload.map_delta = *rec;
    entry->flags = (entry->flags & HIVE_MCL_INDEX_FLAG_OBSOLETE) | HIVE_MCL_INDEX_FLAG_DIRTY;
    entry->flags &= ~HIVE_MCL_INDEX_FLAG_APPLIED;
    entry->last_update_ns = hive_mcl_now_ns();

    if (is_new) {
        g_mcl_mem_count++;
    }

    pthread_mutex_unlock(&g_mcl_mem_mu);
}

static inline uint64_t hive_mcl_now_ns(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void hive_mcl_mem_cache_put(const struct hive_seg_mcl_block_entry *blk,
                                   uint64_t txn_id,
                                   uint32_t generation,
                                   uint64_t inode_key)
{
    struct hive_mcl_mem_table_entry entry;
    size_t slot;

    entry.block = *blk;
    entry.txn_id = txn_id;
    entry.inode_key = inode_key;
    entry.generation = generation;
    entry.last_update_ns = hive_mcl_now_ns();

    pthread_mutex_lock(&g_mcl_mem_mu);
    if (g_mcl_mem_count < HIFS_MCL_MEM_MAX_RECORDS) {
        slot = (g_mcl_mem_head + g_mcl_mem_count) % HIFS_MCL_MEM_MAX_RECORDS;
        g_mcl_mem_count++;
    } else {
        slot = g_mcl_mem_head;
        g_mcl_mem_head = (g_mcl_mem_head + 1) % HIFS_MCL_MEM_MAX_RECORDS;
    }
    g_mcl_mem_table[slot] = entry;
    pthread_mutex_unlock(&g_mcl_mem_mu);
}

int hive_seg_emit_mcl_map_delta(const struct hive_seg_mcl_block_entry *blk,
                                uint64_t txn_id,
                                uint32_t generation,
                                uint64_t inode_key)
{
    struct hifs_mcl_map_delta_rec rec = {0};

    if (!blk) {
        errno = EINVAL;
        return -1;
    }

    rec.txn_id          = txn_id;
    rec.volume_id       = blk->volume_id;
    rec.inode_key       = inode_key;
    rec.block_no        = blk->block_no;
    rec.generation      = generation;
    rec.block_bytes     = blk->block_bytes;
    rec.placement_epoch = blk->placement_epoch;
    rec.hash_algo       = (uint8_t)blk->hash_algo;
    rec.stripe_algo     = (uint8_t)blk->stripe_algo;

    memcpy(rec.content_hash, blk->hash, HIFS_BLOCK_HASH_SIZE);
    memcpy(rec.stripe_id, blk->stripe_id, HIFS_STRIPE_ID_SIZE);

    hive_mcl_mem_cache_put(blk, txn_id, generation, inode_key);

    return hifs_mcl_append(&g_hive_guard_mcl_ctx,
                           CHANGE_MAP_DELTA,
                           HIFS_MCL_NEW,
                           &rec,
                           sizeof(rec),
                           txn_id,
                           inode_key);
}


struct hifs_mcl_ctx g_hive_guard_mcl_ctx = {
    .fd = -1,
};

static pthread_mutex_t g_mcl_rotate_mu = PTHREAD_MUTEX_INITIALIZER;
static uint32_t g_mcl_next_slot;

static inline uint64_t hifs_mcl_next_seqno(struct hifs_mcl_ctx *ctx)
{
    return ctx->next_seqno++;
}

static inline uint32_t hifs_mcl_crc32_seed(uint32_t crc, const void *data, size_t len)
{
    const uint8_t *p = data;

    for (size_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (size_t j = 0; j < 8; j++) {
            uint32_t mask = -(crc & 1u);
            crc = (crc >> 1) ^ (0xedb88320u & mask);
        }
    }

    return crc;
}

static inline size_t hifs_mcl_payload_len(enum hifs_meta_change_item item)
{
    switch (item) {
    case CHANGE_INODE:
        return sizeof(struct hifs_inode);
    case CHANGE_DIRENTRY:
        return sizeof(struct hifs_dir_entry);
    case CHANGE_STRIPE_INFO:
        return sizeof(struct HifsEstripeLocations);
    case CHANGE_ROOT_DIRENTRY:
        return sizeof(struct hifs_volume_root_dentry);
    case CHANGE_VOLUME_SUPERBLOCK:
        return sizeof(struct hifs_volume_superblock);
    case CHANGE_MAP_DELTA:
        return sizeof(struct hifs_mcl_map_delta_rec);
    default:
        return 0;
    }
}

static int hifs_mcl_current_slot(const char *path)
{
    const char *base;
    const char *digits;
    char *endptr;
    long slot;

    if (!path) {
        return -1;
    }

    base = strrchr(path, '/');
    base = base ? base + 1 : path;

    digits = base;
    while (*digits && !isdigit((unsigned char)*digits)) {
        digits++;
    }

    if (!*digits) {
        return -1;
    }

    slot = strtol(digits, &endptr, 10);
    if (digits == endptr || slot < 0) {
        return -1;
    }

    return (int)(slot % HIFS_MCL_MAX_LOGS);
}

static void hifs_mcl_slot_path_for(const char *path,
                                   unsigned int slot,
                                   char *out,
                                   size_t out_sz)
{
    const char *slash;
    size_t dir_len;

    if (!out || out_sz == 0) {
        return;
    }

    slash = strrchr(path ? path : "", '/');
    dir_len = slash ? (size_t)(slash - (path ? path : "") + 1) : 0;
    if (dir_len >= out_sz) {
        dir_len = out_sz - 1;
    }

    if (dir_len && path) {
        memcpy(out, path, dir_len);
    }
    out[dir_len] = '\0';

    snprintf(out + dir_len, out_sz - dir_len, "mcl-%u.log", slot);
}

static int hifs_mcl_rotate_locked(struct hifs_mcl_ctx *ctx)
{
    char next_path[sizeof(ctx->path)];
    int current_slot;
    unsigned int next_slot;
    int new_fd;

    if (!ctx || ctx->fd < 0) {
        errno = EINVAL;
        return -1;
    }

    current_slot = hifs_mcl_current_slot(ctx->path);
    if (current_slot >= 0) {
        next_slot = (unsigned int)((current_slot + 1) % HIFS_MCL_MAX_LOGS);
    } else {
        next_slot = g_mcl_next_slot % HIFS_MCL_MAX_LOGS;
    }
    g_mcl_next_slot = (next_slot + 1) % HIFS_MCL_MAX_LOGS;

    hifs_mcl_slot_path_for(ctx->path, next_slot, next_path, sizeof(next_path));

    unlink(next_path);
    new_fd = open(next_path, O_WRONLY | O_CREAT | O_TRUNC | O_APPEND
#ifdef O_CLOEXEC
                          | O_CLOEXEC
#endif
                          , S_IRUSR | S_IWUSR | S_IRGRP);
    if (new_fd < 0) {
        return -1;
    }

    if (close(ctx->fd) < 0) {
        int saved = errno;
        close(new_fd);
        errno = saved;
        return -1;
    }

    ctx->fd = new_fd;
    strncpy(ctx->path, next_path, sizeof(ctx->path) - 1);
    ctx->path[sizeof(ctx->path) - 1] = '\0';
    ctx->next_seqno = 0;

    return 0;
}

static int hifs_mcl_rotate_if_needed(struct hifs_mcl_ctx *ctx, size_t next_write)
{
    struct stat st;
    uint64_t size;
    int rc = 0;

    if (!ctx || ctx->fd < 0) {
        errno = EINVAL;
        return -1;
    }

    pthread_mutex_lock(&g_mcl_rotate_mu);
    if (fstat(ctx->fd, &st) < 0) {
        rc = -1;
        goto out_unlock;
    }

    size = (uint64_t)st.st_size;
    if (size >= HIFS_MCL_MAX_BYTES ||
        next_write >= HIFS_MCL_MAX_BYTES ||
        HIFS_MCL_MAX_BYTES - size <= next_write) {
        rc = hifs_mcl_rotate_locked(ctx);
    }

out_unlock:
    pthread_mutex_unlock(&g_mcl_rotate_mu);
    return rc;
}

int hifs_mcl_open(struct hifs_mcl_ctx *ctx, const char *path, bool create)
{
    int flags;
    int fd;

    if (!ctx || !path) {
        errno = EINVAL;
        return -1;
    }

    flags = O_WRONLY | O_APPEND;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
    if (create) {
        flags |= O_CREAT;
    }

    fd = open(path, flags, S_IRUSR | S_IWUSR | S_IRGRP);
    if (fd < 0) {
        return -1;
    }

    ctx->fd = fd;
    ctx->next_seqno = 0;
    strncpy(ctx->path, path, sizeof(ctx->path) - 1);
    ctx->path[sizeof(ctx->path) - 1] = '\0';

    return 0;
}

int hifs_mcl_close(struct hifs_mcl_ctx *ctx)
{
    int rc;

    if (!ctx) {
        errno = EINVAL;
        return -1;
    }

    if (ctx->fd < 0) {
        return 0;
    }

    rc = close(ctx->fd);
    if (rc < 0) {
        return -1;
    }

    ctx->fd = -1;
    ctx->next_seqno = 0;
    ctx->path[0] = '\0';

    return 0;
}

int hifs_mcl_append(struct hifs_mcl_ctx *ctx,
                    enum hifs_meta_change_item item,
                    enum hifs_mcl_change_type change_type,
                    const void *entry,
                    size_t entry_len,
                    uint64_t txn_id,
                    uint64_t object_id)
{
    struct hifs_mcl_hdr hdr;
    struct hifs_mcl_record_prefix prefix;
    struct iovec iov[3];
    ssize_t n;
    uint64_t seqno;
    size_t expected_len;
    size_t total_len;
    uint32_t crc;
    int iovcnt = 3;
    int rc;

    if (!ctx || ctx->fd < 0 || !entry) {
        errno = EINVAL;
        return -1;
    }

    expected_len = hifs_mcl_payload_len(item);
    if (expected_len == 0) {
        errno = EINVAL;
        return -1;
    }

    if (entry_len == 0) {
        entry_len = expected_len;
    } else if (entry_len != expected_len) {
        errno = EINVAL;
        return -1;
    }

    total_len = sizeof(struct hifs_mcl_hdr) + sizeof(prefix) + entry_len;
    rc = hifs_mcl_rotate_if_needed(ctx, total_len);
    if (rc < 0) {
        return -1;
    }

    seqno = hifs_mcl_next_seqno(ctx);

    prefix.item = (uint8_t)item;
    prefix.change_type = (uint8_t)change_type;

    hdr.magic = HIFS_MCL_MAGIC_VALUE;
    hdr.version = HIFS_MCL_VERSION;
    hdr.type = (uint16_t)item;
    hdr.length = (uint32_t)(sizeof(prefix) + entry_len);
    hdr.seqno = seqno;
    hdr.txn_id = txn_id;
    hdr.object_id = object_id;

    crc = 0xffffffffu;
    crc = hifs_mcl_crc32_seed(crc, &prefix, sizeof(prefix));
    crc = hifs_mcl_crc32_seed(crc, entry, entry_len);
    hdr.crc32 = ~crc;

    iov[0].iov_base = &hdr;
    iov[0].iov_len = sizeof(hdr);
    iov[1].iov_base = &prefix;
    iov[1].iov_len = sizeof(prefix);
    iov[2].iov_base = (void *)entry;
    iov[2].iov_len = entry_len;

    if (entry_len == 0) {
        iovcnt = 2;
    }

    n = writev(ctx->fd, iov, iovcnt);
    if (n < 0) {
        return -1;
    }

    if ((size_t)n != sizeof(hdr) + sizeof(prefix) + (size_t)entry_len) {
        errno = EIO;
        return -1;
    }

    return 0;
}
