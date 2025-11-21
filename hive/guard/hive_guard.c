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

#include "hive_guard.h"
#include "hive_guard_raft.h"


int main(void)
{

	fprintf(stdout, "main: starting\n");

    /* Hard-code or read from config/env for now */
    struct hg_raft_peer peers[] = {
       // { .id = 1, .address = "127.0.0.1:7000" },
       // { .id = 2, .address = "10.0.0.2:7000" },
       // { .id = 3, .address = "10.0.0.3:7000" },
    };

    struct hg_raft_config rcfg = {
        .self_id      = 1,                       /* this node */
        .self_address = "127.0.0.1:7000",
        .data_dir     = "/var/lib/hive_guard/raft",
        .peers        = peers,
        .num_peers    = sizeof(peers)/sizeof(peers[0]),
    };

 //   if (start_raft_server() != 0) {
 //       fprintf(stderr, "Failed to initialize Raft; starting in degraded mode\n");
 //       /* exit or run read-only here */
 //   }

    /* Now run client TCP server loop */	
	return hive_guard_server_main();
}
