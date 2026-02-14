/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

/**
 * HiveFS - hive_guard_rebalance.c
 * Entry points and data structures for reactive rebalancing of EC stripes.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../common/hive_common_sql.h"


#define HG_REBAL_STRIPE_BYTES \
	((uint64_t)((HIFS_EC_K > 0) ? (HIFS_DEFAULT_BLOCK_SIZE / HIFS_EC_K) : HIFS_DEFAULT_BLOCK_SIZE))
#define HG_REBAL_MAX_STRIPE_FETCH      512u
#define HG_REBAL_PLAN_CHUNK            64u
#define HG_REBAL_HIGH_WATER_DEFAULT    0.85
#define HG_REBAL_LOW_WATER_DEFAULT     0.65
#define HG_REBAL_NODE_ADD_FILL_DEFAULT 0.30
#define HG_REBAL_NODE_ADD_THROTTLE     (128ull * 1024ull * 1024ull)
#define HG_REBAL_CAP_SKEW_GAP_DEFAULT  0.20
#define HG_REBAL_CAP_THROTTLE          (96ull * 1024ull * 1024ull)
#define HG_REBAL_HOTSPOT_READ_DEFAULT  4096u
#define HG_REBAL_HOTSPOT_THROTTLE      (64ull * 1024ull * 1024ull)



struct hg_stripe_transfer {
	uint64_t volume_id;
	uint64_t block_no;
	struct stripe_location loc;
	uint32_t dst_node_id;
};

struct hg_rebalance_plan {
	struct hg_stripe_transfer *items;
	size_t count;
	size_t capacity;
	uint64_t bytes_planned;
};

enum hg_rebalance_reason {
	HG_REBALANCE_REASON_NODE_ADD = 0,
	HG_REBALANCE_REASON_CAPACITY_SKEW,
	HG_REBALANCE_REASON_HOTSPOT,
	HG_REBALANCE_REASON_COUNT,
};

struct hg_rebalance_node_add_cfg {
	double target_fill_ratio;
	uint64_t throttle_bytes;
};

struct hg_rebalance_capacity_cfg {
	double high_water_ratio;
	double low_water_ratio;
	double skew_gap_ratio;
	uint64_t throttle_bytes;
};

struct hg_rebalance_hotspot_cfg {
	uint32_t read_threshold;
	uint64_t throttle_bytes;
};

struct hg_rebalance_policy {
	struct hg_rebalance_node_add_cfg node_add;
	struct hg_rebalance_capacity_cfg capacity;
	struct hg_rebalance_hotspot_cfg hotspot;
};

int hg_rebal_on_node_add(uint32_t new_node_id);
int hg_rebal_on_node_remove(uint32_t removed_node_id);
int hg_rebal_fix_hotspots(double high_water_pct, double low_water_pct);
void hg_rebalance_set_policy(const struct hg_rebalance_policy *policy);
const struct hg_rebalance_policy *hg_rebalance_get_policy(void);
