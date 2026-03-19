/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../../hifs_shared_defs.h"
#include "hive_guard_placement.h"
#include "hive_guard_wbl.h"
#include "hive_guard_sql.h"

static inline uint64_t hg_mix64(uint64_t x)
{
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

static uint64_t hg_hash_bytes(uint64_t seed, const void *buf, size_t len)
{
    const uint8_t *p = buf;

    while (len >= sizeof(uint64_t)) {
        uint64_t chunk;

        memcpy(&chunk, p, sizeof(chunk));
        seed = hg_mix64(seed ^ chunk);
        p += sizeof(chunk);
        len -= sizeof(chunk);
    }

    if (len > 0) {
        uint64_t tail = 0;

        for (size_t i = 0; i < len; ++i)
            tail |= ((uint64_t)p[i]) << (i * 8u);
        seed = hg_mix64(seed ^ tail ^ len);
    }

    return seed;
}

static inline uint64_t hg_saturating_add64(uint64_t a, uint64_t b)
{
    uint64_t sum = a + b;

    if (sum < a)
        return UINT64_MAX;
    return sum;
}

static uint64_t hg_node_free_bytes(const struct hive_storage_node *node)
{
    uint64_t used = hg_saturating_add64(node->storage_used_bytes,
                                        node->storage_reserved_bytes);

    used = hg_saturating_add64(used, node->storage_overhead_bytes);
    if (used >= node->storage_capacity_bytes)
        return 0;
    return node->storage_capacity_bytes - used;
}

static uint64_t hg_required_headroom_bytes(const struct hive_storage_node *node)
{
    uint64_t pct = node->storage_capacity_bytes / 100u;

    pct *= HIFS_EMERGENCY_HEADROOM_PCT;
    if (pct < HG_MIN_FREE_BYTES)
        pct = HG_MIN_FREE_BYTES;
    if (pct > node->storage_capacity_bytes)
        pct = node->storage_capacity_bytes;
    return pct;
}

static bool hg_node_is_eligible(const struct hive_storage_node *node,
                                uint64_t *free_bytes_out)
{
    if (!node || node->id == 0)
        return false;
    if (!node->online || node->fenced)
        return false;
    if (node->storage_capacity_bytes == 0)
        return false;

    uint64_t free_bytes = hg_node_free_bytes(node);
    uint64_t required = hg_required_headroom_bytes(node);

    if (free_bytes < required)
        return false;
    if (free_bytes_out)
        *free_bytes_out = free_bytes;
    return true;
}

static const struct hive_storage_node *hg_acquire_nodes(size_t *count_out)
{
    size_t count = 0;
    const struct hive_storage_node *nodes = hifs_get_storage_nodes(&count);

    if ((!nodes || count == 0) && hifs_load_storage_nodes())
        nodes = hifs_get_storage_nodes(&count);
    if (count_out)
        *count_out = nodes ? count : 0;
    return nodes;
}

static int hg_collect_candidates(const struct hive_storage_node *nodes,
                                 size_t node_count,
                                 struct hg_node_candidate **out_candidates,
                                 size_t *out_count)
{
    if (!nodes || node_count == 0) {
        *out_candidates = NULL;
        *out_count = 0;
        return 0;
    }

    struct hg_node_candidate *cands =
        calloc(node_count, sizeof(*cands));

    if (!cands)
        return -ENOMEM;

    size_t keep = 0;

    for (size_t i = 0; i < node_count; ++i) {
        uint64_t free_bytes = 0;

        if (!hg_node_is_eligible(&nodes[i], &free_bytes))
            continue;
        cands[keep++] = (struct hg_node_candidate){
            .node = &nodes[i],
            .node_id = nodes[i].id,
            .free_bytes = free_bytes,
        };
    }

    if (keep == 0) {
        free(cands);
        cands = NULL;
    }

    *out_candidates = cands;
    *out_count = keep;
    return 0;
}

static struct hg_node_usage_stats
hg_query_usage(const struct hifs_fragment_target *assigned,
               size_t assigned_count,
               uint32_t node_id)
{
    struct hg_node_usage_stats stats = {0, 0};

    for (size_t i = 0; i < assigned_count; ++i) {
        if (assigned[i].node_id != node_id)
            continue;
        ++stats.total;
        if (assigned[i].frag_idx >= HIFS_EC_DATA_FRAGS)
            ++stats.parity;
    }

    return stats;
}

static uint64_t hg_candidate_score(uint64_t base_seed,
                                   uint32_t node_id,
                                   uint32_t frag_idx,
                                   uint64_t free_bytes)
{
    uint64_t v = base_seed ^ ((((uint64_t)node_id) << 32) | frag_idx);

    v = hg_mix64(v);
    v ^= hg_mix64(free_bytes + frag_idx);
    v += (free_bytes >> HG_FREE_BONUS_SHIFT);
    return v;
}

static bool hg_select_candidate(const struct hifs_fragment_target *assigned,
                                size_t assigned_count,
                                const struct hg_node_candidate *candidates,
                                size_t candidate_count,
                                uint64_t base_seed,
                                uint32_t frag_idx,
                                size_t unique_needed,
                                size_t unique_assigned,
                                struct hg_choice *choice_out)
{
    bool parity_frag = frag_idx >= HIFS_EC_DATA_FRAGS;
    bool require_unique = unique_assigned < unique_needed;
    const struct hg_node_candidate *best = NULL;
    struct hg_node_usage_stats best_usage = {0, 0};
    uint64_t best_score = 0;
    unsigned best_rank = UINT_MAX;

    for (size_t i = 0; i < candidate_count; ++i) {
        const struct hg_node_candidate *cand = &candidates[i];
        struct hg_node_usage_stats usage =
            hg_query_usage(assigned, assigned_count, cand->node_id);

        if (require_unique && usage.total > 0)
            continue;

        unsigned rank = usage.total;

        if (parity_frag && usage.parity > 0)
            rank += HG_PARITY_RANK_PENALTY;

        uint64_t score = hg_candidate_score(base_seed,
                                            cand->node_id,
                                            frag_idx,
                                            cand->free_bytes);

        if (!best || rank < best_rank ||
            (rank == best_rank && score > best_score) ||
            (rank == best_rank && score == best_score &&
             cand->node_id < best->node_id)) {
            best = cand;
            best_usage = usage;
            best_score = score;
            best_rank = rank;
        }
    }

    if (!best)
        return false;

    choice_out->candidate = best;
    choice_out->usage_before = best_usage;
    return true;
}

static uint64_t hg_seed_from_fields(const struct hifs_placement_seed *seed)
{
    uint64_t value = 0x9e3779b185ebca87ULL;

    value = hg_mix64(value ^ seed->cluster_id);
    value = hg_mix64(value ^ seed->fs_id);
    value = hg_mix64(value ^ seed->inode_id);
    value = hg_mix64(value ^ seed->stripe_id);
    value = hg_mix64(value ^ seed->txn_id);
    value = hg_mix64(value ^ seed->lba_start);

    uint64_t range_word = (((uint64_t)seed->block_count) << 32) |
                          (uint64_t)seed->generation;

    value = hg_mix64(value ^ range_word);
    value = hg_mix64(value ^ seed->placement_epoch);

    size_t id_len = strnlen(seed->block_id, sizeof(seed->block_id));

    if (id_len > 0)
        value = hg_hash_bytes(value, seed->block_id, id_len);

    return value;
}

int hifs_place_stripe(const struct hifs_placement_seed *seed,
                      struct hifs_placement_result *out)
{
    if (!seed || !out)
        return -EINVAL;

    size_t node_count = 0;
    const struct hive_storage_node *nodes = hg_acquire_nodes(&node_count);

    if (!nodes || node_count == 0) {
        hifs_err("placement: no storage nodes available");
        return -ENODEV;
    }

    struct hg_node_candidate *candidates = NULL;
    size_t candidate_count = 0;
    int rc = hg_collect_candidates(nodes,
                                   node_count,
                                   &candidates,
                                   &candidate_count);

    if (rc != 0)
        return rc;

    if (candidate_count == 0) {
        hifs_err("placement: no eligible storage nodes");
        free(candidates);
        return -ENOSPC;
    }

    uint64_t base_seed = hg_seed_from_fields(seed);
    struct hifs_fragment_target assignments[HIFS_EC_TOTAL_SRIPES] = {0};
    size_t unique_needed = candidate_count >= HIFS_EC_TOTAL_SRIPES ?
                           HIFS_EC_TOTAL_SRIPES : candidate_count;
    size_t unique_assigned = 0;

    for (uint32_t frag_idx = 0; frag_idx < HIFS_EC_TOTAL_SRIPES; ++frag_idx) {
        struct hg_choice choice;

        if (!hg_select_candidate(assignments,
                                 frag_idx,
                                 candidates,
                                 candidate_count,
                                 base_seed,
                                 frag_idx,
                                 unique_needed,
                                 unique_assigned,
                                 &choice)) {
            hifs_err("placement: unable to select target for fragment %u",
                     frag_idx);
            free(candidates);
            return -ENOSPC;
        }

        assignments[frag_idx].frag_idx = frag_idx;
        assignments[frag_idx].node_id = choice.candidate->node_id;
        if (choice.usage_before.total == 0)
            ++unique_assigned;
    }

    free(candidates);

    out->stripe_id = seed->stripe_id;
    out->target_count = HIFS_EC_TOTAL_SRIPES;
    memcpy(out->targets, assignments, sizeof(assignments));
    return 0;
}

static void hg_init_seed_from_entry(const struct hifs_wbl_mem_entry *entry,
                                    uint64_t fs_id,
                                    uint32_t placement_epoch,
                                    struct hifs_placement_seed *seed)
{
    memset(seed, 0, sizeof(*seed));

    seed->cluster_id = hbc.cluster_id;
    seed->fs_id = fs_id ? fs_id :
                  (entry->range.inode_id ? entry->range.inode_id : entry->inode_id);
    seed->inode_id = entry->inode_id ? entry->inode_id : entry->range.inode_id;
    seed->stripe_id = entry->stripe_id[0];
    seed->txn_id = entry->curr_txn_id;
    seed->lba_start = entry->range.lba_start;
    seed->block_count = entry->range.block_count;
    seed->generation = entry->generation;
    seed->placement_epoch = placement_epoch;
    memcpy(seed->block_id, entry->block_id, sizeof(seed->block_id));
    seed->block_id[sizeof(seed->block_id) - 1] = '\0';
}

int hifs_assign_placement(struct hifs_wbl_mem_entry *entry,
                          uint64_t fs_id,
                          uint32_t placement_epoch,
                          struct hifs_placement_result *out)
{
    if (!entry)
        return -EINVAL;

    struct hifs_placement_seed seed;

    hg_init_seed_from_entry(entry, fs_id, placement_epoch, &seed);

    struct hifs_placement_result local = {0};
    int rc = hifs_place_stripe(&seed, &local);

    if (rc != 0)
        return rc;

    memcpy(entry->targets, local.targets, sizeof(local.targets));
    entry->state = HIFS_STRIPE_PLACED;

    if (out)
        *out = local;
    return 0;
}
