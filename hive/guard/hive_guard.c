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
 * implementation lives in hive_guard_raft.c & hive_guard_tcp.c, two
 * independantly started threads (yeah, now there's like 4 or 5 threads 
 * at least).
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "hive_guard_raft.h"
#include "hive_guard.h"
#include "hive_guard_stats.h"
#include "hive_guard_sn_tcp.h"
#include "hive_guard_erasure_code.h"
#include "hive_guard_sql.h"
#include "hive_guard_kv.h"
#include "hive_guard_sock.h"
#include "hive_guard_leasing.h"

const char *g_snapshot_dir = HIVE_GUARD_SNAPSHOT_BASE_DIR;
const char *g_mysqldump_path = "/usr/bin/mysqldump";
const char *g_mysql_defaults_file = "/etc/hivefs/mysql-backup.cnf";
const char *g_mysql_db_name = "hive_meta";


/* Check if we need to run bootstrap (unconfigured or pending state).
 * Node ISOs install with an /etc/hivefs/node.json.conf file with
 * several variables set to unconfigured. As they are configured,
 * that file is updated and they are set to pending (resumable if
 * interrupted) and "cpnfigured" when done. This is how we control
 * whether to run the bootstrap process on first launch and what to
 * configure/setup, to automate everything.
 * 
 * NOTE: Everything will try 3 times to configure, and will hard fail
 * after 3 attempts and require manual intervention. If the filesystems
 * are setup right by the user choices and install process, however,
 * this should never happen.
 * 
 * Returns true if we need to run bootstrap, false otherwise.
 */
static bool needs_bootstrap(void)
{
	struct stat st;
	char *buf = NULL;
	size_t len;
	const char *key = "\"cluster_state\"";
	char *p;
	bool ret = false;

	if (stat(HIVE_NODE_CONF_PATH, &st) != 0 || st.st_size <= 0)
		return false;

	len = (size_t)st.st_size;
	buf = malloc(len + 1);
	if (!buf)
		return false;

	FILE *f = fopen(HIVE_NODE_CONF_PATH, "r");
	if (!f) {
		free(buf);
		return false;
	}

	len = fread(buf, 1, len, f);
	fclose(f);
	buf[len] = '\0';

	p = strstr(buf, key);
	if (p) {
		p += strlen(key);
		while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
			++p;
		if (*p == ':') {
			++p;
			while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
				++p;
			if (*p == '"') {
				const char *val = ++p;
				while (*p && *p != '"')
					++p;
				size_t vlen = (size_t)(p - val);
				if ((vlen == strlen("unconfigured") &&
				     strncmp(val, "unconfigured", vlen) == 0) ||
				    (vlen == strlen("pending") &&
				     strncmp(val, "pending", vlen) == 0)) {
					ret = true;
				}
			}
		}
	}

	free(buf);
	return ret;
}

static void maybe_run_bootstrap(void)
{
	if (!needs_bootstrap())
		return;

	pid_t pid = fork();
	if (pid == 0) {
		execlp(HIVE_BOOTSTRAP_BIN, HIVE_BOOTSTRAP_BIN, (char *)NULL);
		_exit(127);
	}
	if (pid < 0) {
		fprintf(stderr, "hive_guard: failed to fork for bootstrap: %s\n",
			strerror(errno));
		return;
	}

	int status;
	if (waitpid(pid, &status, 0) < 0) {
		fprintf(stderr, "hive_guard: waitpid for bootstrap failed: %s\n",
			strerror(errno));
	} else if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		fprintf(stderr, "hive_guard: bootstrap exited abnormally (%d)\n",
			status);
	}
}

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

	maybe_run_bootstrap();

	if (!ensure_directory(HIVE_DATA_DIR) ||
	    !ensure_directory(HIVE_GUARD_RAFT_DIR) ||
	    !ensure_directory(HIVE_GUARD_KV_DIR)) {
		fprintf(stderr, "main: failed to prepare state directories\n");
		return 1;
	}

	// Make sure our keystore is there & ok.
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

	// Init raft protocol.
	if (hg_raft_init(&rcfg) != 0) {
		fprintf(stderr, "main: hg_raft_init failed, exiting without Raft\n");
		hg_kv_shutdown();
		return 1;
	}
	if (dynamic_peers)
		free_peer_buffers(dynamic_peers, peer_addr_bufs, peer_count);


	// Start the data listener for inter-node EC stripe transfers.
	// This tcp thread could also be called our inter-node cluster
	// heartbeat thread, although raft is our heartbeat and this
	// actually just handles general data transfer to/from nodes.
	if (hifs_sn_tcp_start(0, hifs_recv_stripe_from_node) != 0) {
		fprintf(stderr, "main: failed to start data listener\n");
	}
	if (hive_guard_sock_start() != 0) {
		fprintf(stderr,
			"main: failed to start guard control socket listener: %s\n",
			strerror(errno));
	}

	// Start periodic stats flush to the HiveFS meta-database stats.
	// table(s). Uses storage_node_id from above if available as key
	if (storage_node_id == 0)
		storage_node_id = (unsigned int)self_id;
	hg_stats_flush_periodic_start((int)storage_node_id, NULL);

	/* Now start the leasing service for file locking and leasing
	 * in its own outside thread.
	 */
	hg_leasing_config_t lcfg = {0};
	hg_leasing_hooks_t hooks = {0};

	/* Optional: wire to raft later
	hooks.is_leader = my_is_leader_fn;
	hooks.leader_addr = my_leader_addr_fn;
	hooks.submit_grant = my_submit_grant_fn;
	hooks.ctx = ...;
	*/

	hg_leasing_start(&lcfg, &hooks);

	/* Now start the existing epoll-based TCP server.
	 * Raft is running in its own thread; this sends/recieves data
	 * to/from clients and is the only exposed port to the outside.
	 * It also serves as our main loop since we've spawned threads
	 * for the other things going on.
	 */
	ret = hive_guard_server_main();

	// If we recieve a signal, then flush and shutdown cleanly.
	hg_leasing_stop();
	hive_guard_sock_stop();
	hg_stats_flush_periodic_stop();
	hg_kv_shutdown();
	return ret;
}
