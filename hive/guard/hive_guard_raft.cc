// hive_guard_raft.cc
#include "hive_guard_raft.h"
#include "hive_guard_raft_nodestate.h"
#include "hive_guard_raft_statemachine.h"
#include <braft/raft.h>
#include <butil/logging.h>
#include <memory>
#include <atomic>
#include <cstring>

static std::atomic<bool> g_raft_initialized{false};
static std::unique_ptr<braft::Node> g_raft_node;
static GuardStateMachine *g_state_machine = nullptr;

// Simple binary codec: [1-byte type][4-byte node_id len][node_id bytes][8-byte new_epoch LE]
bool encode_guard_op(const GuardOp &op, std::string *out)
{
    if (!out) return false;
    uint32_t name_len = static_cast<uint32_t>(op.node_id.size());
    out->clear();
    out->reserve(1 + 4 + name_len + 8);
    out->push_back(static_cast<char>(op.type));
    out->append(reinterpret_cast<char *>(&name_len), sizeof(name_len));
    if (name_len) {
        out->append(op.node_id.data(), op.node_id.size());
    }
    uint64_t epoch = op.new_epoch;
    out->append(reinterpret_cast<char *>(&epoch), sizeof(epoch));
    return true;
}

bool decode_guard_op(const std::string &buf, GuardOp *out)
{
    if (!out) return false;
    if (buf.size() < 1 + 4 + 8) return false;
    size_t off = 0;
    out->type = static_cast<GuardOpType>(static_cast<unsigned char>(buf[off++]));
    uint32_t name_len = 0;
    memcpy(&name_len, buf.data() + off, sizeof(name_len));
    off += sizeof(name_len);
    if (off + name_len + sizeof(uint64_t) > buf.size()) return false;
    out->node_id.assign(buf.data() + off, buf.data() + off + name_len);
    off += name_len;
    uint64_t epoch = 0;
    memcpy(&epoch, buf.data() + off, sizeof(epoch));
    out->new_epoch = epoch;
    return true;
}

extern "C" int hg_raft_init(const struct hg_guard_config *cfg)
{
    if (g_raft_initialized.exchange(true)) {
        return 0; // already init
    }

    g_local_node_id = cfg->node_id;

    // 1. init logging, etc.
    // google::InitGoogleLogging(cfg->node_id);

    // 2. parse peer list for braft
    braft::PeerId peer_id;
    if (butil::str2endpoint(cfg->node_id, cfg->listen_port, &peer_id.addr) != 0) {
        // log / print error and bail
        return -1;
    }

    braft::Configuration conf;
    conf.parse_from(cfg->raft_peers); // "ip1:port1,ip2:port2" etc.

    // 3. setup state machine
    g_state_machine = new GuardStateMachine();

    braft::NodeOptions node_options;
    node_options.election_timeout_ms = 1000;
    node_options.fsm = g_state_machine;
    node_options.initial_conf = conf;
    node_options.log_uri = std::string("local://") + cfg->raft_data_dir + "/raft_log";
    node_options.raft_meta_uri = std::string("local://") + cfg->raft_data_dir + "/raft_meta";
    node_options.snapshot_uri = std::string("local://") + cfg->raft_data_dir + "/raft_snapshot";

    g_raft_node.reset(new braft::Node(cfg->raft_group, peer_id));
    if (g_raft_node->init(node_options) != 0) {
        LOG(ERROR) << "Fail to init raft node";
        return -1;
    }

    return 0;
}

static int propose_guard_op(const GuardOp &op)
{
    if (!g_raft_node) return -1;

    std::string buf;
    if (!encode_guard_op(op, &buf)) {
        return -1;
    }

    braft::Task task;
    task.data = new butil::IOBuf;
    task.data->append(buf);
    task.done = nullptr; // or set a callback

    // Only the leader should propose; if not leader, you'd normally
    // redirect via brpc to the current leader.
    if (!g_is_leader) {
        // TODO: implement redirect
        return -2;
    }

    g_raft_node->apply(task);
    return 0;
}

extern "C" int hg_guard_propose_fence_node(const char *target_node_id, bool fenced)
{
    GuardOp op;
    op.type = GUARD_OP_FENCE_NODE;
    op.node_id = target_node_id;
    op.new_epoch = 0;

    // If you want "unfence", you can model as a separate op type or
    // add a boolean flag in the op; here we treat 'fenced=false' as UNFENCE.
    if (!fenced) {
        op.type = GUARD_OP_BUMP_NODE_EPOCH; // or define GUARD_OP_UNFENCE_NODE
    }
    return propose_guard_op(op);
}

extern "C" int hg_guard_propose_bump_cluster_epoch(void)
{
    GuardOp op;
    op.type = GUARD_OP_BUMP_CLUSTER_EPOCH;
    op.node_id.clear();
    op.new_epoch = 0;
    return propose_guard_op(op);
}

extern "C" int hg_guard_propose_bump_node_epoch(const char *target_node_id)
{
    GuardOp op;
    op.type = GUARD_OP_BUMP_NODE_EPOCH;
    op.node_id = target_node_id ? target_node_id : "";
    op.new_epoch = 0;
    return propose_guard_op(op);
}

extern "C" void hg_guard_get_status(struct hg_guard_status *out_status)
{
    if (!out_status) return;
    std::lock_guard<std::mutex> lk(g_guard_state_mu);

    out_status->cluster_epoch = g_guard_state.cluster_epoch;
    const NodeGuardState *ns = g_guard_state.get(g_local_node_id);
    out_status->node_epoch = ns ? ns->epoch : 0;
    out_status->is_leader = g_is_leader;
    out_status->fenced = ns ? ns->fenced : false;
}

extern "C" bool hg_guard_local_can_write(void)
{
    struct hg_guard_status st;
    hg_guard_get_status(&st);

    // Simple policy: fenced → no writes.
    // You could also add checks for epoch validity here.
    return !st.fenced;
}
