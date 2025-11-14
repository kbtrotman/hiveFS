/**
 * Shared client-side cache state for hive_guard TCP wiring.
 * Minimal subset of the old SQL structures; no MariaDB types here.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

struct guard_link_state {
	bool guard_ready;
	struct machine host;
	struct superblock sb[50];
	int rows;
};

extern struct guard_link_state g_guard_link;
