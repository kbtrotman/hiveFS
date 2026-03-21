

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__linux__)
#include <sys/random.h>
#endif
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "hive_guard_segmentation.h"
#include "hive_guard_wbl.h"
#include "hive_guard_kv.h"
#include "hive_guard_raft.h"
#include "hive_guard_mcl.h"
#include "hive_guard_stats.h"
#include "hive_guard_sn_tcp.h"
#include "hive_guard.h"

#define HIVE_SEG_UUID_TEXT_LEN HIFS_WBL_UUID_TEXT_LEN


struct hive_seg_uuid_pair {
    char txn_uuid[HIVE_SEG_UUID_TEXT_LEN];
    char stripe_uuid[HIVE_SEG_UUID_TEXT_LEN];
};

struct hive_seg_txn_slot {
    uint64_t txn_id;
    struct hive_seg_uuid_pair ids;
};

struct hive_seg_txn_map {
    struct hive_seg_txn_slot *slots;
    size_t capacity;
    size_t count;
};

struct hive_seg_seg_slot {
    uint64_t seg_id;
    struct hive_seg_uuid_pair ids;
};

struct hive_seg_seg_map {
    struct hive_seg_seg_slot *slots;
    size_t capacity;
    size_t count;
};

static struct hive_seg_txn_map g_seg_txn_map;
static struct hive_seg_seg_map g_seg_seg_map;
static pthread_mutex_t g_seg_txn_map_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_seg_seg_map_mu = PTHREAD_MUTEX_INITIALIZER;

static inline size_t hive_seg_uuid_hash(uint64_t key)
{
    return (size_t)((key * 0x9e3779b185ebca87ull) >> 1);
}

static bool hive_seg_random_bytes(uint8_t *out, size_t len)
{
    if (!out || len == 0)
        return false;
    size_t written = 0;
#if defined(__linux__)
    while (written < len) {
        ssize_t rc = getrandom(out + written, len - written, 0);
        if (rc < 0) {
            if (errno == EINTR)
                continue;
            if (errno == ENOSYS)
                break;
            return false;
        }
        written += (size_t)rc;
    }
    if (written == len)
        return true;
#endif
    int fd = open("/dev/urandom",
                  O_RDONLY
#ifdef O_CLOEXEC
                  | O_CLOEXEC
#endif
    );
    if (fd < 0)
        return false;
    while (written < len) {
        ssize_t rc = read(fd, out + written, len - written);
        if (rc < 0) {
            if (errno == EINTR)
                continue;
            close(fd);
            return false;
        }
        if (rc == 0)
            break;
        written += (size_t)rc;
    }
    close(fd);
    return written == len;
}

static bool hive_seg_uuid_generate(char out[HIVE_SEG_UUID_TEXT_LEN])
{
    if (!out)
        return false;
    uint8_t raw[16];
    if (!hive_seg_random_bytes(raw, sizeof(raw)))
        return false;
    static const char hex[] = "0123456789abcdef";
    size_t pos = 0;
    for (size_t i = 0; i < sizeof(raw); ++i) {
        out[pos++] = hex[raw[i] >> 4];
        out[pos++] = hex[raw[i] & 0x0F];
    }
    out[pos] = '\0';
    return true;
}

static bool hive_seg_txn_map_resize(struct hive_seg_txn_map *map, size_t new_cap)
{
    if (!map || new_cap == 0 || (new_cap & (new_cap - 1)) != 0)
        return false;
    struct hive_seg_txn_slot *slots = calloc(new_cap, sizeof(*slots));
    if (!slots)
        return false;
    size_t inserted = 0;
    if (map->slots) {
        for (size_t i = 0; i < map->capacity; ++i) {
            uint64_t key = map->slots[i].txn_id;
            if (key == 0 || key == UINT64_MAX)
                continue;
            size_t mask = new_cap - 1;
            size_t idx = hive_seg_uuid_hash(key) & mask;
            while (slots[idx].txn_id != 0)
                idx = (idx + 1) & mask;
            slots[idx] = map->slots[i];
            ++inserted;
        }
    }
    free(map->slots);
    map->slots = slots;
    map->capacity = new_cap;
    map->count = inserted;
    return true;
}

static struct hive_seg_txn_slot *
hive_seg_txn_map_lookup(struct hive_seg_txn_map *map,
                        uint64_t key,
                        bool create)
{
    if (!map || key == 0)
        return NULL;
    if (map->capacity == 0) {
        if (!create)
            return NULL;
        if (!hive_seg_txn_map_resize(map, 64))
            return NULL;
    }
retry:
    if (create && (map->count + 1) * 2 > map->capacity) {
        if (!hive_seg_txn_map_resize(map, map->capacity ? map->capacity * 2 : 64))
            return NULL;
        goto retry;
    }
    size_t mask = map->capacity - 1;
    size_t idx = hive_seg_uuid_hash(key) & mask;
    struct hive_seg_txn_slot *free_slot = NULL;
    while (1) {
        struct hive_seg_txn_slot *slot = &map->slots[idx];
        if (slot->txn_id == key)
            return slot;
        if (slot->txn_id == 0) {
            if (!create)
                return NULL;
            if (!free_slot)
                free_slot = slot;
            break;
        }
        if (slot->txn_id == UINT64_MAX && !free_slot)
            free_slot = slot;
        idx = (idx + 1) & mask;
    }
    if (!create)
        return NULL;
    struct hive_seg_txn_slot *slot = free_slot;
    memset(slot, 0, sizeof(*slot));
    slot->txn_id = key;
    map->count++;
    return slot;
}

static void hive_seg_txn_map_remove(struct hive_seg_txn_map *map, uint64_t key)
{
    if (!map || map->capacity == 0 || key == 0)
        return;
    size_t mask = map->capacity - 1;
    size_t idx = hive_seg_uuid_hash(key) & mask;
    while (1) {
        struct hive_seg_txn_slot *slot = &map->slots[idx];
        if (slot->txn_id == key) {
            slot->txn_id = UINT64_MAX;
            memset(&slot->ids, 0, sizeof(slot->ids));
            if (map->count > 0)
                --map->count;
            return;
        }
        if (slot->txn_id == 0)
            return;
        idx = (idx + 1) & mask;
    }
}

static bool hive_seg_seg_map_resize(struct hive_seg_seg_map *map, size_t new_cap)
{
    if (!map || new_cap == 0 || (new_cap & (new_cap - 1)) != 0)
        return false;
    struct hive_seg_seg_slot *slots = calloc(new_cap, sizeof(*slots));
    if (!slots)
        return false;
    size_t inserted = 0;
    if (map->slots) {
        for (size_t i = 0; i < map->capacity; ++i) {
            uint64_t key = map->slots[i].seg_id;
            if (key == 0 || key == UINT64_MAX)
                continue;
            size_t mask = new_cap - 1;
            size_t idx = hive_seg_uuid_hash(key) & mask;
            while (slots[idx].seg_id != 0)
                idx = (idx + 1) & mask;
            slots[idx] = map->slots[i];
            ++inserted;
        }
    }
    free(map->slots);
    map->slots = slots;
    map->capacity = new_cap;
    map->count = inserted;
    return true;
}

static struct hive_seg_seg_slot *
hive_seg_seg_map_lookup(struct hive_seg_seg_map *map,
                        uint64_t key,
                        bool create)
{
    if (!map || key == 0)
        return NULL;
    if (map->capacity == 0) {
        if (!create)
            return NULL;
        if (!hive_seg_seg_map_resize(map, 64))
            return NULL;
    }
retry:
    if (create && (map->count + 1) * 2 > map->capacity) {
        if (!hive_seg_seg_map_resize(map, map->capacity ? map->capacity * 2 : 64))
            return NULL;
        goto retry;
    }
    size_t mask = map->capacity - 1;
    size_t idx = hive_seg_uuid_hash(key) & mask;
    struct hive_seg_seg_slot *free_slot = NULL;
    while (1) {
        struct hive_seg_seg_slot *slot = &map->slots[idx];
        if (slot->seg_id == key)
            return slot;
        if (slot->seg_id == 0) {
            if (!create)
                return NULL;
            if (!free_slot)
                free_slot = slot;
            break;
        }
        if (slot->seg_id == UINT64_MAX && !free_slot)
            free_slot = slot;
        idx = (idx + 1) & mask;
    }
    if (!create)
        return NULL;
    struct hive_seg_seg_slot *slot = free_slot;
    memset(slot, 0, sizeof(*slot));
    slot->seg_id = key;
    map->count++;
    return slot;
}

static void hive_seg_seg_map_remove(struct hive_seg_seg_map *map, uint64_t key)
{
    if (!map || map->capacity == 0 || key == 0)
        return;
    size_t mask = map->capacity - 1;
    size_t idx = hive_seg_uuid_hash(key) & mask;
    while (1) {
        struct hive_seg_seg_slot *slot = &map->slots[idx];
        if (slot->seg_id == key) {
            slot->seg_id = UINT64_MAX;
            memset(&slot->ids, 0, sizeof(slot->ids));
            if (map->count > 0)
                --map->count;
            return;
        }
        if (slot->seg_id == 0)
            return;
        idx = (idx + 1) & mask;
    }
}

static bool hive_seg_txn_uuid_fill(uint64_t txn_id,
                                   char out[HIVE_SEG_UUID_TEXT_LEN])
{
    if (!out || txn_id == 0)
        return false;
    bool ok = false;
    pthread_mutex_lock(&g_seg_txn_map_mu);
    struct hive_seg_txn_slot *slot =
        hive_seg_txn_map_lookup(&g_seg_txn_map, txn_id, true);
    if (slot) {
        if (slot->ids.txn_uuid[0] == '\0')
            ok = hive_seg_uuid_generate(slot->ids.txn_uuid);
        else
            ok = true;
        if (ok && slot->ids.stripe_uuid[0] == '\0')
            ok = hive_seg_uuid_generate(slot->ids.stripe_uuid);
        if (ok)
            memcpy(out, slot->ids.txn_uuid, HIVE_SEG_UUID_TEXT_LEN);
    }
    pthread_mutex_unlock(&g_seg_txn_map_mu);
    if (!ok) {
        hifs_err("hive_segmentation: unable to allocate txn uuid for txn=%llu",
                 (unsigned long long)txn_id);
    }
    return ok;
}

static bool hive_seg_stripe_uuid_fill(uint64_t txn_id,
                                      char out[HIVE_SEG_UUID_TEXT_LEN])
{
    if (!out || txn_id == 0)
        return false;
    pthread_mutex_lock(&g_seg_txn_map_mu);
    struct hive_seg_txn_slot *slot =
        hive_seg_txn_map_lookup(&g_seg_txn_map, txn_id, true);
    bool ok = false;
    if (slot) {
        if (slot->ids.stripe_uuid[0] == '\0')
            ok = hive_seg_uuid_generate(slot->ids.stripe_uuid);
        else
            ok = true;
        if (ok && slot->ids.txn_uuid[0] == '\0')
            ok = hive_seg_uuid_generate(slot->ids.txn_uuid);
        if (ok)
            memcpy(out, slot->ids.stripe_uuid, HIVE_SEG_UUID_TEXT_LEN);
    }
    pthread_mutex_unlock(&g_seg_txn_map_mu);
    if (!ok) {
        hifs_err("hive_segmentation: unable to allocate stripe uuid for txn=%llu",
                 (unsigned long long)txn_id);
    }
    return ok;
}

static void hive_seg_txn_identity_forget(uint64_t txn_id)
{
    if (txn_id == 0)
        return;
    pthread_mutex_lock(&g_seg_txn_map_mu);
    hive_seg_txn_map_remove(&g_seg_txn_map, txn_id);
    pthread_mutex_unlock(&g_seg_txn_map_mu);
}

static bool hive_seg_seg_txn_uuid_fill(uint64_t seg_id,
                                       char out[HIVE_SEG_UUID_TEXT_LEN])
{
    if (!out || seg_id == 0)
        return false;
    bool ok = false;
    pthread_mutex_lock(&g_seg_seg_map_mu);
    struct hive_seg_seg_slot *slot =
        hive_seg_seg_map_lookup(&g_seg_seg_map, seg_id, true);
    if (slot) {
        if (slot->ids.txn_uuid[0] == '\0')
            ok = hive_seg_uuid_generate(slot->ids.txn_uuid);
        else
            ok = true;
        if (ok && slot->ids.stripe_uuid[0] == '\0')
            ok = hive_seg_uuid_generate(slot->ids.stripe_uuid);
        if (ok)
            memcpy(out, slot->ids.txn_uuid, HIVE_SEG_UUID_TEXT_LEN);
    }
    pthread_mutex_unlock(&g_seg_seg_map_mu);
    if (!ok) {
        hifs_err("hive_segmentation: unable to allocate txn uuid for seg=%llu",
                 (unsigned long long)seg_id);
    }
    return ok;
}

static bool hive_seg_seg_stripe_uuid_fill(uint64_t seg_id,
                                          char out[HIVE_SEG_UUID_TEXT_LEN])
{
    if (!out || seg_id == 0)
        return false;
    pthread_mutex_lock(&g_seg_seg_map_mu);
    struct hive_seg_seg_slot *slot =
        hive_seg_seg_map_lookup(&g_seg_seg_map, seg_id, true);
    bool ok = false;
    if (slot) {
        if (slot->ids.stripe_uuid[0] == '\0')
            ok = hive_seg_uuid_generate(slot->ids.stripe_uuid);
        else
            ok = true;
        if (ok && slot->ids.txn_uuid[0] == '\0')
            ok = hive_seg_uuid_generate(slot->ids.txn_uuid);
        if (ok)
            memcpy(out, slot->ids.stripe_uuid, HIVE_SEG_UUID_TEXT_LEN);
    }
    pthread_mutex_unlock(&g_seg_seg_map_mu);
    if (!ok) {
        hifs_err("hive_segmentation: unable to allocate stripe uuid for seg=%llu",
                 (unsigned long long)seg_id);
    }
    return ok;
}

static void hive_seg_seg_identity_drop(uint64_t seg_id)
{
    if (seg_id == 0)
        return;
    pthread_mutex_lock(&g_seg_seg_map_mu);
    hive_seg_seg_map_remove(&g_seg_seg_map, seg_id);
    pthread_mutex_unlock(&g_seg_seg_map_mu);
}

static struct hive_seg_task g_seg_queue[HIVE_SEG_QUEUE_DEPTH];
static size_t g_seg_head;
static size_t g_seg_tail;
static size_t g_seg_size;
static pthread_mutex_t g_seg_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_seg_cv = PTHREAD_COND_INITIALIZER;
static pthread_cond_t g_seg_space_cv = PTHREAD_COND_INITIALIZER;
static pthread_t g_seg_workers[HIVE_SEG_MAX_WORKERS];
static size_t g_seg_worker_count;
static _Atomic bool g_seg_started = false;
static bool g_seg_running;
static bool g_seg_sn_running;
static atomic_uint_fast64_t g_seg_txn_seq = ATOMIC_VAR_INIT(1);
static pthread_mutex_t g_seg_landing_io_mu = PTHREAD_MUTEX_INITIALIZER;
static struct hive_seg_cache_ctx g_seg_cache = {
    .mu = PTHREAD_MUTEX_INITIALIZER,
    .ready = false,
    .landing_idx_fd = -1,
    .outbound_idx_fd = -1,
    .read_return_idx_fd = -1,
    .landing_idx = {
        .entries = NULL,
        .count = 0,
        .capacity = 0,
    },
    .outbound_idx = {
        .entries = NULL,
        .count = 0,
        .capacity = 0,
    },
    .read_return_idx = {
        .entries = NULL,
        .count = 0,
        .capacity = 0,
    },
};

_Static_assert(HIFS_EC_TOTAL_SRIPES <= 8,
               "outbound stripe commit bitmap supports at most 8 fragments");
_Static_assert(sizeof(struct hifs_fragment_target) ==
                   sizeof(uint32_t) * 2u,
               "fragment target layout changed; update outbound commit handling");

#define HIVE_SEG_FULL_ACK_BITMAP                                            \
    ((uint8_t)(((uint32_t)1u << (uint32_t)HIFS_EC_TOTAL_SRIPES) - 1u))

struct hive_seg_outbound_commit_entry {
    uint64_t seg_id;
    uint32_t node_id;
};

static pthread_mutex_t g_seg_outbound_commit_mu = PTHREAD_MUTEX_INITIALIZER;
static struct hive_seg_outbound_commit_entry *g_seg_outbound_commits;
static size_t g_seg_outbound_commit_count;
static size_t g_seg_outbound_commit_capacity;

static int hive_seg_persist_outbound_segment(uint32_t node_id,
                                             uint64_t seg_id);

static bool hive_seg_outbound_segment_closed(uint32_t node_id,
                                             uint64_t seg_id)
{
    bool closed = false;
    pthread_mutex_lock(&g_seg_outbound_commit_mu);
    for (size_t i = 0; i < g_seg_outbound_commit_count; ++i) {
        if (g_seg_outbound_commits[i].node_id == node_id &&
            g_seg_outbound_commits[i].seg_id == seg_id) {
            closed = true;
            break;
        }
    }
    pthread_mutex_unlock(&g_seg_outbound_commit_mu);
    return closed;
}

struct hive_seg_writable_region {
    uint64_t seg_id;
    uint32_t next_offset;
    uint32_t free_bytes;
};

static pthread_mutex_t g_seg_writable_mu = PTHREAD_MUTEX_INITIALIZER;
static struct hive_seg_writable_region *g_seg_writable_regions;
static size_t g_seg_writable_region_count;
static size_t g_seg_writable_region_capacity;

static inline uint32_t hive_seg_writable_align(uint32_t len)
{
    return hive_seg_align_len(len);
}

static struct hive_seg_writable_region *
hive_seg_writable_find_locked(uint64_t seg_id)
{
    for (size_t i = 0; i < g_seg_writable_region_count; ++i) {
        if (g_seg_writable_regions[i].seg_id == seg_id)
            return &g_seg_writable_regions[i];
    }
    return NULL;
}

static void hive_seg_writable_remove_idx_locked(size_t idx)
{
    if (idx >= g_seg_writable_region_count)
        return;
    if (idx + 1u < g_seg_writable_region_count) {
        g_seg_writable_regions[idx] =
            g_seg_writable_regions[g_seg_writable_region_count - 1u];
    }
    --g_seg_writable_region_count;
}

static bool hive_seg_writable_track(uint64_t seg_id,
                                    uint32_t start_offset,
                                    uint32_t free_bytes)
{
    uint32_t aligned = hive_seg_writable_align(free_bytes);
    if (aligned == 0)
        return false;

    pthread_mutex_lock(&g_seg_writable_mu);
    struct hive_seg_writable_region *region =
        hive_seg_writable_find_locked(seg_id);
    if (!region) {
        if (g_seg_writable_region_count == g_seg_writable_region_capacity) {
            size_t new_cap = g_seg_writable_region_capacity ?
                             g_seg_writable_region_capacity * 2u : 16u;
            void *tmp = realloc(g_seg_writable_regions,
                                new_cap * sizeof(*g_seg_writable_regions));
            if (!tmp) {
                pthread_mutex_unlock(&g_seg_writable_mu);
                return false;
            }
            g_seg_writable_regions = tmp;
            g_seg_writable_region_capacity = new_cap;
        }
        region = &g_seg_writable_regions[g_seg_writable_region_count++];
        region->seg_id = seg_id;
        region->next_offset = start_offset;
        region->free_bytes = aligned;
    } else {
        region->next_offset = start_offset;
        region->free_bytes = aligned;
    }
    pthread_mutex_unlock(&g_seg_writable_mu);
    return true;
}

static bool hive_seg_writable_alloc(uint32_t len, struct hifs_seg_loc *loc)
{
    if (!loc || len == 0)
        return false;

    uint32_t aligned = hive_seg_writable_align(len);
    pthread_mutex_lock(&g_seg_writable_mu);
    size_t best_idx = SIZE_MAX;
    uint32_t best_slack = UINT32_MAX;
    for (size_t i = 0; i < g_seg_writable_region_count; ++i) {
        struct hive_seg_writable_region *region = &g_seg_writable_regions[i];
        if (region->free_bytes < aligned)
            continue;
        uint32_t slack = region->free_bytes - aligned;
        if (slack < best_slack) {
            best_slack = slack;
            best_idx = i;
            if (slack == 0)
                break;
        }
    }

    if (best_idx == SIZE_MAX) {
        pthread_mutex_unlock(&g_seg_writable_mu);
        return false;
    }

    struct hive_seg_writable_region *region =
        &g_seg_writable_regions[best_idx];
    loc->seg_id = region->seg_id;
    loc->offset = region->next_offset;
    loc->length = len;

    region->next_offset += aligned;
    region->free_bytes -= aligned;
    if (region->free_bytes == 0)
        hive_seg_writable_remove_idx_locked(best_idx);

    pthread_mutex_unlock(&g_seg_writable_mu);
    return true;
}

static bool hive_seg_outbound_segment_mark_closed(uint32_t node_id,
                                                  uint64_t seg_id)
{
    pthread_mutex_lock(&g_seg_outbound_commit_mu);
    for (size_t i = 0; i < g_seg_outbound_commit_count; ++i) {
        if (g_seg_outbound_commits[i].node_id == node_id &&
            g_seg_outbound_commits[i].seg_id == seg_id) {
            pthread_mutex_unlock(&g_seg_outbound_commit_mu);
            return false;
        }
    }

    if (g_seg_outbound_commit_count == g_seg_outbound_commit_capacity) {
        size_t new_cap = g_seg_outbound_commit_capacity ?
                         g_seg_outbound_commit_capacity * 2u : 64u;
        void *tmp = realloc(g_seg_outbound_commits,
                            new_cap * sizeof(*g_seg_outbound_commits));
        if (!tmp) {
            pthread_mutex_unlock(&g_seg_outbound_commit_mu);
            hifs_err("hive_segmentation: unable to grow outbound commit tracking for seg=%llu node=%u",
                     (unsigned long long)seg_id,
                     node_id);
            return true;
        }
        g_seg_outbound_commits = tmp;
        g_seg_outbound_commit_capacity = new_cap;
    }

    g_seg_outbound_commits[g_seg_outbound_commit_count++] =
        (struct hive_seg_outbound_commit_entry){
            .seg_id = seg_id,
            .node_id = node_id,
        };
    pthread_mutex_unlock(&g_seg_outbound_commit_mu);
    return true;
}

static struct hive_seg_index_disk_rec *
hive_seg_cache_find_landing_rec_locked(uint64_t volume_id,
                                       uint64_t block_no,
                                       size_t *out_idx)
{
    if (!g_seg_cache.landing_idx.entries)
        return NULL;
    for (size_t i = 0; i < g_seg_cache.landing_idx.count; ++i) {
        struct hive_seg_index_disk_rec *rec =
            &g_seg_cache.landing_idx.entries[i];
        if (rec->volume_id == volume_id && rec->block_no == block_no) {
            if (out_idx)
                *out_idx = i;
            return rec;
        }
    }
    return NULL;
}

static bool hive_seg_cache_is_tail_block_locked(const struct hive_seg_index_disk_rec *rec,
                                                size_t idx)
{
    if (!rec)
        return false;
    uint64_t seg_id = rec->seg_id;
    uint64_t tail_end = rec->offset + (uint64_t)hive_seg_align_len(rec->length);
    for (size_t i = 0; i < g_seg_cache.landing_idx.count; ++i) {
        if (i == idx)
            continue;
        const struct hive_seg_index_disk_rec *other =
            &g_seg_cache.landing_idx.entries[i];
        if (other->seg_id != seg_id)
            continue;
        uint64_t other_end =
            other->offset + (uint64_t)hive_seg_align_len(other->length);
        if (other_end > tail_end)
            return false;
        if (other_end == tail_end && other->offset > rec->offset)
            return false;
    }
    return true;
}

static int hive_seg_landing_sync_entry_locked(size_t idx)
{
    if (g_seg_cache.landing_idx_fd < 0)
        return 0;
    if (idx >= g_seg_cache.landing_idx.count)
        return -EINVAL;
    off_t offset = (off_t)idx * (off_t)sizeof(struct hive_seg_index_disk_rec);
    return hive_seg_pwrite_all(g_seg_cache.landing_idx_fd,
                               &g_seg_cache.landing_idx.entries[idx],
                               sizeof(struct hive_seg_index_disk_rec),
                               offset);
}

static void hive_seg_landing_remove_entry_locked(size_t idx)
{
    struct hive_seg_index_mem *mem = &g_seg_cache.landing_idx;
    if (idx >= mem->count)
        return;
    if (idx + 1u < mem->count) {
        memmove(&mem->entries[idx],
                &mem->entries[idx + 1u],
                (mem->count - idx - 1u) * sizeof(*mem->entries));
    }
    --mem->count;
    hive_seg_rewrite_index_locked(g_seg_cache.landing_idx_fd,
                                  mem,
                                  0,
                                  "landing");
}

static bool hive_seg_prepare_existing_landing(struct hive_seg_block_input *blk)
{
    if (!blk || blk->len == 0)
        return false;

    if (hive_seg_cache_init() != 0)
        return false;

    pthread_mutex_lock(&g_seg_cache.mu);
    if (!g_seg_cache.ready) {
        pthread_mutex_unlock(&g_seg_cache.mu);
        return false;
    }

    size_t idx = 0;
    struct hive_seg_index_disk_rec *rec =
        hive_seg_cache_find_landing_rec_locked(blk->volume_id,
                                               blk->block_no,
                                               &idx);
    if (!rec) {
        pthread_mutex_unlock(&g_seg_cache.mu);
        return false;
    }

    uint32_t old_len = rec->length;
    if (old_len == blk->len) {
        blk->landing_loc.seg_id = rec->seg_id;
        blk->landing_loc.offset = rec->offset;
        blk->landing_loc.length = rec->length;
        blk->has_landing_loc = true;
        pthread_mutex_unlock(&g_seg_cache.mu);
        return true;
    }

    uint32_t old_aligned = hive_seg_align_len(old_len);
    uint32_t new_aligned = hive_seg_align_len(blk->len);
    bool is_tail = hive_seg_cache_is_tail_block_locked(rec, idx);
    struct hive_seg_index_disk_rec rec_copy = *rec;

    if (blk->len < old_len && is_tail && old_aligned >= new_aligned) {
        uint32_t freed_bytes = old_aligned - new_aligned;
        rec->length = blk->len;
        hive_seg_landing_sync_entry_locked(idx);
        pthread_mutex_unlock(&g_seg_cache.mu);

        blk->landing_loc.seg_id = rec_copy.seg_id;
        blk->landing_loc.offset = rec_copy.offset;
        blk->landing_loc.length = blk->len;
        blk->has_landing_loc = true;

        if (freed_bytes > 0) {
            uint32_t start = rec_copy.offset + new_aligned;
            hive_seg_writable_track(rec_copy.seg_id, start, freed_bytes);
        }
        return true;
    }

    hive_seg_landing_remove_entry_locked(idx);
    pthread_mutex_unlock(&g_seg_cache.mu);

    if (is_tail && old_aligned > 0)
        hive_seg_writable_track(rec_copy.seg_id, rec_copy.offset, old_aligned);
    return false;
}

static void hive_seg_log_stripe_committed_record(uint64_t seg_id,
                                                 uint8_t ack_bitmap)
{
    char txn_uuid[HIVE_SEG_UUID_TEXT_LEN];
    char stripe_uuid[HIVE_SEG_UUID_TEXT_LEN];
    if (!hive_seg_seg_txn_uuid_fill(seg_id, txn_uuid) ||
        !hive_seg_seg_stripe_uuid_fill(seg_id, stripe_uuid))
        return;

    struct hifs_wbl_commit_rec rec = {
        .ack_bitmap = ack_bitmap,
    };
    memcpy(rec.txn_id, txn_uuid, sizeof(rec.txn_id));
    memcpy(rec.stripe_id, stripe_uuid, sizeof(rec.stripe_id));
    if (hifs_wbl_mark_committed(&g_hive_guard_wbl_ctx, &rec) != 0) {
        hifs_err("hive_segmentation: unable to append WBL stripe committed record for seg=%llu: %s",
                 (unsigned long long)seg_id,
                 strerror(errno));
    }
}

static void hive_seg_log_stripe_persisting_record(uint64_t seg_id,
                                                  uint8_t persist_bitmap)
{
    char txn_uuid[HIVE_SEG_UUID_TEXT_LEN];
    char stripe_uuid[HIVE_SEG_UUID_TEXT_LEN];
    if (!hive_seg_seg_txn_uuid_fill(seg_id, txn_uuid) ||
        !hive_seg_seg_stripe_uuid_fill(seg_id, stripe_uuid))
        return;

    struct hifs_wbl_persisting_rec rec = {
        .persist_bitmap = persist_bitmap,
    };
    memcpy(rec.txn_id, txn_uuid, sizeof(rec.txn_id));
    memcpy(rec.stripe_id, stripe_uuid, sizeof(rec.stripe_id));
    if (hifs_wbl_mark_persisting(&g_hive_guard_wbl_ctx, &rec) != 0) {
        hifs_err("hive_segmentation: unable to append WBL stripe persisting record for seg=%llu: %s",
                 (unsigned long long)seg_id,
                 strerror(errno));
    }
}

static void hive_seg_publish_segment_commit(uint32_t node_id,
                                            uint64_t seg_id,
                                            uint8_t ack_bitmap)
{
    struct RaftCmd cmd = {0};
    cmd.op_type = HG_OP_PUT_CLUSTER_AUDIT;
    struct RaftClusterAudit *audit = &cmd.u.cluster_audit;
    audit->cluster_id = hbc.cluster_id;
    audit->node_id = storage_node_id;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    audit->timestamp_ns = (uint64_t)ts.tv_sec * 1000000000ull +
                          (uint64_t)ts.tv_nsec;
    snprintf(audit->message,
             sizeof(audit->message),
             "stripe-commit seg=%llu node=%u ack=0x%02x",
             (unsigned long long)seg_id,
             node_id,
             ack_bitmap);
    int rc = hg_raft_submit_cmd_sync(&cmd, NULL);
    if (rc != 0) {
        int err = rc < 0 ? -rc : rc;
        hifs_err("hive_segmentation: failed to publish raft commit for seg=%llu: %s",
                 (unsigned long long)seg_id,
                 strerror(err));
    }
}

static void hive_seg_finalize_persisting(uint32_t node_id,
                                         uint64_t seg_id,
                                         uint8_t persist_bitmap);

static void hive_seg_stripe_commit(uint32_t node_id,
                                   uint64_t seg_id,
                                   uint8_t ack_bitmap)
{
    if (!hive_seg_outbound_segment_mark_closed(node_id, seg_id))
        return;

    int rc = hive_seg_persist_outbound_segment(node_id, seg_id);
    if (rc != 0) {
        int err = rc < 0 ? -rc : rc;
        hifs_crit("hive_segmentation: failed to persist outbound segment seg=%llu node=%u: %s",
                 (unsigned long long)seg_id,
                 node_id,
                 strerror(err));
        return;
    }

    hive_seg_log_stripe_persisting_record(seg_id, ack_bitmap);
    hive_seg_publish_segment_commit(node_id, seg_id, ack_bitmap);
    hive_seg_finalize_persisting(node_id, seg_id, ack_bitmap);
}

static void hive_seg_remove_outbound_segment(uint32_t node_id,
                                             uint64_t seg_id)
{
    if (node_id == 0 || seg_id == 0)
        return;

    char path[PATH_MAX];
    hive_seg_outbound_path(path, sizeof(path), node_id, seg_id);
    if (unlink(path) != 0 && errno != ENOENT) {
        hifs_err("hive_segmentation: failed to remove outbound segment seg=%llu node=%u: %s",
                 (unsigned long long)seg_id,
                 node_id,
                 strerror(errno));
    }
}

static bool hive_seg_index_mem_prune_seg(struct hive_seg_index_mem *mem,
                                         uint64_t seg_id)
{
    if (!mem || !mem->entries || mem->count == 0)
        return false;

    size_t write_idx = 0;
    bool removed = false;
    for (size_t i = 0; i < mem->count; ++i) {
        struct hive_seg_index_disk_rec *rec = &mem->entries[i];
        if (rec->seg_id == seg_id) {
            removed = true;
            continue;
        }
        if (write_idx != i)
            mem->entries[write_idx] = *rec;
        ++write_idx;
    }
    if (removed)
        mem->count = write_idx;
    return removed;
}

static void hive_seg_rewrite_index_locked(int fd,
                                          const struct hive_seg_index_mem *mem,
                                          uint64_t seg_id,
                                          const char *label)
{
    if (fd < 0 || !mem || !mem->entries)
        return;
    if (ftruncate(fd, 0) != 0) {
        hifs_err("hive_segmentation: failed to truncate %s index after persisting seg=%llu: %s",
                 label,
                 (unsigned long long)seg_id,
                 strerror(errno));
        return;
    }
    if (mem->count == 0)
        return;
    if (lseek(fd, 0, SEEK_SET) < 0) {
        hifs_err("hive_segmentation: failed to seek %s index after persisting seg=%llu: %s",
                 label,
                 (unsigned long long)seg_id,
                 strerror(errno));
        return;
    }
    size_t bytes = mem->count * sizeof(struct hive_seg_index_disk_rec);
    int rc = hive_seg_write_all(fd, mem->entries, bytes);
    if (rc != 0) {
        int err = rc < 0 ? -rc : rc;
        hifs_err("hive_segmentation: failed to rewrite %s index after persisting seg=%llu: %s",
                 label,
                 (unsigned long long)seg_id,
                 strerror(err));
    }
}

static void hive_seg_cache_drop_segment(uint64_t seg_id)
{
    if (seg_id == 0)
        return;

    pthread_mutex_lock(&g_seg_cache.mu);
    if (!g_seg_cache.ready) {
        pthread_mutex_unlock(&g_seg_cache.mu);
        return;
    }

    bool outbound_removed = hive_seg_index_mem_prune_seg(&g_seg_cache.outbound_idx,
                                                         seg_id);
    bool landing_removed = hive_seg_index_mem_prune_seg(&g_seg_cache.landing_idx,
                                                        seg_id);
    bool read_return_removed = hive_seg_index_mem_prune_seg(&g_seg_cache.read_return_idx,
                                                            seg_id);

    if (outbound_removed)
        hive_seg_rewrite_index_locked(g_seg_cache.outbound_idx_fd,
                                      &g_seg_cache.outbound_idx,
                                      seg_id,
                                      "outbound");
    if (landing_removed)
        hive_seg_rewrite_index_locked(g_seg_cache.landing_idx_fd,
                                      &g_seg_cache.landing_idx,
                                      seg_id,
                                      "landing");
    if (read_return_removed)
        hive_seg_rewrite_index_locked(g_seg_cache.read_return_idx_fd,
                                      &g_seg_cache.read_return_idx,
                                      seg_id,
                                      "read-return");

    pthread_mutex_unlock(&g_seg_cache.mu);
}

static struct hive_seg_index_disk_rec *
hive_seg_cache_find_read_return_locked(uint64_t seg_id)
{
    if (!g_seg_cache.read_return_idx.entries)
        return NULL;
    for (size_t idx = g_seg_cache.read_return_idx.count; idx > 0; --idx) {
        struct hive_seg_index_disk_rec *rec =
            &g_seg_cache.read_return_idx.entries[idx - 1];
        if (rec->seg_id == seg_id)
            return rec;
    }
    return NULL;
}

static int hive_seg_cache_track_read_checkout_locked(uint64_t seg_id,
                                                     uint32_t length)
{
    struct hive_seg_index_disk_rec rec = {
        .seg_id = seg_id,
        .txn_id = seg_id,
        .state = HIFS_STRIPE_OUTBOUND_QUEUED,
        .length = length,
    };

    if (!hive_seg_index_mem_append(&g_seg_cache.read_return_idx, &rec))
        return -ENOMEM;

    if (g_seg_cache.read_return_idx_fd >= 0) {
        int rc = hive_seg_write_all(g_seg_cache.read_return_idx_fd,
                                    &rec,
                                    sizeof(rec));
        if (rc != 0) {
            --g_seg_cache.read_return_idx.count;
            return rc;
        }
    }
    return 0;
}

static bool hive_seg_cache_drop_read_checkout_locked(uint64_t seg_id)
{
    bool removed = hive_seg_index_mem_prune_seg(&g_seg_cache.read_return_idx,
                                                seg_id);
    if (removed) {
        hive_seg_rewrite_index_locked(g_seg_cache.read_return_idx_fd,
                                      &g_seg_cache.read_return_idx,
                                      seg_id,
                                      "read-return");
    }
    return removed;
}

int hive_seg_checkout_persisted_segment(uint64_t seg_id,
                                        uint8_t **out_data,
                                        size_t *out_len)
{
    if (!out_data || !out_len || seg_id == 0)
        return -EINVAL;

    uint8_t *data = NULL;
    size_t len = 0;
    if (hg_kv_get_estripe_chunk(seg_id, &data, &len) != 0)
        return -ENOENT;

    int rc = hive_seg_cache_init();
    if (rc != 0) {
        free(data);
        return rc;
    }

    pthread_mutex_lock(&g_seg_cache.mu);
    if (!g_seg_cache.ready) {
        pthread_mutex_unlock(&g_seg_cache.mu);
        free(data);
        return -ESHUTDOWN;
    }

    if (hive_seg_cache_find_read_return_locked(seg_id)) {
        pthread_mutex_unlock(&g_seg_cache.mu);
        free(data);
        return -EALREADY;
    }

    uint32_t tracked_len = len > UINT32_MAX ? UINT32_MAX : (uint32_t)len;
    rc = hive_seg_cache_track_read_checkout_locked(seg_id, tracked_len);
    pthread_mutex_unlock(&g_seg_cache.mu);

    if (rc != 0) {
        free(data);
        return rc;
    }

    hive_seg_log_stripe_committed_record(seg_id, HIVE_SEG_FULL_ACK_BITMAP);

    *out_data = data;
    *out_len = len;
    return 0;
}

void hive_seg_release_persisted_segment(uint64_t seg_id, bool changed)
{
    if (seg_id == 0)
        return;

    hive_seg_cache_init();
    pthread_mutex_lock(&g_seg_cache.mu);
    if (g_seg_cache.ready)
        hive_seg_cache_drop_read_checkout_locked(seg_id);
    pthread_mutex_unlock(&g_seg_cache.mu);

    if (changed) {
        hive_seg_log_stripe_persisting_record(seg_id, HIVE_SEG_FULL_ACK_BITMAP);
        hive_seg_log_stripe_persisted_record(seg_id, HIVE_SEG_FULL_ACK_BITMAP);
    }

    hive_seg_log_stripe_reclaimable_record(seg_id, !changed);
    hive_seg_seg_identity_drop(seg_id);
}

static void hive_seg_publish_segment_persisted(uint32_t node_id,
                                               uint64_t seg_id)
{
    if (node_id == 0 || seg_id == 0)
        return;

    struct RaftCmd cmd = {0};
    cmd.op_type = HG_OP_PUT_CLUSTER_AUDIT;
    struct RaftClusterAudit *audit = &cmd.u.cluster_audit;
    audit->cluster_id = hbc.cluster_id;
    audit->node_id = storage_node_id;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    audit->timestamp_ns = (uint64_t)ts.tv_sec * 1000000000ull +
                          (uint64_t)ts.tv_nsec;
    snprintf(audit->message,
             sizeof(audit->message),
             "stripe-persisted seg=%llu node=%u",
             (unsigned long long)seg_id,
             node_id);
    int rc = hg_raft_submit_cmd_sync(&cmd, NULL);
    if (rc != 0) {
        int err = rc < 0 ? -rc : rc;
        hifs_err("hive_segmentation: failed to publish raft persisted event for seg=%llu: %s",
                 (unsigned long long)seg_id,
                 strerror(err));
    }
}

static void hive_seg_log_stripe_persisted_record(uint64_t seg_id,
                                                 uint8_t persist_bitmap)
{
    if (seg_id == 0)
        return;

    char txn_uuid[HIVE_SEG_UUID_TEXT_LEN];
    char stripe_uuid[HIVE_SEG_UUID_TEXT_LEN];
    if (!hive_seg_seg_txn_uuid_fill(seg_id, txn_uuid) ||
        !hive_seg_seg_stripe_uuid_fill(seg_id, stripe_uuid))
        return;

    struct hifs_wbl_persisted_rec rec = {
        .persist_bitmap = persist_bitmap,
    };
    memcpy(rec.txn_id, txn_uuid, sizeof(rec.txn_id));
    memcpy(rec.stripe_id, stripe_uuid, sizeof(rec.stripe_id));
    if (hifs_wbl_mark_persisted(&g_hive_guard_wbl_ctx, &rec) != 0) {
        hifs_err("hive_segmentation: unable to append WBL stripe persisted record for seg=%llu: %s",
                 (unsigned long long)seg_id,
                 strerror(errno));
    }
}

static void hive_seg_log_stripe_reclaimable_record(uint64_t seg_id,
                                                   bool unchanged)
{
    if (seg_id == 0)
        return;

    char txn_uuid[HIVE_SEG_UUID_TEXT_LEN];
    char stripe_uuid[HIVE_SEG_UUID_TEXT_LEN];
    if (!hive_seg_seg_txn_uuid_fill(seg_id, txn_uuid) ||
        !hive_seg_seg_stripe_uuid_fill(seg_id, stripe_uuid))
        return;

    struct hifs_wbl_reclaimable_rec rec = {
        .flags = unchanged ? HIFS_WBL_RECLAIMABLE_F_UNCHANGED : 0u,
    };
    memcpy(rec.txn_id, txn_uuid, sizeof(rec.txn_id));
    memcpy(rec.stripe_id, stripe_uuid, sizeof(rec.stripe_id));
    if (hifs_wbl_mark_reclaimable(&g_hive_guard_wbl_ctx, &rec) != 0) {
        hifs_err("hive_segmentation: unable to append WBL stripe reclaimable record for seg=%llu: %s",
                 (unsigned long long)seg_id,
                 strerror(errno));
    }
}

static void hive_seg_finalize_persisting(uint32_t node_id,
                                         uint64_t seg_id,
                                         uint8_t persist_bitmap)
{
    hive_seg_remove_outbound_segment(node_id, seg_id);
    hive_seg_cache_drop_segment(seg_id);
    hive_seg_publish_segment_persisted(node_id, seg_id);
    hive_seg_log_stripe_persisted_record(seg_id, persist_bitmap);
    hive_seg_log_stripe_reclaimable_record(seg_id, false);
    hive_seg_seg_identity_drop(seg_id);
}
static void hive_seg_cache_drop_remote_fragment(uint32_t node_id,
                                                uint64_t seg_id)
{
    pthread_mutex_lock(&g_seg_cache.mu);
    if (!g_seg_cache.ready) {
        pthread_mutex_unlock(&g_seg_cache.mu);
        return;
    }

    bool removed = false;
    size_t write_idx = 0;
    for (size_t i = 0; i < g_seg_cache.outbound_idx.count; ++i) {
        struct hive_seg_index_disk_rec *rec =
            &g_seg_cache.outbound_idx.entries[i];
        if (rec->seg_id == seg_id && rec->reserved == node_id &&
            rec->volume_id == 0 && rec->block_no == 0) {
            removed = true;
            continue;
        }
        if (write_idx != i)
            g_seg_cache.outbound_idx.entries[write_idx] = *rec;
        ++write_idx;
    }

    if (removed) {
        g_seg_cache.outbound_idx.count = write_idx;
        int fd = g_seg_cache.outbound_idx_fd;
        if (fd >= 0) {
            if (ftruncate(fd, 0) != 0) {
                hifs_err("hive_segmentation: failed to truncate outbound index after ack for seg=%llu: %s",
                         (unsigned long long)seg_id,
                         strerror(errno));
            } else {
                if (write_idx > 0) {
                    if (lseek(fd, 0, SEEK_SET) < 0 ||
                        hive_seg_write_all(fd,
                                           g_seg_cache.outbound_idx.entries,
                                           write_idx * sizeof(struct hive_seg_index_disk_rec)) != 0) {
                        hifs_err("hive_segmentation: failed to rewrite outbound index after ack for seg=%llu: %s",
                                 (unsigned long long)seg_id,
                                 strerror(errno));
                    }
                }
            }
        }
    }

    pthread_mutex_unlock(&g_seg_cache.mu);
}

static void hive_seg_log_remote_fragment_acked(uint64_t seg_id,
                                               uint32_t frag_idx,
                                               uint32_t node_id)
{
    char txn_uuid[HIVE_SEG_UUID_TEXT_LEN];
    char stripe_uuid[HIVE_SEG_UUID_TEXT_LEN];
    if (!hive_seg_seg_txn_uuid_fill(seg_id, txn_uuid) ||
        !hive_seg_seg_stripe_uuid_fill(seg_id, stripe_uuid))
        return;

    struct hifs_wbl_fragment_event_rec rec = {
        .frag_idx = frag_idx,
        .node_id = node_id,
    };
    memcpy(rec.txn_id, txn_uuid, sizeof(rec.txn_id));
    memcpy(rec.stripe_id, stripe_uuid, sizeof(rec.stripe_id));
    if (hifs_wbl_mark_fragment_acked(&g_hive_guard_wbl_ctx, &rec) != 0) {
        hifs_err("hive_segmentation: unable to append WBL fragment acked record for seg=%llu frag=%u node=%u: %s",
                 (unsigned long long)seg_id,
                 frag_idx,
                 node_id,
                 strerror(errno));
    }
}

static inline void hive_seg_ack_remote_fragment(uint32_t node_id,
                                                uint32_t frag_idx,
                                                uint64_t seg_id)
{
    if (node_id == 0 || seg_id == 0)
        return;

    hive_seg_cache_drop_remote_fragment(node_id, seg_id);
    hive_seg_log_remote_fragment_acked(seg_id, frag_idx, node_id);
}

static bool hive_seg_index_mem_append(struct hive_seg_index_mem *mem,
                                      const struct hive_seg_index_disk_rec *rec);
static int hive_seg_write_all(int fd, const void *buf, size_t len);
static int hive_seg_pwrite_all(int fd,
                               const void *buf,
                               size_t len,
                               off_t offset);

static void hive_seg_outbound_path(char *out,
                                   size_t out_sz,
                                   uint32_t node_id,
                                   uint64_t seg_id)
{
    if (!out || out_sz == 0)
        return;
    snprintf(out,
             out_sz,
             HIVE_GUARD_SEGMT_PROC_SEG "/node-%u-seg-%llu.seg",
             node_id,
             (unsigned long long)seg_id);
}

static int hive_seg_read_all(int fd, void *buf, size_t len)
{
    uint8_t *p = buf;
    size_t read_bytes = 0;
    while (read_bytes < len) {
        ssize_t rc = read(fd, p + read_bytes, len - read_bytes);
        if (rc < 0) {
            if (errno == EINTR)
                continue;
            return -errno;
        }
        if (rc == 0)
            return -EIO;
        read_bytes += (size_t)rc;
    }
    return 0;
}

static int hive_seg_persist_outbound_segment(uint32_t node_id,
                                             uint64_t seg_id)
{
    if (node_id == 0 || seg_id == 0)
        return -EINVAL;

    char path[PATH_MAX];
    hive_seg_outbound_path(path, sizeof(path), node_id, seg_id);

    int fd = open(path,
                  O_RDONLY
#ifdef O_CLOEXEC
                  | O_CLOEXEC
#endif
    );
    if (fd < 0)
        return -errno;

    struct stat st;
    if (fstat(fd, &st) != 0) {
        int err = -errno;
        close(fd);
        return err;
    }
    if (st.st_size <= 0) {
        close(fd);
        return -ENOENT;
    }
    if ((uint64_t)st.st_size > SIZE_MAX) {
        close(fd);
        return -EFBIG;
    }

    size_t seg_len = (size_t)st.st_size;
    uint8_t *buf = malloc(seg_len);
    if (!buf) {
        close(fd);
        return -ENOMEM;
    }

    int rc = hive_seg_read_all(fd, buf, seg_len);
    close(fd);
    if (rc != 0) {
        free(buf);
        return rc;
    }

    rc = hg_kv_put_estripe_chunk(seg_id, buf, seg_len);
    free(buf);
    if (rc != 0)
        return -EIO;
    return 0;
}

static int hive_seg_outbound_append(uint32_t node_id,
                                    uint64_t seg_id,
                                    const uint8_t *data,
                                    uint32_t len,
                                    uint64_t *out_offset,
                                    uint64_t *out_size)
{
    if (!data || len == 0)
        return -EINVAL;

    char path[PATH_MAX];
    hive_seg_outbound_path(path, sizeof(path), node_id, seg_id);

    pthread_mutex_lock(&g_seg_landing_io_mu);
    int fd = open(path,
                  O_RDWR | O_CREAT
#ifdef O_CLOEXEC
                  | O_CLOEXEC
#endif
                  ,
                  S_IRUSR | S_IWUSR | S_IRGRP);
    if (fd < 0) {
        int err = errno ? -errno : -EIO;
        pthread_mutex_unlock(&g_seg_landing_io_mu);
        return err;
    }

    off_t start = lseek(fd, 0, SEEK_END);
    if (start < 0) {
        int err = errno ? -errno : -EIO;
        close(fd);
        pthread_mutex_unlock(&g_seg_landing_io_mu);
        return err;
    }

    int rc = hive_seg_pwrite_all(fd, data, len, start);
    close(fd);
    pthread_mutex_unlock(&g_seg_landing_io_mu);

    if (rc != 0)
        return rc;

    if (out_offset)
        *out_offset = (uint64_t)start;
    if (out_size)
        *out_size = (uint64_t)start + (uint64_t)len;
    return 0;
}

static int hive_seg_cache_record_remote_fragment(uint32_t node_id,
                                                 uint64_t seg_id,
                                                 uint64_t offset,
                                                 uint32_t len,
                                                 bool appendable)
{
    struct hive_seg_index_disk_rec rec = {
        .seg_id = seg_id,
        .volume_id = 0,
        .block_no = 0,
        .txn_id = seg_id,
        .generation = appendable ? 0u : 1u,
        .state = appendable ? HIFS_STRIPE_ACKED_PARTIAL : HIFS_STRIPE_COMMITTED,
        .offset = offset,
        .length = len,
        .reserved = node_id,
    };

    pthread_mutex_lock(&g_seg_cache.mu);
    if (!g_seg_cache.ready) {
        pthread_mutex_unlock(&g_seg_cache.mu);
        return -ESHUTDOWN;
    }

    if (!hive_seg_index_mem_append(&g_seg_cache.outbound_idx, &rec)) {
        pthread_mutex_unlock(&g_seg_cache.mu);
        return -ENOMEM;
    }

    int fd = g_seg_cache.outbound_idx_fd;
    int rc = 0;
    if (fd >= 0)
        rc = hive_seg_write_all(fd, &rec, sizeof(rec));

    pthread_mutex_unlock(&g_seg_cache.mu);
    return rc;
}

static void hive_seg_log_segment_received_audit(uint32_t src_node_id,
                                                uint64_t seg_id,
                                                uint64_t offset,
                                                uint32_t len,
                                                bool appendable)
{
    struct RaftCmd cmd = {0};
    cmd.op_type = HG_OP_PUT_CLUSTER_AUDIT;
    struct RaftClusterAudit *audit = &cmd.u.cluster_audit;
    audit->cluster_id = hbc.cluster_id;
    audit->node_id = storage_node_id;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    audit->timestamp_ns = (uint64_t)ts.tv_sec * 1000000000ull +
                          (uint64_t)ts.tv_nsec;
    snprintf(audit->message,
             sizeof(audit->message),
             "SEGMENT_RECIEVED_ON_TARGET seg=%llu src=%u off=%llu len=%u appendable=%s",
             (unsigned long long)seg_id,
             src_node_id,
             (unsigned long long)offset,
             len,
             appendable ? "yes" : "no");
    int rc = hg_raft_submit_cmd_sync(&cmd, NULL);
    if (rc != 0) {
        int err = rc < 0 ? -rc : rc;
        hifs_err("hive_segmentation: failed to log segment received event for seg=%llu: %s",
                 (unsigned long long)seg_id,
                 strerror(err));
    }
}

static void hive_seg_log_remote_fragment_received(uint64_t seg_id,
                                                  uint32_t frag_idx)
{
    char txn_uuid[HIVE_SEG_UUID_TEXT_LEN];
    char stripe_uuid[HIVE_SEG_UUID_TEXT_LEN];
    if (!hive_seg_seg_txn_uuid_fill(seg_id, txn_uuid) ||
        !hive_seg_seg_stripe_uuid_fill(seg_id, stripe_uuid))
        return;

    struct hifs_wbl_fragment_event_rec rec = {
        .frag_idx = frag_idx,
        .node_id = storage_node_id,
    };
    memcpy(rec.txn_id, txn_uuid, sizeof(rec.txn_id));
    memcpy(rec.stripe_id, stripe_uuid, sizeof(rec.stripe_id));
    if (hifs_wbl_mark_fragment_received(&g_hive_guard_wbl_ctx, &rec) != 0) {
        hifs_err("hive_segmentation: unable to append WBL fragment received record for seg=%llu frag=%u: %s",
                 (unsigned long long)seg_id,
                 frag_idx,
                 strerror(errno));
    }
}

static int hive_seg_process_remote_fragment(uint32_t src_node_id,
                                            uint32_t shard_id,
                                            uint64_t seg_id,
                                            const uint8_t *data,
                                            uint32_t len,
                                            uint64_t *out_block_offset)
{
    if (!data || len == 0)
        return -EINVAL;
    if (hive_seg_outbound_segment_closed(src_node_id, seg_id))
        return -EALREADY;

    uint64_t offset = 0;
    uint64_t seg_size = 0;
    int rc = hive_seg_outbound_append(src_node_id,
                                      seg_id,
                                      data,
                                      len,
                                      &offset,
                                      &seg_size);
    if (rc != 0)
        return rc;

    if (out_block_offset)
        *out_block_offset = offset;

    bool appendable = seg_size < (uint64_t)HIFS_SEGMENT_SIZE;

    rc = hive_seg_cache_record_remote_fragment(src_node_id,
                                               seg_id,
                                               offset,
                                               len,
                                               appendable);
    if (rc != 0)
        return rc;

    hive_seg_log_segment_received_audit(src_node_id,
                                        seg_id,
                                        offset,
                                        len,
                                        appendable);
    hive_seg_log_remote_fragment_received(seg_id, shard_id);
    hive_seg_ack_remote_fragment(src_node_id, shard_id, seg_id);
    if (!appendable) {
        hive_seg_log_stripe_committed_record(seg_id, HIVE_SEG_FULL_ACK_BITMAP);
        hive_seg_stripe_commit(src_node_id,
                               seg_id,
                               HIVE_SEG_FULL_ACK_BITMAP);
    }
    return 0;
}

static int hive_seg_sn_recv_cb(uint32_t src_node_id,
                               uint32_t shard_id,
                               uint64_t estripe_id,
                               const uint8_t *data,
                               uint32_t len,
                               uint64_t *out_block_offset)
{
    return hive_seg_process_remote_fragment(src_node_id,
                                            shard_id,
                                            estripe_id,
                                            data,
                                            len,
                                            out_block_offset);
}

static inline struct hive_seg_index_disk_rec *
hive_seg_cache_find_outbound_rec_locked(uint64_t seg_id,
                                        uint64_t volume_id,
                                        uint64_t block_no,
                                        uint32_t node_id,
                                        uint64_t offset)
{
    if (!g_seg_cache.outbound_idx.entries)
        return NULL;
    for (size_t idx = g_seg_cache.outbound_idx.count; idx > 0; --idx) {
        struct hive_seg_index_disk_rec *rec =
            &g_seg_cache.outbound_idx.entries[idx - 1];
        if (rec->seg_id == seg_id &&
            rec->volume_id == volume_id &&
            rec->block_no == block_no &&
            rec->reserved == node_id &&
            rec->offset == offset) {
            return rec;
        }
    }
    return NULL;
}

static int hive_seg_cache_lock_outbound(uint64_t txn_id,
                                        uint32_t generation,
                                        const struct hive_seg_block_input *blk,
                                        uint32_t node_id,
                                        const struct hifs_seg_loc *loc)
{
    if (!blk || !loc || node_id == 0)
        return -EINVAL;

    pthread_mutex_lock(&g_seg_cache.mu);
    if (!g_seg_cache.ready) {
        pthread_mutex_unlock(&g_seg_cache.mu);
        return -ESHUTDOWN;
    }

    struct hive_seg_index_disk_rec *rec =
        hive_seg_cache_find_outbound_rec_locked(loc->seg_id,
                                                blk->volume_id,
                                                blk->block_no,
                                                node_id,
                                                loc->offset);
    if (!rec) {
        pthread_mutex_unlock(&g_seg_cache.mu);
        return -ENOENT;
    }

    rec->txn_id = txn_id;
    rec->generation = generation;
    rec->state = HIFS_STRIPE_OUTBOUND_QUEUED;
    rec->length = loc->length;

    int fd = g_seg_cache.outbound_idx_fd;
    int rc = 0;
    if (fd >= 0) {
        size_t idx = (size_t)(rec - g_seg_cache.outbound_idx.entries);
        rc = hive_seg_pwrite_all(fd,
                                 rec,
                                 sizeof(*rec),
                                 (off_t)idx * (off_t)sizeof(*rec));
    }

    pthread_mutex_unlock(&g_seg_cache.mu);
    return rc;
}

static inline void hive_seg_log_outbound_queued(const struct hive_seg_block_input *blk,
                                                uint64_t txn_id,
                                                uint32_t node_id,
                                                const struct hifs_seg_loc *loc)
{
    if (!blk || !loc || node_id == 0)
        return;

    char txn_uuid[HIVE_SEG_UUID_TEXT_LEN];
    char stripe_uuid[HIVE_SEG_UUID_TEXT_LEN];
    if (!hive_seg_txn_uuid_fill(txn_id, txn_uuid) ||
        !hive_seg_stripe_uuid_fill(txn_id, stripe_uuid))
        return;

    struct hifs_wbl_outbound_queued_rec rec = {
        .node_id = node_id,
        .frag_loc = *loc,
    };
    memcpy(rec.txn_id, txn_uuid, sizeof(rec.txn_id));
    memcpy(rec.stripe_id, stripe_uuid, sizeof(rec.stripe_id));
    if (hifs_wbl_mark_outbound_queued(&g_hive_guard_wbl_ctx, &rec) != 0) {
        hifs_err("hive_segmentation: unable to append WBL outbound queued record for %llu:%llu node=%u: %s",
                 (unsigned long long)blk->volume_id,
                 (unsigned long long)blk->block_no,
                 node_id,
                 strerror(errno));
    }
}

static int hive_seg_mark_outbound_queued(uint64_t txn_id,
                                         uint32_t generation,
                                         const struct hive_seg_block_input *blk,
                                         uint32_t node_id,
                                         const struct hifs_seg_loc *loc)
{
    int rc = hive_seg_cache_lock_outbound(txn_id, generation, blk, node_id, loc);
    if (rc != 0)
        return rc;
    hive_seg_log_outbound_queued(blk, txn_id, node_id, loc);
    return 0;
}

static int hive_seg_cache_mark_fragment_sent(uint64_t txn_id,
                                             uint32_t generation,
                                             const struct hive_seg_block_input *blk,
                                             uint32_t node_id,
                                             const struct hifs_seg_loc *loc)
{
    if (!blk || !loc || node_id == 0)
        return -EINVAL;

    pthread_mutex_lock(&g_seg_cache.mu);
    if (!g_seg_cache.ready) {
        pthread_mutex_unlock(&g_seg_cache.mu);
        return -ESHUTDOWN;
    }

    struct hive_seg_index_disk_rec *rec =
        hive_seg_cache_find_outbound_rec_locked(loc->seg_id,
                                                blk->volume_id,
                                                blk->block_no,
                                                node_id,
                                                loc->offset);
    if (!rec) {
        pthread_mutex_unlock(&g_seg_cache.mu);
        return -ENOENT;
    }

    rec->txn_id = txn_id;
    rec->generation = generation;
    rec->state = HIFS_STRIPE_SENT_PARTIAL;
    rec->length = loc->length;

    int rc = 0;
    if (g_seg_cache.outbound_idx_fd >= 0) {
        size_t idx = (size_t)(rec - g_seg_cache.outbound_idx.entries);
        rc = hive_seg_pwrite_all(g_seg_cache.outbound_idx_fd,
                                 rec,
                                 sizeof(*rec),
                                 (off_t)idx * (off_t)sizeof(*rec));
    }
    pthread_mutex_unlock(&g_seg_cache.mu);
    return rc;
}

static inline void hive_seg_log_fragment_sent(const struct hive_seg_block_input *blk,
                                              uint64_t txn_id,
                                              uint32_t frag_idx,
                                              uint32_t node_id)
{
    if (!blk || node_id == 0)
        return;

    char txn_uuid[HIVE_SEG_UUID_TEXT_LEN];
    char stripe_uuid[HIVE_SEG_UUID_TEXT_LEN];
    if (!hive_seg_txn_uuid_fill(txn_id, txn_uuid) ||
        !hive_seg_stripe_uuid_fill(txn_id, stripe_uuid))
        return;

    struct hifs_wbl_fragment_event_rec rec = {
        .frag_idx = frag_idx,
        .node_id = node_id,
    };
    memcpy(rec.txn_id, txn_uuid, sizeof(rec.txn_id));
    memcpy(rec.stripe_id, stripe_uuid, sizeof(rec.stripe_id));

    if (hifs_wbl_mark_fragment_sent(&g_hive_guard_wbl_ctx, &rec) != 0) {
        hifs_err("hive_segmentation: unable to append WBL fragment sent record for %llu:%llu frag=%u node=%u: %s",
                 (unsigned long long)blk->volume_id,
                 (unsigned long long)blk->block_no,
                 frag_idx,
                 node_id,
                 strerror(errno));
    }
}

static int hive_seg_send_fragment(uint64_t txn_id,
                                  uint32_t generation,
                                  const struct hive_seg_block_input *blk,
                                  uint32_t frag_idx,
                                  uint32_t node_id,
                                  const struct hifs_seg_loc *loc,
                                  const uint8_t *data)
{
    if (!blk || !loc || !data || loc->length == 0 || node_id == 0)
        return -EINVAL;

    uint32_t shard_id = frag_idx;
    uint64_t estripe_id = loc->seg_id ? loc->seg_id : blk->block_no;
    int rc = hifs_sn_tcp_send(node_id,
                              shard_id,
                              estripe_id,
                              data,
                              loc->length);
    if (rc != 0) {
        int err = errno ? errno : EIO;
        hifs_err("hive_segmentation: failed to send fragment %u for %llu:%llu node=%u seg=%llu: %s",
                 frag_idx,
                 (unsigned long long)blk->volume_id,
                 (unsigned long long)blk->block_no,
                 node_id,
                 (unsigned long long)loc->seg_id,
                 strerror(err));
        return -err;
    }

    rc = hive_seg_cache_mark_fragment_sent(txn_id,
                                           generation,
                                           blk,
                                           node_id,
                                           loc);
    if (rc != 0)
        return rc;

    hive_seg_log_fragment_sent(blk, txn_id, frag_idx, node_id);
    return 0;
}
static inline uint64_t hive_seg_now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static inline size_t hive_seg_vappend(char *buf,
                                      size_t buf_sz,
                                      size_t pos,
                                      const char *fmt,
                                      va_list ap)
{
    if (!buf || pos >= buf_sz)
        return buf_sz;
    int rc = vsnprintf(buf + pos, buf_sz - pos, fmt, ap);
    if (rc < 0)
        return buf_sz;
    size_t written = (size_t)rc;
    if (written >= buf_sz - pos)
        return buf_sz;
    return pos + written;
}

static inline size_t hive_seg_append(char *buf,
                                     size_t buf_sz,
                                     size_t pos,
                                     const char *fmt,
                                     ...)
{
    if (!buf || pos >= buf_sz)
        return buf_sz;
    va_list ap;
    va_start(ap, fmt);
    size_t new_pos = hive_seg_vappend(buf, buf_sz, pos, fmt, ap);
    va_end(ap);
    return new_pos;
}

static inline void hive_seg_format_target_summary(char *buf,
                                                  size_t buf_sz,
                                                  const struct hifs_fragment_target *targets,
                                                  size_t count)
{
    if (!buf || buf_sz == 0) {
        return;
    }
    size_t pos = 0;
    buf[0] = '\0';
    pos = hive_seg_append(buf, buf_sz, pos, "targets=[");
    if (!targets || count == 0) {
        hive_seg_append(buf, buf_sz, pos, "]");
        return;
    }
    for (size_t i = 0; i < count && pos < buf_sz; ++i) {
        pos = hive_seg_append(buf,
                              buf_sz,
                              pos,
                              "%s%u:%u",
                              (i == 0) ? "" : ",",
                              targets[i].frag_idx,
                              targets[i].node_id);
    }
    hive_seg_append(buf, buf_sz, pos, "]");
}

static inline void hive_seg_format_frag_summary(char *buf,
                                                size_t buf_sz,
                                                const struct hifs_seg_loc *locs,
                                                size_t count)
{
    if (!buf || buf_sz == 0) {
        return;
    }
    size_t pos = 0;
    buf[0] = '\0';
    pos = hive_seg_append(buf, buf_sz, pos, "frags=[");
    if (!locs || count == 0) {
        hive_seg_append(buf, buf_sz, pos, "]");
        return;
    }
    for (size_t i = 0; i < count && pos < buf_sz; ++i) {
        pos = hive_seg_append(buf,
                              buf_sz,
                              pos,
                              "%s%llu:%u+%u",
                              (i == 0) ? "" : ",",
                              (unsigned long long)locs[i].seg_id,
                              locs[i].offset,
                              locs[i].length);
    }
    hive_seg_append(buf, buf_sz, pos, "]");
}

static inline void hive_seg_format_block_durable(char *buf,
                                                 size_t buf_sz,
                                                 const struct hive_seg_block_input *blk,
                                                 size_t frag_count,
                                                 size_t target_count)
{
    if (!buf || buf_sz == 0) {
        return;
    }
    if (!blk) {
        buf[0] = '\0';
        return;
    }
    size_t pos = 0;
    buf[0] = '\0';
    pos = hive_seg_append(buf,
                          buf_sz,
                          pos,
                          "placement_epoch=%u len=%u",
                          blk->placement_epoch,
                          blk->len);
    hive_seg_append(buf,
                    buf_sz,
                    pos,
                    " frags=%zu targets=%zu",
                    frag_count,
                    target_count);
}

static void hive_seg_log_raft_event(const char *event,
                                    const struct hive_seg_block_input *blk,
                                    uint64_t txn_id,
                                    const char *detail)
{
    if (!blk)
        return;
    struct RaftCmd cmd = {0};
    cmd.op_type = HG_OP_PUT_CLUSTER_AUDIT;
    struct RaftClusterAudit *audit = &cmd.u.cluster_audit;
    audit->cluster_id = hbc.cluster_id;
    audit->node_id = storage_node_id;
    audit->severity = 0;
    audit->reserved = 0;
    audit->timestamp_ns = hive_seg_now_ns();
    if (!event)
        event = "seg-stage";
    snprintf(audit->message,
             sizeof(audit->message),
             "%s vol=%llu block=%llu txn=%llu %s",
             event,
             (unsigned long long)blk->volume_id,
             (unsigned long long)blk->block_no,
             (unsigned long long)txn_id,
             detail ? detail : "");
    int rc = hg_raft_submit_cmd_sync(&cmd, NULL);
    if (rc != 0) {
        int err = rc < 0 ? -rc : rc;
        hifs_err("hive_segmentation: failed to append raft audit (%s) for %llu:%llu: %s",
                 event,
                 (unsigned long long)blk->volume_id,
                 (unsigned long long)blk->block_no,
                 strerror(err));
    }
}

static void hive_seg_log_wbl_placement_assigned(uint64_t txn_id,
                                                const struct hive_seg_block_input *blk,
                                                const struct hifs_fragment_target *targets,
                                                size_t target_count)
{
    if (!blk || !targets || target_count == 0)
        return;

    char txn_uuid[HIVE_SEG_UUID_TEXT_LEN];
    char stripe_uuid[HIVE_SEG_UUID_TEXT_LEN];
    if (!hive_seg_txn_uuid_fill(txn_id, txn_uuid) ||
        !hive_seg_stripe_uuid_fill(txn_id, stripe_uuid))
        return;

    struct hifs_wbl_placement_rec rec = {0};
    memcpy(rec.txn_id, txn_uuid, sizeof(rec.txn_id));
    memcpy(rec.stripe_id, stripe_uuid, sizeof(rec.stripe_id));

    if (target_count > HIFS_EC_TOTAL_SRIPES)
        target_count = HIFS_EC_TOTAL_SRIPES;
    memcpy(rec.targets,
           targets,
           target_count * sizeof(struct hifs_fragment_target));
    for (size_t i = target_count; i < HIFS_EC_TOTAL_SRIPES; ++i) {
        rec.targets[i].frag_idx = 0;
        rec.targets[i].node_id = 0;
    }

    if (hifs_wbl_mark_placement_assigned(&g_hive_guard_wbl_ctx, &rec) != 0) {
        hifs_err("hive_segmentation: unable to append WBL placement record for %llu:%llu: %s",
                 (unsigned long long)blk->volume_id,
                 (unsigned long long)blk->block_no,
                 strerror(errno));
    }
}

static void hive_seg_publish_placement_state(const struct hive_seg_block_input *blk,
                                             uint64_t txn_id,
                                             const struct hifs_fragment_target *targets,
                                             size_t target_count,
                                             const struct hifs_seg_loc *frag_locs,
                                             size_t frag_count)
{
    if (!blk || !targets || target_count == 0)
        return;

    char detail[HG_CLUSTER_AUDIT_MSG_MAX];

    hive_seg_format_target_summary(detail, sizeof(detail), targets, target_count);
    hive_seg_log_raft_event("placement-assigned", blk, txn_id, detail);

    hive_seg_format_frag_summary(detail, sizeof(detail), frag_locs, frag_count);
    hive_seg_log_raft_event("stripe-prepared", blk, txn_id, detail);

    hive_seg_format_block_durable(detail,
                                  sizeof(detail),
                                  blk,
                                  frag_count,
                                  target_count);
    hive_seg_log_raft_event("block-durable", blk, txn_id, detail);

    hive_seg_log_wbl_placement_assigned(txn_id, blk, targets, target_count);
}
static inline uint64_t hive_seg_resolve_txn(uint64_t hint)
{
    if (hint != 0)
        return hint;
    return atomic_fetch_add_explicit(&g_seg_txn_seq, 1, memory_order_relaxed);
}

static inline void hive_seg_release_block(struct hive_seg_block_input *blk)
{
    if (!blk)
        return;
    if (blk->release_cb) {
        blk->release_cb(blk->release_ctx, blk->data, blk->len);
    } else if (blk->data) {
        free(blk->data);
    }
    blk->data = NULL;
}

static void hive_seg_task_cleanup(struct hive_seg_task *task)
{
    if (!task)
        return;
    if (task->type == HIVE_SEG_TASK_SINGLE) {
        hive_seg_release_block(&task->u.single);
    } else if (task->type == HIVE_SEG_TASK_CONTIG && task->u.contig.blocks) {
        for (uint32_t i = 0; i < task->u.contig.count; ++i)
            hive_seg_release_block(&task->u.contig.blocks[i]);
        free(task->u.contig.blocks);
        task->u.contig.blocks = NULL;
        task->u.contig.count = 0;
    }
}

static struct {
    pthread_mutex_t mu;
    uint64_t next_seg_id;
    struct hive_seg_stage_cursor cursor;
} g_seg_stage_allocator = {
    .mu = PTHREAD_MUTEX_INITIALIZER,
    .next_seg_id = 1,
    .cursor = {
        .seg_id = 0,
        .bytes_used = 0,
    },
};

struct hive_seg_node_stage {
    uint32_t node_id;
    struct hive_seg_stage_cursor cursor;
};

static struct hive_seg_node_stage *g_seg_node_stages;
static size_t g_seg_node_stage_count;
static size_t g_seg_node_stage_capacity;

static inline uint64_t hive_seg_stage_next_id_locked(void)
{
    return g_seg_stage_allocator.next_seg_id++;
}

static inline uint32_t hive_seg_align_len(uint32_t len)
{
    if (len == 0)
        return 0;
    if (len >= HIFS_SEGMENT_SIZE)
        return HIFS_SEGMENT_SIZE;
    uint32_t rem = len % HIFS_DEFAULT_BLOCK_SIZE;
    if (rem == 0)
        return len;
    return len + (HIFS_DEFAULT_BLOCK_SIZE - rem);
}

static inline void hive_seg_stage_rotate_locked(void)
{
    g_seg_stage_allocator.cursor.seg_id = hive_seg_stage_next_id_locked();
    g_seg_stage_allocator.cursor.bytes_used = 0;
}

static inline void hive_seg_stage_ensure_capacity_locked(uint32_t need)
{
    if (g_seg_stage_allocator.cursor.seg_id == 0 ||
        (need > 0 &&
         g_seg_stage_allocator.cursor.bytes_used + need > HIFS_SEGMENT_SIZE)) {
        hive_seg_stage_rotate_locked();
    }
}

static inline void hive_seg_mark_landing(struct hive_seg_block_input *blk,
                                         uint64_t seg_id,
                                         uint32_t offset)
{
    blk->has_landing_loc = true;
    blk->landing_loc.seg_id = seg_id;
    blk->landing_loc.offset = offset;
    blk->landing_loc.length = blk->len;
}

static inline void hive_seg_index_mem_reset(struct hive_seg_index_mem *mem)
{
    if (!mem)
        return;
    free(mem->entries);
    mem->entries = NULL;
    mem->count = 0;
    mem->capacity = 0;
}

static bool hive_seg_index_mem_append(struct hive_seg_index_mem *mem,
                                      const struct hive_seg_index_disk_rec *rec)
{
    if (!mem || !rec)
        return false;
    if (mem->count == mem->capacity) {
        size_t new_cap = mem->capacity ? (mem->capacity * 2u) : 128u;
        void *tmp = realloc(mem->entries, new_cap * sizeof(*mem->entries));
        if (!tmp)
            return false;
        mem->entries = tmp;
        mem->capacity = new_cap;
    }
    mem->entries[mem->count++] = *rec;
    return true;
}

static inline void hive_seg_cache_close_fd(int *fd)
{
    if (fd && *fd >= 0) {
        close(*fd);
        *fd = -1;
    }
}

static int hive_seg_ensure_directory(const char *path)
{
    if (!path || !*path)
        return -EINVAL;
    if (mkdir(path, 0755) == 0)
        return 0;
    if (errno == EEXIST)
        return 0;
    return -errno;
}

static int hive_seg_open_index(const char *path)
{
    int fd = open(path,
                  O_RDWR | O_CREAT | O_APPEND
#ifdef O_CLOEXEC
                  | O_CLOEXEC
#endif
                  ,
                  S_IRUSR | S_IWUSR | S_IRGRP);
    if (fd < 0)
        return -errno;
    return fd;
}

static int hive_seg_cache_load_locked(int fd, struct hive_seg_index_mem *mem)
{
    if (fd < 0 || !mem)
        return 0;
    if (lseek(fd, 0, SEEK_SET) < 0)
        return -errno;
    struct hive_seg_index_disk_rec rec;
    ssize_t rd;
    while ((rd = read(fd, &rec, sizeof(rec))) == (ssize_t)sizeof(rec)) {
        if (!hive_seg_index_mem_append(mem, &rec))
            return -ENOMEM;
    }
    if (rd < 0)
        return -errno;
    if (lseek(fd, 0, SEEK_END) < 0)
        return -errno;
    return 0;
}

static int hive_seg_cache_init(void)
{
    pthread_mutex_lock(&g_seg_cache.mu);
    if (g_seg_cache.ready) {
        pthread_mutex_unlock(&g_seg_cache.mu);
        return 0;
    }

    int rc = 0;
    const char *dirs[] = {
        HIVE_DATA_DIR,
        HIVE_GUARD_SEGMT_DIR,
        HIVE_GUARD_SEGMT_RECV_NEW,
        HIVE_GUARD_SEGMT_PROC_SEG,
        HIVE_GUARD_SEGMT_READ_RETURN,
    };
    for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); ++i) {
        rc = hive_seg_ensure_directory(dirs[i]);
        if (rc != 0)
            goto fail;
    }

    g_seg_cache.landing_idx_fd = hive_seg_open_index(HIVE_GUARD_SEGMT_RECV_NEW_INDX);
    if (g_seg_cache.landing_idx_fd < 0) {
        rc = g_seg_cache.landing_idx_fd;
        goto fail;
    }
    g_seg_cache.outbound_idx_fd = hive_seg_open_index(HIVE_GUARD_SEGMT_PROC_SEG_INDX);
    if (g_seg_cache.outbound_idx_fd < 0) {
        rc = g_seg_cache.outbound_idx_fd;
        goto fail;
    }
    g_seg_cache.read_return_idx_fd = hive_seg_open_index(HIVE_GUARD_SEGMT_RRET_INDX);
    if (g_seg_cache.read_return_idx_fd < 0) {
        rc = g_seg_cache.read_return_idx_fd;
        goto fail;
    }

    rc = hive_seg_cache_load_locked(g_seg_cache.landing_idx_fd,
                                    &g_seg_cache.landing_idx);
    if (rc != 0)
        goto fail;

    rc = hive_seg_cache_load_locked(g_seg_cache.outbound_idx_fd,
                                    &g_seg_cache.outbound_idx);
    if (rc != 0)
        goto fail;

    rc = hive_seg_cache_load_locked(g_seg_cache.read_return_idx_fd,
                                    &g_seg_cache.read_return_idx);
    if (rc != 0)
        goto fail;

    g_seg_cache.ready = true;
    pthread_mutex_unlock(&g_seg_cache.mu);
    return 0;

fail:
    hive_seg_cache_close_fd(&g_seg_cache.landing_idx_fd);
    hive_seg_cache_close_fd(&g_seg_cache.outbound_idx_fd);
    hive_seg_cache_close_fd(&g_seg_cache.read_return_idx_fd);
    hive_seg_index_mem_reset(&g_seg_cache.landing_idx);
    hive_seg_index_mem_reset(&g_seg_cache.outbound_idx);
    hive_seg_index_mem_reset(&g_seg_cache.read_return_idx);
    pthread_mutex_unlock(&g_seg_cache.mu);
    return rc;
}

static void hive_seg_cache_shutdown(void)
{
    pthread_mutex_lock(&g_seg_cache.mu);
    g_seg_cache.ready = false;
    hive_seg_cache_close_fd(&g_seg_cache.landing_idx_fd);
    hive_seg_cache_close_fd(&g_seg_cache.read_return_idx_fd);
    hive_seg_cache_close_fd(&g_seg_cache.outbound_idx_fd);
    hive_seg_index_mem_reset(&g_seg_cache.landing_idx);
    hive_seg_index_mem_reset(&g_seg_cache.outbound_idx);
    hive_seg_index_mem_reset(&g_seg_cache.read_return_idx);
    pthread_mutex_unlock(&g_seg_cache.mu);
}

static inline int hive_seg_write_all(int fd, const void *buf, size_t len)
{
    const uint8_t *p = buf;
    size_t written = 0;
    while (written < len) {
        ssize_t rc = write(fd, p + written, len - written);
        if (rc < 0) {
            if (errno == EINTR)
                continue;
            return -errno;
        }
        written += (size_t)rc;
    }
    return 0;
}

static inline int hive_seg_pwrite_all(int fd,
                                      const void *buf,
                                      size_t len,
                                      off_t offset)
{
    const uint8_t *p = buf;
    size_t written = 0;
    while (written < len) {
        ssize_t rc = pwrite(fd, p + written, len - written, offset + (off_t)written);
        if (rc < 0) {
            if (errno == EINTR)
                continue;
            return -errno;
        }
        written += (size_t)rc;
    }
    return 0;
}

static void hive_seg_landing_path(const struct hive_seg_block_input *blk,
                                  char *out,
                                  size_t out_sz)
{
    if (!out || out_sz == 0)
        return;
    if (!blk || !blk->has_landing_loc) {
        snprintf(out, out_sz, HIVE_GUARD_SEGMT_RECV_NEW "/pending.seg");
        return;
    }
    snprintf(out,
             out_sz,
             HIVE_GUARD_SEGMT_RECV_NEW "/seg-%llu-%u.seg",
             (unsigned long long)blk->landing_loc.seg_id,
             blk->landing_loc.offset);
}

static bool hive_seg_encode_block(const struct hive_seg_block_input *blk,
                                  struct hifs_ec_stripe_set *out)
{
    if (!blk || !out)
        return false;
    enum hifs_hash_algorithm algo = blk->hash_algo;
    if (algo == HIFS_HASH_ALGO_NONE)
        algo = HIFS_HASH_ALGO_SHA256;
    const uint8_t *pre_hash = blk->has_hash ? blk->hash : NULL;
    return hifs_volume_block_ec_encode(blk->data, blk->len, algo, pre_hash, out);
}

static int hive_seg_write_landing_file(uint64_t txn_id,
                                       uint32_t generation,
                                       const struct hive_seg_block_input *blk,
                                       const struct hifs_ec_stripe_set *ec)
{
    if (!blk || !blk->data || blk->len == 0)
        return -EINVAL;

    char path[PATH_MAX];
    hive_seg_landing_path(blk, path, sizeof(path));

    struct hive_seg_block_file_hdr hdr = {
        .magic = HIVE_SEG_BLOCK_FILE_MAGIC,
        .version = HIVE_SEG_BLOCK_FILE_VERSION,
        .chunk_count = (uint16_t)(ec ? ec->chunk_count : 0u),
        .chunk_len = (uint32_t)(ec ? ec->chunk_len : 0u),
        .block_bytes = blk->len,
        .landing_length = blk->has_landing_loc ? blk->landing_loc.length : blk->len,
        .seg_id = blk->has_landing_loc ? blk->landing_loc.seg_id : 0,
        .volume_id = blk->volume_id,
        .block_no = blk->block_no,
        .txn_id = txn_id,
        .generation = generation,
        .landing_offset = blk->has_landing_loc ? blk->landing_loc.offset : 0u,
        .hash_algo = blk->hash_algo,
        .stripe_algo = blk->stripe_algo,
    };
    if (blk->has_hash)
        memcpy(hdr.hash, blk->hash, sizeof(hdr.hash));
    else
        memset(hdr.hash, 0, sizeof(hdr.hash));

    pthread_mutex_lock(&g_seg_landing_io_mu);
    int fd = open(path,
                  O_WRONLY | O_CREAT | O_TRUNC
#ifdef O_CLOEXEC
                  | O_CLOEXEC
#endif
                  ,
                  S_IRUSR | S_IWUSR | S_IRGRP);
    if (fd < 0) {
        int err = errno;
        pthread_mutex_unlock(&g_seg_landing_io_mu);
        return -err;
    }

    int rc = hive_seg_write_all(fd, &hdr, sizeof(hdr));
    if (rc == 0) {
        if (ec && ec->chunks && ec->chunk_count > 0) {
            off_t offset = (off_t)sizeof(hdr);
            for (size_t i = 0; i < ec->chunk_count && rc == 0; ++i) {
                rc = hive_seg_pwrite_all(fd,
                                         ec->chunks[i],
                                         ec->chunk_len,
                                         offset);
                offset += (off_t)ec->chunk_len;
            }
        } else {
            rc = hive_seg_pwrite_all(fd,
                                     blk->data,
                                     blk->len,
                                     (off_t)sizeof(hdr));
        }
    }

    close(fd);
    pthread_mutex_unlock(&g_seg_landing_io_mu);
    return rc;
}

static int hive_seg_cache_record_landing(uint64_t txn_id,
                                         uint32_t generation,
                                         const struct hive_seg_block_input *blk)
{
    if (!blk || !blk->has_landing_loc)
        return -EINVAL;

    struct hive_seg_index_disk_rec rec = {
        .seg_id = blk->landing_loc.seg_id,
        .volume_id = blk->volume_id,
        .block_no = blk->block_no,
        .txn_id = txn_id,
        .generation = generation,
        .state = HIFS_STRIPE_LANDING_ECCODED,
        .offset = blk->landing_loc.offset,
        .length = blk->landing_loc.length,
        .reserved = 0,
    };

    pthread_mutex_lock(&g_seg_cache.mu);
    if (!g_seg_cache.ready) {
        pthread_mutex_unlock(&g_seg_cache.mu);
        return -ESHUTDOWN;
    }
    if (!hive_seg_index_mem_append(&g_seg_cache.landing_idx, &rec)) {
        pthread_mutex_unlock(&g_seg_cache.mu);
        return -ENOMEM;
    }
    int fd = g_seg_cache.landing_idx_fd;
    int rc = 0;
    if (fd >= 0)
        rc = hive_seg_write_all(fd, &rec, sizeof(rec));
    pthread_mutex_unlock(&g_seg_cache.mu);
    return rc;
}

static inline void hive_seg_log_landing_eccoded(const struct hive_seg_block_input *blk,
                                                uint64_t txn_id)
{
    if (!blk)
        return;
    char txn_uuid[HIVE_SEG_UUID_TEXT_LEN];
    char stripe_uuid[HIVE_SEG_UUID_TEXT_LEN];
    if (!hive_seg_txn_uuid_fill(txn_id, txn_uuid) ||
        !hive_seg_stripe_uuid_fill(txn_id, stripe_uuid))
        return;

    struct hifs_wbl_landing_rec landing = {0};
    memcpy(landing.txn_id, txn_uuid, sizeof(landing.txn_id));
    memcpy(landing.stripe_id, stripe_uuid, sizeof(landing.stripe_id));
    if (blk->has_landing_loc)
        landing.landing_loc = blk->landing_loc;
    else {
        landing.landing_loc.seg_id = blk->block_no;
        landing.landing_loc.offset = 0;
        landing.landing_loc.length = blk->len;
    }
    if (hifs_wbl_mark_landing_eccoded(&g_hive_guard_wbl_ctx, &landing) != 0) {
        hifs_err("hive_segmentation: unable to append WBL landing event for %llu:%llu: %s",
                 (unsigned long long)blk->volume_id,
                 (unsigned long long)blk->block_no,
                 strerror(errno));
    }
}

static int hive_seg_stage_landing_eccoded(struct hive_seg_block_input *blk,
                                          uint64_t txn_id,
                                          uint32_t generation)
{
    if (!blk)
        return -EINVAL;
    if (!blk->has_landing_loc)
        return -EIO;

    int rc = hive_seg_cache_init();
    if (rc != 0)
        return rc;

    struct hifs_ec_stripe_set ec = {0};
    struct hifs_ec_stripe_set *ec_ptr = NULL;
    if (blk->data && blk->len > 0 && hive_seg_encode_block(blk, &ec))
        ec_ptr = &ec;

    if (!ec_ptr)
        return -EIO;

    rc = hive_seg_prepare_fragments(blk, txn_id, generation, ec_ptr);
    if (rc != 0)
        goto out;

    rc = hive_seg_write_landing_file(txn_id, generation, blk, ec_ptr);
    if (rc != 0)
        goto out;

    rc = hive_seg_cache_record_landing(txn_id, generation, blk);
    if (rc != 0)
        goto out;

    hive_seg_log_landing_eccoded(blk, txn_id);

out:
    if (ec_ptr)
        hifs_volume_block_ec_free(ec_ptr);
    return rc;
}

static inline void hive_seg_stage_block_locked(struct hive_seg_block_input *blk)
{
    if (!blk || blk->len == 0 || blk->has_landing_loc)
        return;
    struct hifs_seg_loc loc;
    if (hive_seg_writable_alloc(blk->len, &loc)) {
        blk->landing_loc = loc;
        blk->has_landing_loc = true;
        return;
    }

    uint32_t aligned = hive_seg_align_len(blk->len);
    if (aligned == 0)
        aligned = blk->len;
    hive_seg_stage_ensure_capacity_locked(aligned);
    hive_seg_mark_landing(blk,
                          g_seg_stage_allocator.cursor.seg_id,
                          g_seg_stage_allocator.cursor.bytes_used);
    g_seg_stage_allocator.cursor.bytes_used += aligned;
}

static struct hive_seg_node_stage *
hive_seg_node_stage_acquire_locked(uint32_t node_id)
{
    for (size_t i = 0; i < g_seg_node_stage_count; ++i) {
        if (g_seg_node_stages[i].node_id == node_id)
            return &g_seg_node_stages[i];
    }

    size_t needed = g_seg_node_stage_count + 1;
    if (needed > g_seg_node_stage_capacity) {
        size_t new_cap = g_seg_node_stage_capacity ? (g_seg_node_stage_capacity * 2u) : 8u;
        void *tmp = realloc(g_seg_node_stages,
                            new_cap * sizeof(*g_seg_node_stages));
        if (!tmp)
            return NULL;
        g_seg_node_stages = tmp;
        g_seg_node_stage_capacity = new_cap;
    }

    struct hive_seg_node_stage *stage = &g_seg_node_stages[g_seg_node_stage_count++];
    stage->node_id = node_id;
    stage->cursor.seg_id = 0;
    stage->cursor.bytes_used = 0;
    return stage;
}

static int hive_seg_stage_fragment_locked(uint32_t node_id,
                                          uint32_t len,
                                          struct hifs_seg_loc *loc)
{
    if (!loc || len == 0)
        return -EINVAL;
    struct hive_seg_node_stage *stage = hive_seg_node_stage_acquire_locked(node_id);
    if (!stage)
        return -ENOMEM;
    uint32_t aligned = hive_seg_align_len(len);
    if (aligned == 0)
        aligned = len;
    if (stage->cursor.seg_id == 0 ||
        stage->cursor.bytes_used + aligned > HIFS_SEGMENT_SIZE) {
        stage->cursor.seg_id = hive_seg_stage_next_id_locked();
        stage->cursor.bytes_used = 0;
    }
    loc->seg_id = stage->cursor.seg_id;
    loc->offset = stage->cursor.bytes_used;
    loc->length = len;
    stage->cursor.bytes_used += aligned;
    return 0;
}

static void hive_seg_fragment_path(char *out,
                                   size_t out_sz,
                                   uint32_t node_id,
                                   uint64_t seg_id)
{
    if (!out || out_sz == 0)
        return;
    snprintf(out,
             out_sz,
             HIVE_GUARD_SEGMT_RECV_NEW "/node-%u-seg-%llu.seg",
             node_id,
             (unsigned long long)seg_id);
}

static int hive_seg_write_fragment(uint32_t node_id,
                                   const struct hifs_seg_loc *loc,
                                   const uint8_t *data)
{
    if (!loc || !data || loc->length == 0)
        return -EINVAL;

    char path[PATH_MAX];
    hive_seg_fragment_path(path, sizeof(path), node_id, loc->seg_id);

    pthread_mutex_lock(&g_seg_landing_io_mu);
    int fd = open(path,
                  O_RDWR | O_CREAT
#ifdef O_CLOEXEC
                  | O_CLOEXEC
#endif
                  ,
                  S_IRUSR | S_IWUSR | S_IRGRP);
    if (fd < 0) {
        int err = errno;
        pthread_mutex_unlock(&g_seg_landing_io_mu);
        return -err;
    }

    int rc = hive_seg_pwrite_all(fd,
                                 data,
                                 loc->length,
                                 (off_t)loc->offset);
    close(fd);
    pthread_mutex_unlock(&g_seg_landing_io_mu);
    return rc;
}

static int hive_seg_cache_record_fragment(uint64_t txn_id,
                                          uint32_t generation,
                                          const struct hive_seg_block_input *blk,
                                          uint32_t frag_idx,
                                          uint32_t node_id,
                                          const struct hifs_seg_loc *loc)
{
    (void)frag_idx;
    if (!blk || !loc)
        return -EINVAL;

    struct hive_seg_index_disk_rec rec = {
        .seg_id = loc->seg_id,
        .volume_id = blk->volume_id,
        .block_no = blk->block_no,
        .txn_id = txn_id,
        .generation = generation,
        .state = HIFS_STRIPE_PREPARED,
        .offset = loc->offset,
        .length = loc->length,
        .reserved = node_id,
    };

    pthread_mutex_lock(&g_seg_cache.mu);
    if (!g_seg_cache.ready) {
        pthread_mutex_unlock(&g_seg_cache.mu);
        return -ESHUTDOWN;
    }
    if (!hive_seg_index_mem_append(&g_seg_cache.outbound_idx, &rec)) {
        pthread_mutex_unlock(&g_seg_cache.mu);
        return -ENOMEM;
    }
    int fd = g_seg_cache.outbound_idx_fd;
    int rc = 0;
    if (fd >= 0)
        rc = hive_seg_write_all(fd, &rec, sizeof(rec));
    pthread_mutex_unlock(&g_seg_cache.mu);
    return rc;
}

static inline uint64_t hive_seg_block_stripe_hint(const struct hive_seg_block_input *blk)
{
    uint64_t g = blk ? blk->block_no : 0;
    if (blk && blk->has_stripe_id)
        memcpy(&g, blk->stripe_id, sizeof(g));
    return g;
}

static void hive_seg_build_placement_seed(const struct hive_seg_block_input *blk,
                                          uint64_t txn_id,
                                          uint32_t generation,
                                          struct hifs_placement_seed *seed)
{
    memset(seed, 0, sizeof(*seed));
    seed->cluster_id = hbc.cluster_id;
    seed->fs_id = blk->volume_id;
    seed->inode_id = blk->volume_id;
    seed->stripe_id = hive_seg_block_stripe_hint(blk);
    seed->txn_id = txn_id;
    seed->lba_start = blk->block_no;
    seed->block_count = 1;
    seed->generation = generation;
    seed->placement_epoch = blk->placement_epoch;
    snprintf(seed->block_id,
             sizeof(seed->block_id),
             "%llu:%llu",
             (unsigned long long)blk->volume_id,
             (unsigned long long)blk->block_no);
}

static int hive_seg_log_stripe_prepared(uint64_t txn_id,
                                        uint64_t stripe_id,
                                        uint32_t generation,
                                        const struct hifs_seg_loc *frag_locs,
                                        size_t frag_count)
{
    char txn_uuid[HIVE_SEG_UUID_TEXT_LEN];
    char stripe_uuid[HIVE_SEG_UUID_TEXT_LEN];
    if (!hive_seg_txn_uuid_fill(txn_id, txn_uuid) ||
        !hive_seg_stripe_uuid_fill(txn_id, stripe_uuid))
        return -EIO;

    struct hifs_wbl_prepared_rec rec = {
        .generation = generation,
    };
    memcpy(rec.txn_id, txn_uuid, sizeof(rec.txn_id));
    memcpy(rec.stripe_id, stripe_uuid, sizeof(rec.stripe_id));
    if (frag_locs && frag_count > 0) {
        if (frag_count > HIFS_EC_TOTAL_SRIPES)
            frag_count = HIFS_EC_TOTAL_SRIPES;
        memcpy(rec.frag_locs,
               frag_locs,
               frag_count * sizeof(struct hifs_seg_loc));
    }
    if (hifs_wbl_mark_stripe_prepared(&g_hive_guard_wbl_ctx, &rec) != 0) {
        int err = errno ? -errno : -EIO;
        hifs_crit("hive_segmentation: unable to append WBL stripe prepared record for txn=%llu: %s",
                 (unsigned long long)txn_id,
                 strerror(errno));
        return err;
    }
    return 0;
}

static int hive_seg_prepare_fragments(struct hive_seg_block_input *blk,
                                      uint64_t txn_id,
                                      uint32_t generation,
                                      const struct hifs_ec_stripe_set *ec)
{
    if (!blk || !ec || !ec->chunks || ec->chunk_count == 0 || ec->chunk_len == 0)
        return -EINVAL;
    if (ec->chunk_len > UINT32_MAX)
        return -EOVERFLOW;

    struct hifs_placement_seed seed;
    hive_seg_build_placement_seed(blk, txn_id, generation, &seed);
    struct hifs_placement_result placement = {0};
    int rc = hifs_place_stripe(&seed, &placement);
    if (rc != 0)
        return rc;

    size_t frag_count = ec->chunk_count;
    if (frag_count > HIFS_EC_TOTAL_SRIPES)
        frag_count = HIFS_EC_TOTAL_SRIPES;
    if (placement.target_count < frag_count)
        frag_count = placement.target_count;
    if (frag_count == 0)
        return -EINVAL;

    blk->has_frag_targets = true;
    blk->frag_target_count = placement.target_count;
    memcpy(blk->frag_targets,
           placement.targets,
           sizeof(placement.targets));

    struct hifs_seg_loc frag_locs[HIFS_EC_TOTAL_SRIPES];
    memset(frag_locs, 0, sizeof(frag_locs));

    uint32_t chunk_len = (uint32_t)ec->chunk_len;
    for (size_t i = 0; i < frag_count; ++i) {
        if (!ec->chunks[i])
            return -EIO;
        uint32_t node_id = placement.targets[i].node_id;
        if (node_id == 0)
            return -ENODEV;
        pthread_mutex_lock(&g_seg_stage_allocator.mu);
        rc = hive_seg_stage_fragment_locked(node_id, chunk_len, &frag_locs[i]);
        pthread_mutex_unlock(&g_seg_stage_allocator.mu);
        if (rc != 0)
            return rc;
        rc = hive_seg_write_fragment(node_id, &frag_locs[i], ec->chunks[i]);
        if (rc != 0)
            return rc;
        rc = hive_seg_cache_record_fragment(txn_id,
                                            generation,
                                            blk,
                                            (uint32_t)i,
                                            node_id,
                                            &frag_locs[i]);
        if (rc != 0)
            return rc;
        rc = hive_seg_mark_outbound_queued(txn_id,
                                           generation,
                                           blk,
                                           node_id,
                                           &frag_locs[i]);
        if (rc != 0)
            return rc;
        uint32_t frag_idx = placement.targets[i].frag_idx;
        rc = hive_seg_send_fragment(txn_id,
                                    generation,
                                    blk,
                                    frag_idx,
                                    node_id,
                                    &frag_locs[i],
                                    ec->chunks[i]);
        if (rc != 0)
            return rc;
    }

    rc = hive_seg_log_stripe_prepared(txn_id,
                                      blk->block_no,
                                      generation,
                                      frag_locs,
                                      frag_count);
    if (rc != 0)
        return rc;

    hive_seg_publish_placement_state(blk,
                                     txn_id,
                                     blk->frag_targets,
                                     frag_count,
                                     frag_locs,
                                     frag_count);

    hifs_guard_notify_write_ack(blk->volume_id,
                                blk->block_no,
                                blk->hash,
                                HIFS_BLOCK_HASH_SIZE);
    return 0;
}

static void hive_seg_stage_blocks(struct hive_seg_block_input *blocks,
                                  uint32_t count)
{
    if (!blocks || count == 0)
        return;

    for (uint32_t i = 0; i < count; ++i) {
        if (blocks[i].len == 0 || blocks[i].has_landing_loc)
            continue;
        hive_seg_prepare_existing_landing(&blocks[i]);
    }

    pthread_mutex_lock(&g_seg_stage_allocator.mu);

    uint64_t total = 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (blocks[i].len == 0 || blocks[i].has_landing_loc)
            continue;
        total += hive_seg_align_len(blocks[i].len);
    }

    if (total > 0 && total <= HIFS_SEGMENT_SIZE)
        hive_seg_stage_ensure_capacity_locked((uint32_t)total);
    else if (g_seg_stage_allocator.cursor.seg_id == 0)
        hive_seg_stage_rotate_locked();

    for (uint32_t i = 0; i < count; ++i)
        hive_seg_stage_block_locked(&blocks[i]);

    pthread_mutex_unlock(&g_seg_stage_allocator.mu);
}

static inline void hive_seg_copy_block_input(struct hive_seg_block_input *dst,
                                             const struct hive_seg_block_input *src)
{
    if (!dst || !src)
        return;
    *dst = *src;
    if (!src->has_hash)
        memset(dst->hash, 0, sizeof(dst->hash));
    if (!src->has_stripe_id)
        memset(dst->stripe_id, 0, sizeof(dst->stripe_id));
    dst->has_landing_loc = false;
    memset(&dst->landing_loc, 0, sizeof(dst->landing_loc));
    dst->has_frag_targets = false;
    dst->frag_target_count = 0;
    memset(dst->frag_targets, 0, sizeof(dst->frag_targets));
}

static inline void hive_seg_log_write_intent(const struct hive_seg_block_input *blk,
                                             uint64_t txn_id,
                                             uint32_t generation)
{
    if (!blk)
        return;

    char txn_uuid[HIVE_SEG_UUID_TEXT_LEN];
    char stripe_uuid[HIVE_SEG_UUID_TEXT_LEN];
    if (!hive_seg_txn_uuid_fill(txn_id, txn_uuid) ||
        !hive_seg_stripe_uuid_fill(txn_id, stripe_uuid))
        return;

    struct hifs_wbl_write_intent_rec rec = {
        .inode_id = blk->volume_id,
        .generation = generation,
    };
    memcpy(rec.txn_id, txn_uuid, sizeof(rec.txn_id));
    memcpy(rec.stripe_id, stripe_uuid, sizeof(rec.stripe_id));
    rec.range.inode_id = blk->volume_id;
    rec.range.lba_start = blk->block_no;
    rec.range.block_count = 1;
    snprintf(rec.block_id, sizeof(rec.block_id),
             "%llu:%llu",
             (unsigned long long)blk->volume_id,
             (unsigned long long)blk->block_no);
    if (hifs_wbl_mark_write_intent(&g_hive_guard_wbl_ctx, &rec) != 0) {
        hifs_err("hive_segmentation: unable to append WBL write intent for %llu:%llu: %s",
                 (unsigned long long)blk->volume_id,
                 (unsigned long long)blk->block_no,
                 strerror(errno));
    }
}

// TODO: Now that we have the incoming data on permanent disk, we can 

static inline void hive_seg_log_metadata(const struct hive_seg_block_input *blk,
                                         uint64_t txn_id)
{
    if (!blk)
        return;
    struct hive_seg_mcl_block_entry entry = {
        .volume_id = blk->volume_id,
        .block_no = blk->block_no,
        .hash_algo = blk->hash_algo,
        .block_bytes = blk->len,
        .placement_epoch = blk->placement_epoch,
    };
    memcpy(entry.hash, blk->hash, sizeof(entry.hash));
    memcpy(entry.stripe_id, blk->stripe_id, sizeof(entry.stripe_id));
    if (hifs_mcl_append(&g_hive_guard_mcl_ctx,
                        CHANGE_STRIPE_INFO,
                        HIFS_MCL_NEW,
                        &entry,
                        sizeof(entry),
                        txn_id,
                        blk->block_no) != 0) {
        hifs_err("hive_segmentation: unable to append MCL record for %llu:%llu: %s",
                 (unsigned long long)blk->volume_id,
                 (unsigned long long)blk->block_no,
                 strerror(errno));
    }
}

static int hive_seg_apply_block(struct hive_seg_block_input *blk,
                                uint64_t txn_id,
                                uint32_t generation)
{
    if (!blk || !blk->data || blk->len == 0)
        return -EINVAL;
    if (!blk->has_hash)
        return -EINVAL;

    hive_seg_log_write_intent(blk, txn_id, generation);
    int rc = hive_seg_stage_landing_eccoded(blk, txn_id, generation);
    if (rc != 0)
        return rc;
    hive_seg_log_metadata(blk, txn_id);

    return hifs_put_block(blk->volume_id,
                          blk->block_no,
                          blk->data,
                          blk->len,
                          blk->hash_algo,
                          blk->hash,
                          blk->stripe_id,
                          blk->stripe_algo,
                          blk->placement_epoch);
}

static void hive_seg_process_task(struct hive_seg_task *task)
{
    if (!task)
        return;
    uint64_t resolved_txn = hive_seg_resolve_txn(task->txn_hint);
    if (task->type == HIVE_SEG_TASK_SINGLE) {
        int rc = hive_seg_apply_block(&task->u.single,
                                      resolved_txn,
                                      task->generation);
        if (rc != 0) {
            hifs_err("hive_segmentation: failed to apply block %llu:%llu rc=%d",
                     (unsigned long long)task->u.single.volume_id,
                     (unsigned long long)task->u.single.block_no,
                     rc);
        }
        hive_seg_txn_identity_forget(resolved_txn);
        return;
    }

    uint64_t total_bytes = 0;
    for (uint32_t i = 0; i < task->u.contig.count; ++i) {
        struct hive_seg_block_input *blk = &task->u.contig.blocks[i];
        int rc = hive_seg_apply_block(blk, resolved_txn, task->generation);
        if (rc != 0) {
            hifs_err("hive_segmentation: failed to apply contig block %u/%u (%llu:%llu) rc=%d",
                     i + 1,
                     task->u.contig.count,
                     (unsigned long long)blk->volume_id,
                     (unsigned long long)blk->block_no,
                     rc);
            break;
        }
        total_bytes += blk->len;
    }
    atomic_fetch_add(&g_stats.contig_write_calls, 1);
    atomic_fetch_add(&g_stats.contig_write_bytes, total_bytes);
    hive_seg_txn_identity_forget(resolved_txn);
}

static bool hive_seg_queue_pop(struct hive_seg_task *out)
{
    pthread_mutex_lock(&g_seg_mu);
    while (g_seg_running && g_seg_size == 0)
        pthread_cond_wait(&g_seg_cv, &g_seg_mu);

    if (!g_seg_running && g_seg_size == 0) {
        pthread_mutex_unlock(&g_seg_mu);
        return false;
    }

    *out = g_seg_queue[g_seg_head];
    g_seg_head = (g_seg_head + 1u) % HIVE_SEG_QUEUE_DEPTH;
    g_seg_size--;
    pthread_cond_signal(&g_seg_space_cv);
    pthread_mutex_unlock(&g_seg_mu);
    return true;
}

static void *hive_seg_worker_main(void *arg)
{
    (void)arg;
    struct hive_seg_task task;
    while (hive_seg_queue_pop(&task)) {
        hive_seg_process_task(&task);
        hive_seg_task_cleanup(&task);
    }
    return NULL;
}

static inline size_t hive_seg_worker_budget(void)
{
    long cpu_cnt = sysconf(_SC_NPROCESSORS_ONLN);
    if (cpu_cnt <= 0)
        cpu_cnt = 2;
    if (cpu_cnt > (long)HIVE_SEG_MAX_WORKERS)
        cpu_cnt = (long)HIVE_SEG_MAX_WORKERS;
    return (size_t)cpu_cnt;
}

static int hive_seg_queue_push(const struct hive_seg_task *task)
{
    if (!task)
        return -EINVAL;
    pthread_mutex_lock(&g_seg_mu);
    while (g_seg_running && g_seg_size == HIVE_SEG_QUEUE_DEPTH)
        pthread_cond_wait(&g_seg_space_cv, &g_seg_mu);

    if (!g_seg_running) {
        pthread_mutex_unlock(&g_seg_mu);
        return -ESHUTDOWN;
    }

    g_seg_queue[g_seg_tail] = *task;
    g_seg_tail = (g_seg_tail + 1u) % HIVE_SEG_QUEUE_DEPTH;
    g_seg_size++;
    pthread_cond_signal(&g_seg_cv);
    pthread_mutex_unlock(&g_seg_mu);
    return 0;
}

static int hive_seg_setup_workers(void)
{
    g_seg_worker_count = hive_seg_worker_budget();
    for (size_t i = 0; i < g_seg_worker_count; ++i) {
        int rc = pthread_create(&g_seg_workers[i], NULL, hive_seg_worker_main, NULL);
        if (rc != 0) {
            g_seg_worker_count = i;
            return -rc;
        }
    }
    return 0;
}

int hive_recieve_data_plane(void)
{
    bool expected = false;
    if (!atomic_compare_exchange_strong_explicit(&g_seg_started,
                                                 &expected,
                                                 true,
                                                 memory_order_seq_cst,
                                                 memory_order_seq_cst)) {
        return 0;
    }

    int rc = hive_seg_cache_init();
    if (rc != 0) {
        atomic_store(&g_seg_started, false);
        return rc;
    }

    pthread_mutex_lock(&g_seg_mu);
    g_seg_head = g_seg_tail = g_seg_size = 0;
    g_seg_running = true;
    pthread_mutex_unlock(&g_seg_mu);

    rc = hive_seg_setup_workers();
    if (rc != 0) {
        hive_segmentation_shutdown();
        return rc;
    }

    rc = hifs_sn_tcp_start(storage_node_stripe_port, hive_seg_sn_recv_cb);
    if (rc != 0) {
        hive_segmentation_shutdown();
        return rc;
    }
    g_seg_sn_running = true;
    return 0;
}

void hive_segmentation_shutdown(void)
{
    if (!atomic_load(&g_seg_started))
        return;

    pthread_mutex_lock(&g_seg_mu);
    g_seg_running = false;
    pthread_cond_broadcast(&g_seg_cv);
    pthread_cond_broadcast(&g_seg_space_cv);
    pthread_mutex_unlock(&g_seg_mu);

    if (g_seg_sn_running) {
        hifs_sn_tcp_stop();
        g_seg_sn_running = false;
    }

    for (size_t i = 0; i < g_seg_worker_count; ++i)
        pthread_join(g_seg_workers[i], NULL);
    g_seg_worker_count = 0;

    pthread_mutex_lock(&g_seg_mu);
    while (g_seg_size > 0) {
        struct hive_seg_task task = g_seg_queue[g_seg_head];
        g_seg_head = (g_seg_head + 1u) % HIVE_SEG_QUEUE_DEPTH;
        g_seg_size--;
        pthread_mutex_unlock(&g_seg_mu);
        hive_seg_task_cleanup(&task);
        pthread_mutex_lock(&g_seg_mu);
    }
    pthread_mutex_unlock(&g_seg_mu);

    hive_seg_cache_shutdown();
    atomic_store(&g_seg_started, false);
}

int hive_seg_submit_single(const struct hive_seg_single_request *req)
{
    if (!req || !req->block)
        return -EINVAL;
    if (!atomic_load(&g_seg_started))
        return -EAGAIN;

    struct hive_seg_task task = {
        .type = HIVE_SEG_TASK_SINGLE,
        .txn_hint = req->txn_hint,
        .generation = req->generation,
    };
    hive_seg_copy_block_input(&task.u.single, req->block);
    hive_seg_stage_blocks(&task.u.single, 1);
    return hive_seg_queue_push(&task);
}

int hive_seg_submit_contig(const struct hive_seg_contig_request *req)
{
    if (!req || !req->blocks || req->block_count == 0)
        return -EINVAL;
    if (!atomic_load(&g_seg_started))
        return -EAGAIN;

    struct hive_seg_task task = {
        .type = HIVE_SEG_TASK_CONTIG,
        .txn_hint = req->txn_hint,
        .generation = req->generation,
    };
    task.u.contig.count = req->block_count;
    task.u.contig.blocks = calloc(req->block_count, sizeof(*task.u.contig.blocks));
    if (!task.u.contig.blocks)
        return -ENOMEM;
    for (uint32_t i = 0; i < req->block_count; ++i)
        hive_seg_copy_block_input(&task.u.contig.blocks[i], &req->blocks[i]);
    hive_seg_stage_blocks(task.u.contig.blocks, req->block_count);

    int rc = hive_seg_queue_push(&task);
    if (rc != 0)
        free(task.u.contig.blocks);
    return rc;
}
