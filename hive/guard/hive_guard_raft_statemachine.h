#pragma once

#include "hive_guard_raft.h"
#include "hive_guard_raft_nodestate.h"
#include <braft/raft.h>
#include <butil/status.h>

/* State machine that applies GuardOp entries to the in-memory snapshot. */
class GuardStateMachine : public braft::StateMachine {
public:
    void on_apply(braft::Iterator &iter) override;
    void on_leader_start(int64_t term) override;
    void on_leader_stop(const butil::Status &status) override;

private:
    void apply_op(const GuardOp &op);
};
