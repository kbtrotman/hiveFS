#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

/* -------------------------------------------------------------------------- */
/* Lease Op Types (wire-visible)                                              */
/* -------------------------------------------------------------------------- */

typedef enum hg_lease_op {
    HG_LEASE_ACQUIRE = 1,
    HG_LEASE_LOOKUP  = 2,
    HG_LEASE_RENEW   = 3,
    HG_LEASE_RELEASE = 4,
} hg_lease_op_t;

typedef enum hg_lease_mode {
    HG_LEASE_SHARED   = 1,
    HG_LEASE_EXCLUSIVE= 2,
} hg_lease_mode_t;

/* -------------------------------------------------------------------------- */
/* Hooks: let the leasing server consult raft layer without coupling      */
/* -------------------------------------------------------------------------- */

typedef struct hg_leasing_hooks {
    /* Return true if this node believes it is the raft leader. */
    bool (*is_leader)(void *ctx);

    /* Return leader address string (e.g. "ip:port"), or NULL if unknown. */
    const char *(*leader_addr)(void *ctx);

    /* Submit a raft-backed grant / transition (slow path).
     * Return 0 if accepted for replication, <0 on error.
     *
     * You’ll likely implement this as "append lease grant entry to raft".
     * For now, the leasing server will just reply "forward_to_leader".
     */
    int (*submit_grant)(void *ctx,
                        const char *lease_key,
                        hg_lease_mode_t mode,
                        uint64_t requested_ttl_ms,
                        uint32_t prefer_owner_node_id);

    void *ctx;
} hg_leasing_hooks_t;

/* -------------------------------------------------------------------------- */
/* Public API                                                                 */
/* -------------------------------------------------------------------------- */

typedef struct hg_leasing_config {
    const char *listen_addr;   /* default "0.0.0.0" */
    int listen_port;           /* default env HIVE_LEASE_LISTEN_PORT or 7400 */
    int idle_timeout_ms;       /* close idle connections (default 30000) */
} hg_leasing_config_t;

/* Start/stop the leasing listener thread. */
int  hg_leasing_start(const hg_leasing_config_t *cfg, const hg_leasing_hooks_t *hooks);
void hg_leasing_stop(void);

/* Debug helper: dump lease table to stdout (optional for debug purposes, add define or switch). */
void hg_leasing_debug_dump(void);
