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
 * implementation lives in hive_guard_tcp_coms.c.
 */

#include "hive_guard_raft.h"

static struct hg_guard_config default_guard_config(void)
{
    return (struct hg_guard_config){
        .node_id       = "node-1",  // replace with hostname/IP
        .raft_group    = "hive_guard",
        .raft_peers    = "node-1:8100,node-2:8100,node-3:8100",
        .raft_data_dir = "/var/lib/hive_guard/raft",
        .listen_port   = 8100,
    };
}

void hive_guard_start_from_config(void)
{
    struct hg_guard_config cfg = default_guard_config();

    if (hg_rpc_server_start(&cfg) != 0) {
        // log and bail or run degraded
    }
}

/* Example: called from hifs_open_file() before allowing write */
bool hive_guard_can_open_for_write(void)
{
    return hg_guard_local_can_write();
}


#include "hive_guard.h"

int main(void)
{
	return hive_guard_server_main();
}
