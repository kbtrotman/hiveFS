// hive_guard_state_machine.cc
#include "hive_guard_raft_statemachine.h"
#include <braft/util.h>
#include <braft/storage.h>
#include <butil/logging.h>
#include <butil/time.h>
#include <mutex>

GuardClusterState g_guard_state;
std::mutex g_guard_state_mu;
std::string g_local_node_id;
bool g_is_leader = false;

void GuardStateMachine::on_apply(braft::Iterator &iter) {
    // This is called on each follower/leader as log entries commit
    for (; iter.valid(); iter.next()) {
        // Iterator provides the serialized data directly.
        const butil::IOBuf &io = iter.data();
        GuardOp op;
        if (!decode_guard_op(io.to_string(), &op)) {
            LOG(ERROR) << "Failed to decode GuardOp";
            continue;
        }

        apply_op(op);
    }
}

void GuardStateMachine::on_leader_start(int64_t term) {
    g_is_leader = true;
    LOG(INFO) << "GuardStateMachine leader start, term=" << term;
}

void GuardStateMachine::on_leader_stop(const butil::Status &status) {
    g_is_leader = false;
    LOG(INFO) << "GuardStateMachine leader stop: " << status;
}

void GuardStateMachine::apply_op(const GuardOp &op) {
    std::lock_guard<std::mutex> lk(g_guard_state_mu);

    switch (op.type) {
    case GUARD_OP_FENCE_NODE: {
        auto &ns = g_guard_state.get_or_create(op.node_id);
        ns.fenced = true;
        // optionally bump epoch or write to DB here
        break;
    }
    case GUARD_OP_BUMP_NODE_EPOCH: {
        auto &ns = g_guard_state.get_or_create(op.node_id);
        if (op.new_epoch > ns.epoch) {
            ns.epoch = op.new_epoch;
        } else {
            ns.epoch += 1;
        }
        break;
    }
    case GUARD_OP_BUMP_CLUSTER_EPOCH: {
        if (op.new_epoch > g_guard_state.cluster_epoch) {
            g_guard_state.cluster_epoch = op.new_epoch;
        } else {
            g_guard_state.cluster_epoch += 1;
        }
        break;
    }
    default:
        break;
    }

    // TODO: persist to MariaDB here:
    // hive_guard_db_apply(op, g_guard_state);
}
