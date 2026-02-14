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
 * Re-balance load after a new node additon, drain a node on 
 * node removal and re-balance, or other change.
 */

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <mysql.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../hifs_shared_defs.h"
#include "hive_guard.h"
#include "hive_guard_erasure_code.h"
#include "hive_guard_rebalance.h"
#include "hive_guard_sn_tcp.h"
#include "hive_guard_sql.h"

typedef unsigned __int128 hg_u128;


static inline double hg_rebal_clamp_ratio(double pct)
{
	if (pct > 1.0)
		pct /= 100.0;
	if (pct < 0.0)
		pct = 0.0;
	if (pct > 1.0)
		pct = 1.0;
	return pct;
}

static inline uint64_t hg_rebal_effective_bytes(uint64_t value, uint64_t fallback)
{
	return value ? value : fallback;
}

static struct hg_rebalance_policy hg_active_policy = {
	.node_add = {
		.target_fill_ratio = HG_REBAL_NODE_ADD_FILL_DEFAULT,
		.throttle_bytes = HG_REBAL_NODE_ADD_THROTTLE,
	},
	.capacity = {
		.high_water_ratio = HG_REBAL_HIGH_WATER_DEFAULT,
		.low_water_ratio = HG_REBAL_LOW_WATER_DEFAULT,
		.skew_gap_ratio = HG_REBAL_CAP_SKEW_GAP_DEFAULT,
		.throttle_bytes = HG_REBAL_CAP_THROTTLE,
	},
	.hotspot = {
		.read_threshold = HG_REBAL_HOTSPOT_READ_DEFAULT,
		.throttle_bytes = HG_REBAL_HOTSPOT_THROTTLE,
	},
};

void hg_rebalance_set_policy(const struct hg_rebalance_policy *policy)
{
	if (!policy)
		return;
	struct hg_rebalance_policy normalized = *policy;
	normalized.node_add.target_fill_ratio =
		hg_rebal_clamp_ratio(policy->node_add.target_fill_ratio > 0.0
				    ? policy->node_add.target_fill_ratio
				    : HG_REBAL_NODE_ADD_FILL_DEFAULT);
	normalized.node_add.throttle_bytes =
		hg_rebal_effective_bytes(policy->node_add.throttle_bytes,
					 HG_REBAL_NODE_ADD_THROTTLE);
	normalized.capacity.high_water_ratio =
		hg_rebal_clamp_ratio(policy->capacity.high_water_ratio > 0.0
				    ? policy->capacity.high_water_ratio
				    : HG_REBAL_HIGH_WATER_DEFAULT);
	normalized.capacity.low_water_ratio =
		hg_rebal_clamp_ratio(policy->capacity.low_water_ratio > 0.0
				    ? policy->capacity.low_water_ratio
				    : HG_REBAL_LOW_WATER_DEFAULT);
	if (normalized.capacity.low_water_ratio >= normalized.capacity.high_water_ratio)
		normalized.capacity.low_water_ratio =
			hg_rebal_clamp_ratio(normalized.capacity.high_water_ratio * 0.8);
	normalized.capacity.skew_gap_ratio =
		hg_rebal_clamp_ratio(policy->capacity.skew_gap_ratio > 0.0
				    ? policy->capacity.skew_gap_ratio
				    : HG_REBAL_CAP_SKEW_GAP_DEFAULT);
	normalized.capacity.throttle_bytes =
		hg_rebal_effective_bytes(policy->capacity.throttle_bytes,
					 HG_REBAL_CAP_THROTTLE);
	normalized.hotspot.read_threshold =
		policy->hotspot.read_threshold ? policy->hotspot.read_threshold
						    : HG_REBAL_HOTSPOT_READ_DEFAULT;
	normalized.hotspot.throttle_bytes =
		hg_rebal_effective_bytes(policy->hotspot.throttle_bytes,
					 HG_REBAL_HOTSPOT_THROTTLE);
	hg_active_policy = normalized;
}

const struct hg_rebalance_policy *hg_rebalance_get_policy(void)
{
	return &hg_active_policy;
}

struct hg_node_balance_info {
	const struct hive_storage_node *node;
	uint32_t node_id;
	uint64_t capacity;
	uint64_t used;
	double usage_ratio;
};

struct hg_node_balance_delta {
	const struct hive_storage_node *node;
	uint32_t node_id;
	uint64_t delta_bytes;
	double usage_ratio;
};

struct hg_stripe_ref {
	uint64_t volume_id;
	uint64_t block_no;
	struct stripe_location loc;
};

static uint32_t hg_sql_to_u32(const char *s)
{
	return s ? (uint32_t)strtoul(s, NULL, 10) : 0u;
}

static uint64_t hg_sql_to_u64(const char *s)
{
	return s ? strtoull(s, NULL, 10) : 0ull;
}

static inline double hg_normalize_pct(double pct)
{
	if (pct > 1.0)
		pct /= 100.0;
	if (pct < 0.0)
		pct = 0.0;
	if (pct > 1.0)
		pct = 1.0;
	return pct;
}

static inline uint64_t hg_min_u64(uint64_t a, uint64_t b)
{
	return (a < b) ? a : b;
}

static inline size_t hg_min_size(size_t a, size_t b)
{
	return (a < b) ? a : b;
}

static const struct hive_storage_node *hg_lookup_node(uint32_t node_id)
{
	size_t count = 0;
	const struct hive_storage_node *nodes = hifs_get_storage_nodes(&count);
	if ((!nodes || count == 0) && hifs_load_storage_nodes())
		nodes = hifs_get_storage_nodes(&count);
	if (!nodes)
		return NULL;

	for (size_t i = 0; i < count; ++i) {
		if (nodes[i].id == node_id)
			return &nodes[i];
	}
	return NULL;
}

static struct hg_node_balance_info *hg_collect_node_info(size_t *count_out)
{
	size_t count = 0;
	const struct hive_storage_node *nodes = hifs_get_storage_nodes(&count);
	if ((!nodes || count == 0) && hifs_load_storage_nodes())
		nodes = hifs_get_storage_nodes(&count);
	if (!nodes || count == 0)
		return NULL;

	struct hg_node_balance_info *out = calloc(count, sizeof(*out));
	if (!out)
		return NULL;

	size_t keep = 0;
	for (size_t i = 0; i < count; ++i) {
		const struct hive_storage_node *node = &nodes[i];
		if (node->id == 0 || node->storage_capacity_bytes == 0)
			continue;
		if (node->fenced)
			continue;

		struct hg_node_balance_info info = {
			.node = node,
			.node_id = node->id,
			.capacity = node->storage_capacity_bytes,
			.used = node->storage_used_bytes +
				node->storage_reserved_bytes +
				node->storage_overhead_bytes,
		};
		if (info.used > info.capacity)
			info.used = info.capacity;
		info.usage_ratio = (info.capacity == 0)
				   ? 0.0
				   : (double)info.used / (double)info.capacity;
		out[keep++] = info;
	}

	if (keep == 0) {
		free(out);
		return NULL;
	}

	*count_out = keep;
	return out;
}

static bool hg_compute_usage_totals(const struct hg_node_balance_info *nodes,
				    size_t count,
				    uint32_t skip_node_id,
				    hg_u128 *used_out,
				    hg_u128 *cap_out,
				    double *ratio_out)
{
	hg_u128 used = 0;
	hg_u128 cap = 0;

	for (size_t i = 0; i < count; ++i) {
		if (nodes[i].node_id == skip_node_id)
			continue;
		used += (hg_u128)nodes[i].used;
		cap += (hg_u128)nodes[i].capacity;
	}

	if (cap == 0)
		return false;

	if (used_out)
		*used_out = used;
	if (cap_out)
		*cap_out = cap;
	if (ratio_out) {
		long double ratio = (long double)used / (long double)cap;
		*ratio_out = (double)ratio;
	}
	return true;
}

static uint64_t hg_target_bytes_for_node(const struct hg_node_balance_info *node,
					 hg_u128 used_sum,
					 hg_u128 cap_sum)
{
	if (cap_sum == 0)
		return 0;

	hg_u128 num = (hg_u128)node->capacity * used_sum;
	hg_u128 target = num / cap_sum;
	if (target > UINT64_MAX)
		return UINT64_MAX;
	return (uint64_t)target;
}

static int hg_delta_desc(const void *a, const void *b)
{
	const struct hg_node_balance_delta *da = a;
	const struct hg_node_balance_delta *db = b;
	if (da->delta_bytes == db->delta_bytes) {
		if (da->usage_ratio == db->usage_ratio)
			return (db->node_id > da->node_id) - (db->node_id < da->node_id);
		return (da->usage_ratio < db->usage_ratio) -
		       (da->usage_ratio > db->usage_ratio);
	}
	return (db->delta_bytes > da->delta_bytes) -
	       (db->delta_bytes < da->delta_bytes);
}

static int hg_usage_asc_ptr(const void *a, const void *b)
{
	const struct hg_node_balance_info *const *pa = a;
	const struct hg_node_balance_info *const *pb = b;
	if ((*pa)->usage_ratio < (*pb)->usage_ratio)
		return -1;
	if ((*pa)->usage_ratio > (*pb)->usage_ratio)
		return 1;
	return ((*pa)->node_id > (*pb)->node_id) - ((*pa)->node_id < (*pb)->node_id);
}

static struct hg_node_balance_delta *
hg_calc_stripe_data_node_sources(const struct hg_node_balance_info *nodes,
				 size_t count,
				 uint32_t skip_node_id,
				 hg_u128 used_sum,
				 hg_u128 cap_sum,
				 uint64_t *total_surplus_out,
				 size_t *out_count)
{
	if (!nodes || count == 0 || cap_sum == 0)
		return NULL;

	struct hg_node_balance_delta *deltas = calloc(count, sizeof(*deltas));
	if (!deltas)
		return NULL;

	size_t idx = 0;
	uint64_t total = 0;

	for (size_t i = 0; i < count; ++i) {
		if (nodes[i].node_id == skip_node_id)
			continue;
		uint64_t target = hg_target_bytes_for_node(&nodes[i], used_sum, cap_sum);
		if (nodes[i].used <= target)
			continue;
		deltas[idx].node = nodes[i].node;
		deltas[idx].node_id = nodes[i].node_id;
		deltas[idx].usage_ratio = nodes[i].usage_ratio;
		deltas[idx].delta_bytes = nodes[i].used - target;
		total += deltas[idx].delta_bytes;
		++idx;
	}

	if (idx == 0) {
		free(deltas);
		return NULL;
	}

	qsort(deltas, idx, sizeof(*deltas), hg_delta_desc);
	if (total_surplus_out)
		*total_surplus_out = total;
	*out_count = idx;
	return deltas;
}

static struct hg_node_balance_delta *
hg_calc_receiver_targets(const struct hg_node_balance_info *nodes,
			 size_t count,
			 uint32_t skip_node_id,
			 hg_u128 used_sum,
			 hg_u128 cap_sum,
			 size_t *out_count)
{
	if (!nodes || count == 0 || cap_sum == 0)
		return NULL;

	struct hg_node_balance_delta *deltas = calloc(count, sizeof(*deltas));
	if (!deltas)
		return NULL;

	size_t idx = 0;
	for (size_t i = 0; i < count; ++i) {
		if (nodes[i].node_id == skip_node_id)
			continue;
		uint64_t target = hg_target_bytes_for_node(&nodes[i], used_sum, cap_sum);
		if (nodes[i].used >= target)
			continue;
		deltas[idx].node = nodes[i].node;
		deltas[idx].node_id = nodes[i].node_id;
		deltas[idx].usage_ratio = nodes[i].usage_ratio;
		deltas[idx].delta_bytes = target - nodes[i].used;
		++idx;
	}

	if (idx == 0) {
		free(deltas);
		return NULL;
	}

	qsort(deltas, idx, sizeof(*deltas), hg_delta_desc);
	*out_count = idx;
	return deltas;
}

static void hg_plan_reset(struct hg_rebalance_plan *plan)
{
	if (!plan)
		return;
	free(plan->items);
	plan->items = NULL;
	plan->count = 0;
	plan->capacity = 0;
	plan->bytes_planned = 0;
}

static bool hg_plan_reserve(struct hg_rebalance_plan *plan, size_t extra)
{
	if (!plan)
		return false;
	size_t needed = plan->count + extra;
	if (needed <= plan->capacity)
		return true;

	size_t new_cap = plan->capacity ? plan->capacity : HG_REBAL_PLAN_CHUNK;
	while (new_cap < needed)
		new_cap *= 2;

	struct hg_stripe_transfer *tmp =
		realloc(plan->items, new_cap * sizeof(*tmp));
	if (!tmp)
		return false;
	plan->items = tmp;
	plan->capacity = new_cap;
	return true;
}

static bool hg_plan_append(struct hg_rebalance_plan *plan,
			   const struct hg_stripe_ref *stripe,
			   uint32_t dst_node_id)
{
	if (!hg_plan_reserve(plan, 1))
		return false;

	struct hg_stripe_transfer *dst = &plan->items[plan->count++];
	dst->volume_id = stripe->volume_id;
	dst->block_no = stripe->block_no;
	dst->loc = stripe->loc;
	dst->dst_node_id = dst_node_id;
	plan->bytes_planned += HG_REBAL_STRIPE_BYTES;
	return true;
}

static struct hg_stripe_ref *
hg_fetch_node_stripes(uint32_t node_id, size_t limit, size_t *out_count)
{
	if (limit == 0)
		return NULL;

	char sql[256];
	int written = snprintf(sql, sizeof(sql),
			       SQL_BLOCK_STRIPE_LOCATIONS_SELECT_BY_NODE,
			       (unsigned)node_id,
			       (unsigned long long)limit);
	if (written < 0 || (size_t)written >= sizeof(sql))
		return NULL;

	MYSQL_RES *res = NULL;
	if (!hifs_execute_query(sql, &res))
		return NULL;

	size_t rows = (size_t)mysql_num_rows(res);
	if (rows == 0) {
		mysql_free_result(res);
		sqldb.last_query = NULL;
		return NULL;
	}

	struct hg_stripe_ref *entries =
		calloc(rows, sizeof(struct hg_stripe_ref));
	if (!entries) {
		mysql_free_result(res);
		sqldb.last_query = NULL;
		return NULL;
	}

	MYSQL_ROW row;
	size_t idx = 0;
	while ((row = mysql_fetch_row(res)) && idx < rows) {
		entries[idx].volume_id = hg_sql_to_u64(row[0]);
		entries[idx].block_no = hg_sql_to_u64(row[1]);
		entries[idx].loc.stripe_index = (uint8_t)hg_sql_to_u32(row[2]);
		entries[idx].loc.storage_node_id = node_id;
		entries[idx].loc.shard_id = hg_sql_to_u32(row[3]);
		entries[idx].loc.estripe_id = hg_sql_to_u64(row[4]);
		entries[idx].loc.block_offset = hg_sql_to_u64(row[5]);
		++idx;
	}

	mysql_free_result(res);
	sqldb.last_query = NULL;
	if (idx == 0) {
		free(entries);
		return NULL;
	}

	if (out_count)
		*out_count = idx;
	return entries;
}

static int hg_rec_tcp_stripes(uint32_t node_id,
			      uint32_t shard_id,
			      uint64_t estripe_id,
			      uint8_t **out_buf,
			      size_t *out_len)
{
	const struct hive_storage_node *node = hg_lookup_node(node_id);
	if (!node || !node->address[0])
		return -ENOENT;
	uint16_t port = node->stripe_port ? node->stripe_port
					  : storage_node_stripe_port;
	return hifs_sn_tcp_fetch(node_id, shard_id,
				 node->address, port,
				 estripe_id, out_buf, out_len);
}

static int hg_send_tcp_stripes(uint32_t node_id,
			       uint32_t shard_id,
			       uint64_t estripe_id,
			       const uint8_t *buf,
			       uint32_t len,
			       uint64_t *out_block_offset)
{
	return hifs_send_stripe_to_node(node_id, shard_id,
					estripe_id, buf, len,
					out_block_offset);
}

static bool hg_commit_stripe_move(const struct hg_stripe_transfer *move,
				  uint32_t dst_node_id,
				  uint32_t dst_shard_id,
				  uint64_t dst_estripe_id,
				  uint64_t dst_block_offset)
{
	char sql[256];
	int written = snprintf(sql, sizeof(sql),
			       SQL_BLOCK_STRIPE_LOCATION_UPDATE,
			       (unsigned)dst_node_id,
			       (unsigned)dst_shard_id,
			       (unsigned long long)dst_estripe_id,
			       (unsigned long long)dst_block_offset,
			       (unsigned long long)move->volume_id,
			       (unsigned long long)move->block_no,
			       (unsigned)move->loc.stripe_index);
	if (written < 0 || (size_t)written >= sizeof(sql))
		return false;
	return hifs_insert_sql(sql);
}

static int hg_execute_plan(struct hg_rebalance_plan *plan)
{
	if (!plan || plan->count == 0)
		return 0;

	for (size_t i = 0; i < plan->count; ++i) {
		struct hg_stripe_transfer *move = &plan->items[i];
		uint8_t *payload = NULL;
		size_t payload_len = 0;
		int rc = hg_rec_tcp_stripes(move->loc.storage_node_id,
					    move->loc.shard_id,
					    move->loc.estripe_id,
					    &payload, &payload_len);
		if (rc != 0)
			return rc;

		uint32_t dst_shard = move->loc.shard_id;
		uint64_t new_offset = 0;
		uint64_t new_estripe_id = hifs_alloc_estripe_id();

		rc = hg_send_tcp_stripes(move->dst_node_id,
					 dst_shard,
					 new_estripe_id,
					 payload,
					 (uint32_t)payload_len,
					 &new_offset);
		free(payload);
		if (rc != 0)
			return rc;

		if (!hg_commit_stripe_move(move,
					   move->dst_node_id,
					   dst_shard,
					   new_estripe_id,
					   new_offset))
			return -EIO;
		move->loc.storage_node_id = move->dst_node_id;
		move->loc.estripe_id = new_estripe_id;
		move->loc.block_offset = new_offset;
	}

	return 0;
}

static bool hg_calc_stripes_to_move(uint32_t dst_node_id,
				    const struct hg_node_balance_delta *sources,
				    size_t source_count,
				    uint64_t max_bytes,
				    struct hg_rebalance_plan *plan)
{
	if (!sources || source_count == 0 || max_bytes == 0 || !plan)
		return false;

	uint64_t remaining = max_bytes;
	const uint64_t stripe_bytes = HG_REBAL_STRIPE_BYTES;

	for (size_t i = 0; i < source_count && remaining > 0; ++i) {
		size_t needed = (size_t)((remaining + stripe_bytes - 1) / stripe_bytes);
		needed = hg_min_size(needed, HG_REBAL_MAX_STRIPE_FETCH);

		size_t have = 0;
		struct hg_stripe_ref *refs =
			hg_fetch_node_stripes(sources[i].node_id, needed, &have);
		if (!refs || have == 0) {
			free(refs);
			continue;
		}

		for (size_t j = 0; j < have && remaining > 0; ++j) {
			if (!hg_plan_append(plan, &refs[j], dst_node_id)) {
				free(refs);
				return false;
			}
			if (remaining > stripe_bytes)
				remaining -= stripe_bytes;
			else
				remaining = 0;
		}
		free(refs);
	}
	return plan->count > 0;
}

static uint64_t hg_bytes_needed_for_threshold(const struct hg_node_balance_info *node,
					      double target_ratio)
{
	if (!node || target_ratio <= node->usage_ratio)
		return 0;
	double desired = target_ratio * (double)node->capacity;
	double needed = desired - (double)node->used;
	if (needed <= 0.0)
		return 0;
	if (needed > (double)UINT64_MAX)
		return UINT64_MAX;
	return (uint64_t)needed;
}

int hg_rebal_on_node_add(uint32_t new_node_id)
{
	if (new_node_id == 0)
		return -EINVAL;

	size_t node_count = 0;
	struct hg_node_balance_info *nodes = hg_collect_node_info(&node_count);
	if (!nodes)
		return -ENOENT;

	const struct hg_node_balance_info *target = NULL;
	for (size_t i = 0; i < node_count; ++i) {
		if (nodes[i].node_id == new_node_id) {
			target = &nodes[i];
			break;
		}
	}

	if (!target) {
		free(nodes);
		return -ENOENT;
	}

	const struct hg_rebalance_policy *policy = hg_rebalance_get_policy();
	hg_u128 used_sum = 0;
	hg_u128 cap_sum = 0;
	double target_ratio = 0.0;
	if (!hg_compute_usage_totals(nodes, node_count, 0, &used_sum, &cap_sum,
				     &target_ratio)) {
		free(nodes);
		return -EINVAL;
	}

	uint64_t balance_goal =
		hg_target_bytes_for_node(target, used_sum, cap_sum);
	if (balance_goal > target->used)
		balance_goal -= target->used;
	else
		balance_goal = 0;

	long double desired_ld =
		(long double)target->capacity *
		(long double)policy->node_add.target_fill_ratio;
	uint64_t desired_bytes =
		(desired_ld > (long double)UINT64_MAX) ? UINT64_MAX :
							 (uint64_t)desired_ld;
	uint64_t fill_goal =
		(desired_bytes > target->used) ? (desired_bytes - target->used) :
						 0;

	uint64_t bytes_needed = balance_goal;
	if (fill_goal > 0) {
		if (bytes_needed == 0 || fill_goal < bytes_needed)
			bytes_needed = fill_goal;
	}
	if (bytes_needed == 0) {
		free(nodes);
		return 0;
	}

	uint64_t throttle = policy->node_add.throttle_bytes;
	if (throttle > 0 && bytes_needed > throttle)
		bytes_needed = throttle;

	size_t donor_count = 0;
	uint64_t total_surplus = 0;
	struct hg_node_balance_delta *donors =
		hg_calc_stripe_data_node_sources(nodes, node_count,
						 new_node_id,
						 used_sum, cap_sum,
						 &total_surplus,
						 &donor_count);
	if (!donors) {
		free(nodes);
		return 0;
	}

	struct hg_rebalance_plan plan = {0};
	uint64_t to_move = hg_min_u64(bytes_needed, total_surplus);
	bool have = hg_calc_stripes_to_move(new_node_id, donors, donor_count,
					    to_move, &plan);
	free(donors);
	free(nodes);

	if (!have) {
		hg_plan_reset(&plan);
		return 0;
	}

	int rc = hg_execute_plan(&plan);
	hg_plan_reset(&plan);
	return rc;
}

int hg_rebal_on_node_remove(uint32_t removed_node_id)
{
	if (removed_node_id == 0)
		return -EINVAL;

	size_t node_count = 0;
	struct hg_node_balance_info *nodes = hg_collect_node_info(&node_count);
	if (!nodes)
		return -ENOENT;

	const struct hg_node_balance_info *removed = NULL;
	for (size_t i = 0; i < node_count; ++i) {
		if (nodes[i].node_id == removed_node_id) {
			removed = &nodes[i];
			break;
		}
	}
	if (!removed) {
		free(nodes);
		return -ENOENT;
	}

	/* Build receiver list */
	struct hg_node_balance_info **receivers =
		calloc(node_count, sizeof(*receivers));
	if (!receivers) {
		free(nodes);
		return -ENOMEM;
	}

	size_t recv_count = 0;
	for (size_t i = 0; i < node_count; ++i) {
		if (nodes[i].node_id == removed_node_id)
			continue;
		receivers[recv_count++] = &nodes[i];
	}
	if (recv_count == 0) {
		free(receivers);
		free(nodes);
		return -ENOENT;
	}
	qsort(receivers, recv_count, sizeof(*receivers), hg_usage_asc_ptr);

	uint64_t bytes_remaining = removed->used;
	struct hg_rebalance_plan plan = {0};
	size_t receiver_idx = 0;

	while (bytes_remaining > 0) {
		size_t batch = (size_t)((bytes_remaining + HG_REBAL_STRIPE_BYTES - 1) /
					HG_REBAL_STRIPE_BYTES);
		batch = hg_min_size(batch, HG_REBAL_MAX_STRIPE_FETCH);

		size_t fetched = 0;
		struct hg_stripe_ref *refs =
			hg_fetch_node_stripes(removed_node_id, batch, &fetched);
		if (!refs || fetched == 0) {
			free(refs);
			break;
		}

		for (size_t i = 0; i < fetched; ++i) {
			uint32_t dst_id =
				receivers[receiver_idx % recv_count]->node_id;
			if (!hg_plan_append(&plan, &refs[i], dst_id)) {
				free(refs);
				free(receivers);
				free(nodes);
				hg_plan_reset(&plan);
				return -ENOMEM;
			}
			receiver_idx++;
			if (bytes_remaining > HG_REBAL_STRIPE_BYTES)
				bytes_remaining -= HG_REBAL_STRIPE_BYTES;
			else
				bytes_remaining = 0;
		}

		free(refs);
		if (fetched < batch)
			break;
	}

	free(receivers);
	free(nodes);

	if (plan.count == 0) {
		hg_plan_reset(&plan);
		return 0;
	}

	int rc = hg_execute_plan(&plan);
	hg_plan_reset(&plan);
	return rc;
}

int hg_rebal_fix_hotspots(double high_water_pct, double low_water_pct)
{
	const struct hg_rebalance_policy *policy = hg_rebalance_get_policy();
	double high = (high_water_pct > 0.0)
			      ? hg_normalize_pct(high_water_pct)
			      : policy->capacity.high_water_ratio;
	double low = (low_water_pct > 0.0)
			     ? hg_normalize_pct(low_water_pct)
			     : policy->capacity.low_water_ratio;
	if (high <= low) {
		high = policy->capacity.high_water_ratio;
		low = policy->capacity.low_water_ratio;
	}

	size_t node_count = 0;
	struct hg_node_balance_info *nodes = hg_collect_node_info(&node_count);
	if (!nodes)
		return -ENOENT;

	hg_u128 used_sum = 0;
	hg_u128 cap_sum = 0;
	double target_ratio = 0.0;
	if (!hg_compute_usage_totals(nodes, node_count, 0,
				     &used_sum, &cap_sum, &target_ratio)) {
		free(nodes);
		return -EINVAL;
	}

	size_t donor_count = 0;
	uint64_t total_surplus = 0;
	struct hg_node_balance_delta *donors =
		hg_calc_stripe_data_node_sources(nodes, node_count, 0,
						 used_sum, cap_sum,
						 &total_surplus,
						 &donor_count);
	if (!donors) {
		free(nodes);
		return 0;
	}

	size_t receiver_count = 0;
	struct hg_node_balance_delta *receivers =
		hg_calc_receiver_targets(nodes, node_count, 0,
					 used_sum, cap_sum,
					 &receiver_count);
	if (!receivers) {
		free(donors);
		free(nodes);
		return 0;
	}

	const struct hg_node_balance_delta *hot = NULL;
	for (size_t i = 0; i < donor_count; ++i) {
		if (donors[i].usage_ratio >= high) {
			hot = &donors[i];
			break;
		}
	}

	const struct hg_node_balance_delta *cold = NULL;
	for (size_t i = 0; i < receiver_count; ++i) {
		if (receivers[i].usage_ratio <= low) {
			cold = &receivers[i];
			break;
		}
	}

	if (!hot || !cold ||
	    (hot->usage_ratio - cold->usage_ratio) <
		    policy->capacity.skew_gap_ratio) {
		free(receivers);
		free(donors);
		free(nodes);
		return 0;
	}

	uint64_t desired = hg_bytes_needed_for_threshold(
		&(struct hg_node_balance_info){
			.node = cold->node,
			.node_id = cold->node_id,
			.capacity = cold->node->storage_capacity_bytes,
			.used = cold->node->storage_used_bytes +
				cold->node->storage_reserved_bytes +
				cold->node->storage_overhead_bytes,
			.usage_ratio = cold->usage_ratio,
		}, target_ratio);
	if (desired == 0 || desired > cold->delta_bytes)
		desired = cold->delta_bytes;
	uint64_t bytes_to_move = hg_min_u64(desired, hot->delta_bytes);

	uint64_t throttle = policy->capacity.throttle_bytes;
	if (throttle > 0 && bytes_to_move > throttle)
		bytes_to_move = throttle;

	struct hg_rebalance_plan plan = {0};
	if (!hg_calc_stripes_to_move(cold->node_id, donors, donor_count,
				     bytes_to_move, &plan)) {
		free(receivers);
		free(donors);
		free(nodes);
		hg_plan_reset(&plan);
		return 0;
	}

	int rc = hg_execute_plan(&plan);
	hg_plan_reset(&plan);
	free(receivers);
	free(donors);
	free(nodes);
	return rc;
}
