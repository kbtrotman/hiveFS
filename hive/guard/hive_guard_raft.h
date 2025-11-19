/* hive_guard_raft.h */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
#include <string>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Single config used by both the Raft core and the RPC service. */
struct hg_guard_config {
    const char *node_id;      /* e.g. "node-1" or hostname */
    const char *raft_group;   /* e.g. "hive_guard" */
    const char *raft_peers;   /* comma-separated peer list "node-1:8100,node-2:8100,..." */
    const char *raft_data_dir;
    int         listen_port;  /* brpc/braft service port */
};

/* Keep the legacy names around for now so existing callers still compile. */
typedef struct hg_guard_config hg_raft_config;
typedef struct hg_guard_config hg_rpc_server_config;

/* Basic node status as seen locally */
struct hg_guard_status {
    uint64_t cluster_epoch;     /* monotonically increasing cluster generation */
    uint64_t node_epoch;        /* this node's own generation */
    bool     is_leader;         /* true if this node is currently the leader */
    bool     fenced;            /* if true: this node must NOT serve writes */
};

enum GuardOpType : uint8_t {
    GUARD_OP_FENCE_NODE = 1,
    GUARD_OP_BUMP_CLUSTER_EPOCH = 2,
    GUARD_OP_BUMP_NODE_EPOCH = 3,
};

/* Initialize Raft + brpc and join/start the hive_guard group */
int hg_raft_init(const struct hg_guard_config *cfg);

/* Periodic tick (if you want, or you can rely on braft internal timers) */
void hg_raft_poll(void);

/* Query local view of cluster/guard status */
void hg_guard_get_status(struct hg_guard_status *out_status);

/* Propose operations (must be sent to leader only; helper wraps redirect) */
int hg_guard_propose_fence_node(const char *target_node_id, bool fenced);
int hg_guard_propose_bump_cluster_epoch(void);
int hg_guard_propose_bump_node_epoch(const char *target_node_id);

/* Convenience: check if THIS node is currently allowed to serve writes */
bool hg_guard_local_can_write(void);


/* Start the combined Raft + brpc server on this node.
 * Returns 0 on success, <0 on failure.
 */
int hg_rpc_server_start(const struct hg_guard_config *cfg);

/* Stop server (optional; you can also just exit process). */
void hg_rpc_server_stop(void);

/* Client configuration for talking to a HiveGuard leader (or any node) */
struct hg_rpc_client_config {
    const char *target_addr;   /* "ip:port" of a HiveGuardService */
    int         timeout_ms;    /* RPC timeout */
};

/* Initialize a client handle. Returns opaque pointer, or NULL on error. */
typedef void* hg_rpc_client_t;

hg_rpc_client_t hg_rpc_client_create(const struct hg_rpc_client_config *cfg);
void            hg_rpc_client_destroy(hg_rpc_client_t client);

/* Get status from a remote node. Returns 0 on success. */
int hg_rpc_client_get_status(hg_rpc_client_t client,
                             struct hg_guard_status *out_status);

/* Ask the cluster (via the contacted node) to fence/unfence a node.
 * Returns 0 on success.
 */
int hg_rpc_client_fence_node(hg_rpc_client_t client,
                             const char *target_node_id,
                             bool fenced);

/* Bump cluster epoch (leader decides exact value). */
int hg_rpc_client_bump_cluster_epoch(hg_rpc_client_t client);

/* Bump a node's epoch (leader decides exact value). */
int hg_rpc_client_bump_node_epoch(hg_rpc_client_t client,
                                  const char *target_node_id);

                                  
#ifdef __cplusplus
} /* extern "C" */

/* C++-only helpers/types live outside the extern "C" block */
struct GuardOp {
    GuardOpType type;
    std::string node_id;    // empty for cluster-wide ops
    uint64_t    new_epoch;  // optional; 0 = "auto"
};

// Very simple binary encoding/decoding (replace with protobuf if you want)
bool encode_guard_op(const GuardOp &op, std::string *out);
bool decode_guard_op(const std::string &buf, GuardOp *out);
#endif
