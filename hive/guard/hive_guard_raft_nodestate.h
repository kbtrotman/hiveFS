// hive_guard_raft_nodestate.h
#pragma once
#include <string>
#include <unordered_map>
#include <mutex>
#include <cstdint>

struct NodeGuardState {
    uint64_t epoch = 0;
    bool fenced = false;
};

class GuardClusterState {
public:
    uint64_t cluster_epoch = 0;

    NodeGuardState &get_or_create(const std::string &node_id) {
        return nodes[node_id];
    }

    const NodeGuardState *get(const std::string &node_id) const {
        auto it = nodes.find(node_id);
        return it == nodes.end() ? nullptr : &it->second;
    }

private:
    friend class GuardStateMachine;
    std::unordered_map<std::string, NodeGuardState> nodes;
};

// single global instance on each node:
extern GuardClusterState g_guard_state;
extern std::mutex g_guard_state_mu;
extern std::string g_local_node_id;
extern bool g_is_leader;
