// Combined brpc service + server bootstrap for HiveGuard.
#include "hive_guard_raft.h"
#include "hive_guard_raft.pb.h"      // generated from hive_guard_raft.proto
#include <brpc/controller.h>
#include <brpc/server.h>
#include <butil/logging.h>
#include <mutex>

using namespace hiveguard;

namespace {
brpc::Server g_brpc_server;
bool g_server_started = false;
std::mutex g_server_mu;

/* ---- brpc service implementation ---- */
class HiveGuardServiceImpl : public HiveGuardService {
public:
    void GetStatus(::google::protobuf::RpcController* controller,
                   const Empty*,
                   GuardStatus* response,
                   ::google::protobuf::Closure* done) override {
        brpc::ClosureGuard done_guard(done);

        struct hg_guard_status st;
        hg_guard_get_status(&st);

        response->set_cluster_epoch(st.cluster_epoch);
        response->set_node_epoch(st.node_epoch);
        response->set_is_leader(st.is_leader);
        response->set_fenced(st.fenced);
    }

    void FenceNode(::google::protobuf::RpcController* controller,
                   const FenceRequest* request,
                   OpResult* response,
                   ::google::protobuf::Closure* done) override {
        brpc::ClosureGuard done_guard(done);

        const std::string &target = request->target_node_id();
        bool fenced = request->fenced();

        int rc = hg_guard_propose_fence_node(target.c_str(), fenced);
        if (rc == 0) {
            response->set_ok(true);
        } else {
            response->set_ok(false);
            response->set_error_msg("failed to propose fence node");
        }
    }

    void BumpClusterEpoch(::google::protobuf::RpcController* controller,
                          const Empty*,
                          OpResult* response,
                          ::google::protobuf::Closure* done) override {
        brpc::ClosureGuard done_guard(done);
        int rc = hg_guard_propose_bump_cluster_epoch();
        if (rc == 0) {
            response->set_ok(true);
        } else {
            response->set_ok(false);
            response->set_error_msg("failed to bump cluster epoch");
        }
    }

    void BumpNodeEpoch(::google::protobuf::RpcController* controller,
                       const NodeId* request,
                       OpResult* response,
                       ::google::protobuf::Closure* done) override {
        brpc::ClosureGuard done_guard(done);
        const std::string &target = request->node_id();
        int rc = hg_guard_propose_bump_node_epoch(target.c_str());
        if (rc == 0) {
            response->set_ok(true);
        } else {
            response->set_ok(false);
            response->set_error_msg("failed to bump node epoch");
        }
    }
};
} // namespace

extern "C" int hg_rpc_server_start(const struct hg_guard_config *cfg)
{
    if (!cfg || !cfg->node_id || !cfg->raft_group ||
        !cfg->raft_peers || !cfg->raft_data_dir) {
        return -1;
    }

    std::lock_guard<std::mutex> lk(g_server_mu);
    if (g_server_started) {
        return 0;
    }

    // 1) Initialize Raft/guard layer first
    if (hg_raft_init(cfg) != 0) {
        LOG(ERROR) << "hg_raft_init failed";
        return -1;
    }

    // 2) Start brpc server with HiveGuardService
    HiveGuardServiceImpl *svc = new HiveGuardServiceImpl();
    if (g_brpc_server.AddService(svc, brpc::SERVER_OWNS_SERVICE) != 0) {
        LOG(ERROR) << "Failed to add HiveGuardService";
        return -1;
    }

    brpc::ServerOptions options;
    if (g_brpc_server.Start(cfg->listen_port, &options) != 0) {
        LOG(ERROR) << "Failed to start brpc server";
        return -1;
    }

    g_server_started = true;
    return 0;
}

extern "C" void hg_rpc_server_stop(void)
{
    std::lock_guard<std::mutex> lk(g_server_mu);
    if (!g_server_started) return;
    g_brpc_server.Stop(0);
    g_brpc_server.Join();
    g_server_started = false;
}
