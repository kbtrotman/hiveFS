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


#include <errno.h>
#include <string.h>
#include <sys/stat.h>

#include "hive_guard_raft.h"
#include "hive_guard.h"
#include "hive_guard_stats.h"
#include "hive_guard_sn_tcp.h"
#include "hive_guard_erasure_code.h"
#include "hive_guard_sql.h"
#include "hive_guard_kv.h"

#define HIVE_GUARD_STATE_ROOT "/var/lib/hivefs"
#define HIVE_GUARD_RAFT_DIR   HIVE_GUARD_STATE_ROOT "/hive_guard_raft"
#define HIVE_GUARD_KV_DIR     HIVE_GUARD_STATE_ROOT "/hive_guard_kv"

static bool ensure_directory(const char *path)
{
	if (!path || !*path)
		return false;
	if (mkdir(path, 0755) == 0)
		return true;
	if (errno == EEXIST)
		return true;
	fprintf(stderr, "hive_guard: failed mkdir(%s): %s\n",
		path, strerror(errno));
	return false;
}

static void free_peer_buffers(struct hg_raft_peer *peers,
				      char **addr_bufs,
				      size_t count)
{
	if (addr_bufs) {
		for (size_t i = 0; i < count; ++i)
			free(addr_bufs[i]);
	}
	free(addr_bufs);
	free(peers);
}

static bool load_peers_from_db(struct hg_raft_peer **out_peers,
				   char ***out_addr_bufs,
				   size_t *out_count,
				   uint64_t *out_self_id,
				   const char **out_self_addr)
{
	const uint16_t default_port = 7000;
	const char *fallback_host = "127.0.0.1";
	size_t node_count = 0;
	const struct hive_storage_node *nodes;
	struct hg_raft_peer *peers;
	char **addr_bufs;

	if (!out_peers || !out_addr_bufs || !out_count ||
	    !out_self_id || !out_self_addr)
		return false;

	init_hive_link();
	if (!hifs_load_storage_nodes())
		return false;

	nodes = hifs_get_storage_nodes(&node_count);
	if (!nodes || node_count == 0)
		return false;
    
	if (!get_host_info()) {
		return false;
	}

	peers = calloc(node_count, sizeof(*peers));
	addr_bufs = calloc(node_count, sizeof(*addr_bufs));
	if (!peers || !addr_bufs) {
		free(peers);
		free(addr_bufs);
		return false;
	}

	for (size_t i = 0; i < node_count; ++i) {
		const char *host = nodes[i].address[0] ? nodes[i].address : fallback_host;
		uint16_t port = nodes[i].guard_port ? nodes[i].guard_port : default_port;
		const char *i_node_uid = nodes[i].uid;
		const char *i_node_serial = nodes[i].serial;
		if (i_node_uid[0] && i_node_serial[0] &&
		    storage_node_uid[0] && storage_node_serial[0] &&
		    strcmp(i_node_uid, storage_node_uid) == 0 &&
		    strcmp(i_node_serial, storage_node_serial) == 0) {
			storage_node_id = nodes[i].id ? nodes[i].id : (uint64_t)(i + 1);
		}
		size_t len = (size_t)snprintf(NULL, 0, "%s:%u", host, port);
		char *addr = malloc(len + 1);
		if (!addr) {
			free_peer_buffers(peers, addr_bufs, node_count);
			return false;
		}
		snprintf(addr, len + 1, "%s:%u", host, port);
		addr_bufs[i] = addr;
		peers[i].id = nodes[i].id ? nodes[i].id : (uint64_t)(i + 1);
		peers[i].address = addr;
	}

	*out_peers = peers;
	*out_addr_bufs = addr_bufs;
	*out_count = node_count;
	*out_self_id = peers[0].id;
	*out_self_addr = peers[0].address;
	return true;
}

int main(void)
{
	struct hg_raft_peer fallback_peers[] = {
		{ .id = 1, .address = "127.0.0.1:7000" },
	};
	struct hg_raft_peer *dynamic_peers = NULL;
	char **peer_addr_bufs = NULL;
	struct hg_raft_peer *peers = fallback_peers;
	size_t peer_count = sizeof(fallback_peers) / sizeof(fallback_peers[0]);
	uint64_t self_id = fallback_peers[0].id;
	const char *self_address = fallback_peers[0].address;
	int ret;

	if (!ensure_directory(HIVE_GUARD_STATE_ROOT) ||
	    !ensure_directory(HIVE_GUARD_RAFT_DIR) ||
	    !ensure_directory(HIVE_GUARD_KV_DIR)) {
		fprintf(stderr, "main: failed to prepare state directories\n");
		return 1;
	}

	if (hg_kv_init(HIVE_GUARD_KV_DIR) != 0) {
		fprintf(stderr, "main: failed to initialize RocksDB at %s\n",
			HIVE_GUARD_KV_DIR);
		return 1;
	}

	if (load_peers_from_db(&dynamic_peers,
			      &peer_addr_bufs,
			      &peer_count,
			      &self_id,
			      &self_address)) {
		peers = dynamic_peers;
	}

	struct hg_raft_config rcfg = {
		.self_id      = self_id,
		.self_address = self_address,
		.data_dir     = HIVE_GUARD_RAFT_DIR,
		.peers        = peers,
		.num_peers    = (unsigned)peer_count,
	};

	fprintf(stderr, "main: starting\n");
	fflush(stderr);

	if (hg_raft_init(&rcfg) != 0) {
		fprintf(stderr, "main: hg_raft_init failed, exiting without Raft\n");
		hg_kv_shutdown();
		return 1;
	}
	if (dynamic_peers)
		free_peer_buffers(dynamic_peers, peer_addr_bufs, peer_count);

	if (hifs_sn_tcp_start(0, hifs_recv_stripe_from_node) != 0) {
		fprintf(stderr, "main: failed to start data listener\n");
	}

	if (storage_node_id == 0)
		storage_node_id = (unsigned int)self_id;
	hg_stats_flush_periodic_start((int)storage_node_id, NULL);

	/* Now start the existing epoll-based TCP server.
	 * Raft is running in its own thread; both will make progress.
	 */
	ret = hive_guard_server_main();
	hg_stats_flush_periodic_stop();
	hg_kv_shutdown();
	return ret;
}
