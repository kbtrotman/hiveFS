/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */


/**
 * Thin wrapper so the build keeps the historical entry point while the
 * implementation lives in hive_guard_raft.c & hive_guard_tcp_coms.c, two
 * independantly started threads.
 */

#include "hive_guard_raft.h"
#include "hive_guard.h"
#include "hive_guard_sn_tcp.h"
#include "hive_guard_erasure_code.h"

int main(void)
{
    /* TODO: read these from config or env */
    struct hg_raft_peer peers[] = {
        { .id = 1, .address = "127.0.0.1:7000" },
        /* Add other nodes here when you run a real cluster */
    };

    struct hg_raft_config rcfg = {
        .self_id      = 1,
        .self_address = "127.0.0.1:7000",
        .data_dir     = "/tmp/hive_guard_raft",
        .peers        = peers,
        .num_peers    = sizeof(peers) / sizeof(peers[0]),
    };

    fprintf(stderr, "main: starting\n");
    fflush(stderr);

    if (hg_raft_init(&rcfg) != 0) {
        fprintf(stderr, "main: hg_raft_init failed, running without Raft\n");
        /* You can choose to exit here instead: return 1; */
    }
    if (hifs_sn_tcp_start(0, hifs_recv_stripe_from_node) != 0) {
        fprintf(stderr, "main: failed to start stripe listener\n");
    }

    /* Now start the existing epoll-based TCP server.
     * Raft is running in its own thread; both will make progress.
     */
    return hive_guard_server_main();
}
