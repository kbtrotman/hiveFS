/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>

#include <liberasurecode/erasurecode.h>

int last_node_in_cascade;
int cascade_length;

/* ======================= Internal helpers ======================= */

/* Advance 6 positions from last_node_in_cascade, wrapping within [1, cascade_length] */
#define NEXT_STRIPE_NODE(last_node_in_cascade, cascade_length) \
    last_node_in_cascade = ((((last_node_in_cascade - 1) + 6) % (cascade_length)) + 1)



