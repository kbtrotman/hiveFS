/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

/**
 * Lightweight TCP helper for sending/receiving EC stripes between nodes.
 * This is evolving intoa fully-fledged inter-cluster communication as 
 * data transfers, encryption, and security is slowly added.
 */

#include "hive_guard_sn_tcp.h"
#include "hive_guard_kv.h"
#include "hive_guard_sql.h"
#include "hive_guard_sock.h"
#include "hive_guard_raft.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <netdb.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define HIFS_STRIPE_TCP_BACKLOG      8

struct stripe_msg_header {
	uint32_t storage_node_id;
	uint32_t shard_id;
	uint64_t estripe_id;
	uint32_t payload_len;
} __attribute__((packed));

static pthread_t sn_thread;
static int sn_listen_fd = -1;
static volatile bool sn_running = false;
static uint16_t sn_port = HIFS_STRIPE_TCP_DEFAULT_PORT;
static hifs_sn_recv_cb sn_recv_cb;
static int tcp_recv_file_on_new_node(int sock_fd);
extern const char *g_snapshot_dir;
extern const char *g_mysqldump_path;
extern const char *g_mysql_defaults_file;
extern const char *g_mysql_db_name;

static uint64_t htonll(uint64_t v)
{
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	return ((uint64_t)htonl(v & 0xFFFFFFFFULL) << 32) | htonl(v >> 32);
#else
	return v;
#endif
}

static uint64_t ntohll(uint64_t v)
{
	return htonll(v);
}

static ssize_t write_full(int fd, const void *buf, size_t len)
{
	size_t total = 0;
	const uint8_t *in = buf;
	while (total < len) {
		ssize_t n = write(fd, in + total, len - total);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		total += (size_t)n;
	}
	return (ssize_t)total;
}

static ssize_t read_full(int fd, void *buf, size_t len)
{
	size_t total = 0;
	uint8_t *out = buf;
	while (total < len) {
		ssize_t n = read(fd, out + total, len - total);
		if (n == 0)
			return total; /* EOF */
		if (n < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		total += (size_t)n;
	}
	return (ssize_t)total;
}

#define SNAPSHOT_XFER_MAGIC 0x53584652u

struct snapshot_xfer_header {
	uint32_t magic;
	uint16_t version;
	uint16_t reserved;
	uint32_t source_len;
	uint32_t path_len;
	uint64_t file_size;
} __attribute__((packed));

#define HG_JOIN_CLUSTER_MAGIC 0x48474A4Eu

struct hg_join_cluster_request_wire {
	uint32_t magic;
	uint16_t version;
	uint16_t reserved;
	uint32_t storage_node_id;
} __attribute__((packed));

struct hg_join_cluster_response_wire {
	uint32_t magic;
	int32_t status;
} __attribute__((packed));

static void handle_join_cluster_request(int fd);
static int hg_send_join_request_to_peer(const struct hive_storage_node *peer,
					unsigned int storage_node_id);
int hg_ask_to_join_cluster(unsigned int storage_node_id);
int hg_acept_request_to_join_cluster(unsigned int storage_node_id);

static int parse_host_port_string(const char *addr,
				  char *host_out,
				  size_t host_len,
				  uint16_t *port_out)
{
	if (!addr || !host_out || host_len == 0 || !port_out)
		return -EINVAL;

	while (isspace((unsigned char)*addr))
		++addr;
	if (*addr == '\0')
		return -EINVAL;

	uint16_t port = HIFS_STRIPE_TCP_DEFAULT_PORT;
	size_t host_copy = 0;

	if (*addr == '[') {
		const char *end = strchr(addr, ']');
		if (!end || end == addr + 1)
			return -EINVAL;
		host_copy = (size_t)(end - addr - 1);
		if (host_copy >= host_len)
			host_copy = host_len - 1;
		memcpy(host_out, addr + 1, host_copy);
		host_out[host_copy] = '\0';
		if (end[1] == ':' && end[2]) {
			char *endptr = NULL;
			unsigned long val = strtoul(end + 2, &endptr, 10);
			if (!endptr || *endptr != '\0' || val == 0 || val > 0xFFFF)
				return -EINVAL;
			port = (uint16_t)val;
		}
	} else {
		const char *last_colon = strrchr(addr, ':');
		bool has_port = false;
		if (last_colon && last_colon[1]) {
			has_port = true;
			for (const char *p = last_colon + 1; *p; ++p) {
				if (!isdigit((unsigned char)*p)) {
					has_port = false;
					break;
				}
			}
		}
		if (has_port) {
			char *endptr = NULL;
			unsigned long val = strtoul(last_colon + 1, &endptr, 10);
			if (!endptr || *endptr != '\0' || val == 0 || val > 0xFFFF)
				return -EINVAL;
			port = (uint16_t)val;
			host_copy = (size_t)(last_colon - addr);
		} else {
			host_copy = strlen(addr);
		}
		if (host_copy == 0)
			return -EINVAL;
		if (host_copy >= host_len)
			host_copy = host_len - 1;
		memcpy(host_out, addr, host_copy);
		host_out[host_copy] = '\0';
	}

	*port_out = port;
	return 0;
}

static bool snapshot_rel_path_invalid(const char *path)
{
	if (!path || !*path)
		return true;

	for (const char *p = path; *p; ++p) {
		if (*p == '\0')
			break;
		if (p[0] == '.' && p[1] == '.') {
			bool at_seg_start = (p == path) || (p[-1] == '/');
			char next = p[2];
			bool at_seg_end = (next == '\0') || (next == '/');
			if (at_seg_start && at_seg_end)
				return true;
		}
	}
	return false;
}

static int snapshot_ids_from_path(const char *path,
				  uint64_t *snap_id,
				  uint64_t *snap_index)
{
	if (!path || !snap_id || !snap_index)
		return -EINVAL;

	unsigned long long sid = 0;
	unsigned long long sidx = 0;

	const char *file = strrchr(path, '/');
	file = file ? file + 1 : path;
	if (sscanf(file, "meta-snap-%llu-idx-%llu.sql", &sid, &sidx) == 2) {
		*snap_id = sid;
		*snap_index = sidx;
		return 0;
	}

	char *dup = strdup(path);
	if (!dup)
		return -ENOMEM;
	char *slash = strrchr(dup, '/');
	if (slash)
		*slash = '\0';
	const char *dir_name = NULL;
	if (slash) {
		char *parent = strrchr(dup, '/');
		dir_name = parent ? parent + 1 : dup;
	} else {
		dir_name = dup;
	}

	int rc = -EINVAL;
	if (dir_name &&
	    sscanf(dir_name, "snap-%llu-idx-%llu", &sid, &sidx) == 2) {
		*snap_id = sid;
		*snap_index = sidx;
		rc = 0;
	}
	free(dup);
	return rc;
}

static int stream_file_over_tcp(int sock_fd, int file_fd, off_t file_size)
{
	if (file_size <= 0)
		return 0;

#if defined(__linux__)
	off_t offset = 0;
	while (offset < file_size) {
		off_t remaining = file_size - offset;
		if (remaining <= 0)
			break;
		const off_t max_chunk = (off_t)(1UL << 30);
		size_t chunk = (size_t)((remaining > max_chunk) ? max_chunk : remaining);
		ssize_t sent = sendfile(sock_fd, file_fd, &offset, chunk);
		if (sent < 0) {
			if (errno == EINTR || errno == EAGAIN)
				continue;
			return -errno;
		}
		if (sent == 0)
			return -EIO;
	}
	return 0;
#else
	uint8_t buf[64 * 1024];
	while (1) {
		ssize_t r = read(file_fd, buf, sizeof(buf));
		if (r < 0) {
			if (errno == EINTR)
				continue;
			return -errno;
		}
		if (r == 0)
			break;
		ssize_t off = 0;
		while (off < r) {
			ssize_t w = write(sock_fd, buf + off, r - off);
			if (w < 0) {
				if (errno == EINTR)
					continue;
				return -errno;
			}
			off += w;
		}
	}
	return 0;
#endif
}

static int connect_to_host_port(const char *host, uint16_t port)
{
	if (!host || !*host)
		host = "127.0.0.1";
	if (!port)
		port = HIFS_STRIPE_TCP_DEFAULT_PORT;

	char port_str[16];
	snprintf(port_str, sizeof(port_str), "%u", port);

	struct addrinfo hints = {
		.ai_family = AF_UNSPEC,
		.ai_socktype = SOCK_STREAM,
	};
	struct addrinfo *res = NULL;
	int rc = getaddrinfo(host, port_str, &hints, &res);
	if (rc != 0) {
		fprintf(stderr, "stripe tcp: getaddrinfo(%s:%s) failed: %s\n",
		        host, port_str, gai_strerror(rc));
		return -1;
	}

	int fd = -1;
	for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
		fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
		if (fd < 0)
			continue;
		if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0)
			break;
		close(fd);
		fd = -1;
	}
	freeaddrinfo(res);
	return fd;
}

static void handle_fetch_request(int fd, const struct stripe_msg_header *req)
{
	if (fd < 0 || !req) {
		if (fd >= 0)
			close(fd);
		return;
	}

	uint8_t *chunk = NULL;
	size_t chunk_len = 0;
	if (hg_kv_get_estripe_chunk(req->estripe_id, &chunk, &chunk_len) != 0) {
		chunk = NULL;
		chunk_len = 0;
	}

	struct stripe_msg_header resp = {
		.storage_node_id = htonl(req->storage_node_id),
		.shard_id = htonl(req->shard_id),
		.estripe_id = htonll(req->estripe_id),
		.payload_len = htonl((uint32_t)chunk_len),
	};

	if (write_full(fd, &resp, sizeof(resp)) != (ssize_t)sizeof(resp)) {
		free(chunk);
		close(fd);
		return;
	}

	if (chunk_len > 0) {
		if (write_full(fd, chunk, chunk_len) != (ssize_t)chunk_len) {
			free(chunk);
			close(fd);
			return;
		}
	}

	free(chunk);
	close(fd);
}

static void handle_join_cluster_request(int fd)
{
	if (fd < 0) {
		return;
	}

	struct hg_join_cluster_request_wire req;
	struct hg_join_cluster_response_wire resp = {
		.magic = htonl(HG_JOIN_CLUSTER_MAGIC),
		.status = htonl(-EPROTO),
	};

	if (read_full(fd, &req, sizeof(req)) != (ssize_t)sizeof(req)) {
		close(fd);
		return;
	}

	if (ntohl(req.magic) != HG_JOIN_CLUSTER_MAGIC ||
	    ntohs(req.version) != 1 ||
	    ntohl(req.storage_node_id) == 0) {
		write_full(fd, &resp, sizeof(resp));
		close(fd);
		return;
	}

	unsigned int node_id = ntohl(req.storage_node_id);
	int rc = -EAGAIN;

	if (hg_guard_local_can_write())
		rc = hg_acept_request_to_join_cluster(node_id);

	resp.status = htonl(rc);
	write_full(fd, &resp, sizeof(resp));
	close(fd);
}

static void *sn_listener_thread(void *arg)
{
	(void)arg;
	while (sn_running) {
		struct sockaddr_storage ss;
		socklen_t slen = sizeof(ss);
		int fd = accept(sn_listen_fd, (struct sockaddr *)&ss, &slen);
		if (fd < 0) {
			if (errno == EINTR)
				continue;
			perror("stripe tcp: accept");
			continue;
		}

		uint32_t peek_magic = 0;
		ssize_t peeked;
		do {
			peeked = recv(fd, &peek_magic, sizeof(peek_magic),
				      MSG_PEEK | MSG_WAITALL);
		} while (peeked < 0 && errno == EINTR);

		if (peeked == (ssize_t)sizeof(peek_magic)) {
			uint32_t magic = ntohl(peek_magic);
			if (magic == SNAPSHOT_XFER_MAGIC) {
				tcp_recv_file_on_new_node(fd);
				continue;
			}
			if (magic == HG_JOIN_CLUSTER_MAGIC) {
				handle_join_cluster_request(fd);
				continue;
			}
		}

		if (peeked != (ssize_t)sizeof(peek_magic)) {
			close(fd);
			continue;
		}

		struct stripe_msg_header hdr;
		if (read_full(fd, &hdr, sizeof(hdr)) != sizeof(hdr)) {
			close(fd);
			continue;
		}

		struct stripe_msg_header local = {
			.storage_node_id = ntohl(hdr.storage_node_id),
			.shard_id        = ntohl(hdr.shard_id),
			.estripe_id      = ntohll(hdr.estripe_id),
			.payload_len     = ntohl(hdr.payload_len),
		};

		if (local.payload_len == 0) {
			handle_fetch_request(fd, &local);
			continue;
		}

		uint8_t *payload = NULL;
		if (local.payload_len) {
			payload = malloc(local.payload_len);
			if (!payload) {
				close(fd);
				continue;
			}
			if (read_full(fd, payload, local.payload_len) !=
			    (ssize_t)local.payload_len) {
				free(payload);
				close(fd);
				continue;
			}
		}
		close(fd);

		if (sn_recv_cb) {
			uint64_t dummy_offset = 0;
			sn_recv_cb(local.storage_node_id,
				   local.shard_id,
				   local.estripe_id,
				   payload,
				   local.payload_len,
				   &dummy_offset);
		}
		free(payload);
	}
	return NULL;
}

int hifs_sn_tcp_start(uint16_t port, hifs_sn_recv_cb cb)
{
	if (sn_running)
		return 0;

	sn_recv_cb = cb;
	sn_port = port ? port : HIFS_STRIPE_TCP_DEFAULT_PORT;

	sn_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (sn_listen_fd < 0) {
		perror("stripe tcp: socket");
		return -1;
	}

	int opt = 1;
	setsockopt(sn_listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	struct sockaddr_in addr = {
		.sin_family = AF_INET,
		.sin_port = htons(sn_port),
		.sin_addr.s_addr = htonl(INADDR_ANY),
	};

	if (bind(sn_listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("stripe tcp: bind");
		close(sn_listen_fd);
		sn_listen_fd = -1;
		return -1;
	}

	if (listen(sn_listen_fd, HIFS_STRIPE_TCP_BACKLOG) < 0) {
		perror("stripe tcp: listen");
		close(sn_listen_fd);
		sn_listen_fd = -1;
		return -1;
	}

	sn_running = true;
	if (pthread_create(&sn_thread, NULL, sn_listener_thread, NULL) != 0) {
		perror("stripe tcp: pthread_create");
		close(sn_listen_fd);
		sn_listen_fd = -1;
		sn_running = false;
		return -1;
	}
	pthread_detach(sn_thread);
	return 0;
}

void hifs_sn_tcp_stop(void)
{
	if (!sn_running)
		return;
	sn_running = false;
	if (sn_listen_fd >= 0) {
		close(sn_listen_fd);
		sn_listen_fd = -1;
	}
}

static int connect_to_target(uint16_t port)
{
	const char *host = getenv("HIFS_STRIPE_TARGET_HOST");
	if (!host || !*host)
		host = "127.0.0.1";

	uint16_t target_port = port;
	const char *port_env = getenv("HIFS_STRIPE_TARGET_PORT");
	if (port_env && *port_env) {
		char *endptr = NULL;
		long val = strtol(port_env, &endptr, 10);
		if (!endptr || *endptr == '\0') {
			if (val > 0 && val <= 0xFFFF)
				target_port = (uint16_t)val;
		}
	}

	return connect_to_host_port(host, target_port);
}

int hifs_sn_tcp_send(uint32_t storage_node_id,
                     uint32_t shard_id,
                     uint64_t estripe_id,
                     const uint8_t *data,
                     uint32_t len)
{
	if (!data || len == 0)
		return -1;

	int fd = connect_to_target(sn_port);
	if (fd < 0)
		return -1;

	struct stripe_msg_header hdr = {
		.storage_node_id = htonl(storage_node_id),
		.shard_id        = htonl(shard_id),
		.estripe_id      = htonll(estripe_id),
		.payload_len     = htonl(len),
	};

	ssize_t n = write(fd, &hdr, sizeof(hdr));
	if (n != (ssize_t)sizeof(hdr)) {
		close(fd);
		return -1;
	}

	size_t total = 0;
	const uint8_t *ptr = data;
	while (total < len) {
		n = write(fd, ptr + total, len - total);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			close(fd);
			return -1;
		}
		total += (size_t)n;
	}

	close(fd);
	return 0;
}

int hifs_sn_tcp_fetch(uint32_t storage_node_id,
			  uint32_t shard_id,
			  const char *host,
			  uint16_t port,
			  uint64_t estripe_id,
			  uint8_t **out_data,
			  size_t *out_len)
{
	if (!out_data || !out_len || estripe_id == 0)
		return -1;

	int fd = connect_to_host_port(host, port);
	if (fd < 0)
		return -1;

	struct stripe_msg_header hdr = {
		.storage_node_id = htonl(storage_node_id),
		.shard_id        = htonl(shard_id),
		.estripe_id      = htonll(estripe_id),
		.payload_len     = htonl(0),
	};

	if (write_full(fd, &hdr, sizeof(hdr)) != (ssize_t)sizeof(hdr)) {
		close(fd);
		return -1;
	}

	if (read_full(fd, &hdr, sizeof(hdr)) != (ssize_t)sizeof(hdr)) {
		close(fd);
		return -1;
	}

	uint32_t payload_len = ntohl(hdr.payload_len);
	if (payload_len == 0) {
		close(fd);
		return -1;
	}

	uint8_t *buf = malloc(payload_len);
	if (!buf) {
		close(fd);
		return -1;
	}

	if (read_full(fd, buf, payload_len) != (ssize_t)payload_len) {
		free(buf);
		close(fd);
		return -1;
	}

	close(fd);
	*out_data = buf;
	*out_len = payload_len;
	return 0;
}

int tcp_send_file_to_new_node(const char *source_addr,
			      const char *local_path,
			      const char *new_node_addr)
{
	if (!local_path || !*local_path || !new_node_addr || !*new_node_addr)
		return -EINVAL;

	struct stat st;
	if (stat(local_path, &st) != 0)
		return -errno;
	if (!S_ISREG(st.st_mode))
		return -EINVAL;

	int file_fd = open(local_path, O_RDONLY);
	if (file_fd < 0)
		return -errno;

	char host[256];
	uint16_t port = 0;
	int rc = parse_host_port_string(new_node_addr, host, sizeof(host), &port);
	if (rc != 0) {
		close(file_fd);
		return rc;
	}

	int sock = connect_to_host_port(host, port);
	if (sock < 0) {
		int err = errno ? -errno : -ECONNREFUSED;
		close(file_fd);
		return err;
	}

	const char *src = (source_addr && *source_addr) ? source_addr : "";
	size_t src_len = strlen(src);
	size_t path_len = strlen(local_path);
	if (src_len > UINT32_MAX || path_len > UINT32_MAX) {
		close(sock);
		close(file_fd);
		return -EOVERFLOW;
	}

	struct snapshot_xfer_header hdr = {
		.magic = htonl(SNAPSHOT_XFER_MAGIC),
		.version = htons(1),
		.reserved = 0,
		.source_len = htonl((uint32_t)src_len),
		.path_len = htonl((uint32_t)path_len),
		.file_size = htonll((uint64_t)st.st_size),
	};

	if (write_full(sock, &hdr, sizeof(hdr)) != (ssize_t)sizeof(hdr)) {
		rc = -EIO;
		goto done;
	}

	if (src_len &&
	    write_full(sock, src, src_len) != (ssize_t)src_len) {
		rc = -EIO;
		goto done;
	}

	if (path_len &&
	    write_full(sock, local_path, path_len) != (ssize_t)path_len) {
		rc = -EIO;
		goto done;
	}

	rc = stream_file_over_tcp(sock, file_fd, st.st_size);

done:
	close(sock);
	close(file_fd);
	return rc;
}

static int tcp_recv_file_on_new_node(int sock_fd)
{
	if (sock_fd < 0)
		return -EINVAL;

	int rc = 0;
	struct snapshot_xfer_header wire;
	char *source = NULL;
	char *path = NULL;
	char *final_path = NULL;
	char *snap_dir = NULL;
	int file_fd = -1;
	bool snap_dir_created = false;
	bool final_ready = false;
	char tmp_template[PATH_MAX];
	bool tmp_valid = false;

	ssize_t n = read_full(sock_fd, &wire, sizeof(wire));
	if (n != (ssize_t)sizeof(wire)) {
		rc = -EIO;
		goto done;
	}

	if (ntohl(wire.magic) != SNAPSHOT_XFER_MAGIC ||
	    ntohs(wire.version) != 1) {
		rc = -EPROTO;
		goto done;
	}

	uint32_t source_len = ntohl(wire.source_len);
	uint32_t path_len = ntohl(wire.path_len);
	uint64_t file_size = ntohll(wire.file_size);

	if (source_len) {
		source = malloc((size_t)source_len + 1);
		if (!source) {
			rc = -ENOMEM;
			goto done;
		}
		if (read_full(sock_fd, source, source_len) != (ssize_t)source_len) {
			rc = -EIO;
			goto done;
		}
		source[source_len] = '\0';
	}

	if (path_len) {
		path = malloc((size_t)path_len + 1);
		if (!path) {
			rc = -ENOMEM;
			goto done;
		}
		if (read_full(sock_fd, path, path_len) != (ssize_t)path_len) {
			rc = -EIO;
			goto done;
		}
		path[path_len] = '\0';
	}

	const char *base_dir = (g_snapshot_dir && *g_snapshot_dir)
				       ? g_snapshot_dir
				       : HIVE_GUARD_SNAPSHOT_BASE_DIR;
	if (mkdir(base_dir, 0755) != 0 && errno != EEXIST) {
		rc = -errno;
		hifs_warning("tcp_recv_file_on_new_node: unable to create base_dir=%s rc=%d",
			     base_dir,
			     rc);
		goto done;
	}

	if (!path || !*path) {
		rc = -EINVAL;
		goto done;
	}
	size_t base_len = strlen(base_dir);
	const char *relative = path;
	if (path[0] == '/') {
		if (base_len == 0 ||
		    strncmp(path, base_dir, base_len) != 0 ||
		    (path[base_len] != '/' && path[base_len] != '\0')) {
			hifs_warning("tcp_recv_file_on_new_node: incoming path %s not under %s",
				     path,
				     base_dir);
			rc = -EINVAL;
			goto done;
		}
		relative = path + base_len;
		if (*relative == '/')
			++relative;
	}
	if (!relative || !*relative || snapshot_rel_path_invalid(relative)) {
		rc = -EINVAL;
		goto done;
	}

	size_t rel_len = strlen(relative);
	bool base_has_sep = base_len > 0 && base_dir[base_len - 1] == '/';
	size_t final_len = base_len + (base_has_sep ? 0 : 1) + rel_len + 1;
	final_path = malloc(final_len);
	if (!final_path) {
		rc = -ENOMEM;
		goto done;
	}
	size_t pos = 0;
	memcpy(final_path + pos, base_dir, base_len);
	pos += base_len;
	if (!base_has_sep)
		final_path[pos++] = '/';
	memcpy(final_path + pos, relative, rel_len);
	pos += rel_len;
	final_path[pos] = '\0';

	snap_dir = strdup(final_path);
	if (!snap_dir) {
		rc = -ENOMEM;
		goto done;
	}
	char *last_slash = strrchr(snap_dir, '/');
	if (!last_slash || last_slash == snap_dir) {
		rc = -EINVAL;
		goto done;
	}
	*last_slash = '\0';

	if (mkdir(snap_dir, 0700) != 0) {
		rc = (errno == EEXIST) ? -EEXIST : -errno;
		hifs_warning("tcp_recv_file_on_new_node: mkdir %s failed rc=%d",
			     snap_dir,
			     rc);
		goto done;
	}
	snap_dir_created = true;

	int written = snprintf(tmp_template,
			       sizeof(tmp_template),
			       "%s/.snap-incomingXXXXXX",
			       snap_dir);
	if (written < 0 || (size_t)written >= sizeof(tmp_template)) {
		rc = -ENAMETOOLONG;
		goto done;
	}

	file_fd = mkstemp(tmp_template);
	if (file_fd < 0) {
		rc = -errno;
		goto done;
	}
	tmp_valid = true;

	hifs_notice("tcp_recv_file_on_new_node: receiving %llu bytes from %s into %s",
		    (unsigned long long)file_size,
		    source ? source : "(unknown)",
		    final_path);

	uint8_t buf[256 * 1024];
	uint64_t remaining = file_size;
	while (remaining > 0) {
		size_t chunk = remaining > sizeof(buf) ? sizeof(buf) : (size_t)remaining;
		ssize_t got = read_full(sock_fd, buf, chunk);
		if (got != (ssize_t)chunk) {
			rc = (got < 0) ? -errno : -EIO;
			goto done;
		}
		if (write_full(file_fd, buf, chunk) != (ssize_t)chunk) {
			rc = -errno;
			goto done;
		}
		remaining -= chunk;
	}

	close(file_fd);
	file_fd = -1;
	if (rename(tmp_template, final_path) != 0) {
		rc = -errno;
		goto done;
	}
	tmp_valid = false;
	final_ready = true;

	uint64_t snap_id = 0;
	uint64_t snap_index = 0;
	rc = snapshot_ids_from_path(final_path, &snap_id, &snap_index);
	if (rc != 0) {
		hifs_warning("tcp_recv_file_on_new_node: unable to decode snap ids from %s rc=%d",
			     final_path,
			     rc);
		goto done;
	}

	char restored_path[PATH_MAX];
	rc = hg_sql_snapshot_restore_artifact(base_dir,
					      g_mysqldump_path,
					      g_mysql_defaults_file,
					      g_mysql_db_name,
					      snap_id,
					      snap_index,
					      restored_path,
					      sizeof(restored_path));
	if (rc != 0) {
		hifs_warning("tcp_recv_file_on_new_node: restore failed snap_id=%llu index=%llu rc=%d",
			     (unsigned long long)snap_id,
			     (unsigned long long)snap_index,
			     rc);
		goto done;
	}

	hifs_notice("tcp_recv_file_on_new_node: restored snap_id=%llu index=%llu from %s",
		    (unsigned long long)snap_id,
		    (unsigned long long)snap_index,
		    final_path);
	
	hg_ask_to_join_cluster(storage_node_id);

done:
	if (file_fd >= 0)
		close(file_fd);
	if (tmp_valid)
		unlink(tmp_template);
	if (!final_ready && snap_dir_created && snap_dir)
		rmdir(snap_dir);
	free(snap_dir);
	free(final_path);
	free(source);
	free(path);
	close(sock_fd);
	return rc;
}

static int hg_send_join_request_to_peer(const struct hive_storage_node *peer,
					unsigned int storage_node_id)
{
	if (!peer || storage_node_id == 0)
		return -EINVAL;

	char host[256];
	bool have_host = false;
	uint16_t port = peer->stripe_port
		? peer->stripe_port
		: HIFS_STRIPE_TCP_DEFAULT_PORT;

	if (peer->address[0]) {
		uint16_t parsed_port = port;
		if (parse_host_port_string(peer->address,
					   host,
					   sizeof(host),
					   &parsed_port) == 0) {
			if (parsed_port)
				port = parsed_port;
			have_host = true;
		}
	}
	if (!have_host) {
		const char *fallback = peer->address[0]
			? peer->address
			: "127.0.0.1";
		snprintf(host, sizeof(host), "%s", fallback);
	}

	int fd = connect_to_host_port(host, port);
	if (fd < 0)
		return -ECONNREFUSED;

	struct hg_join_cluster_request_wire req = {
		.magic = htonl(HG_JOIN_CLUSTER_MAGIC),
		.version = htons(1),
		.storage_node_id = htonl(storage_node_id),
	};

	if (write_full(fd, &req, sizeof(req)) != (ssize_t)sizeof(req)) {
		close(fd);
		return -EIO;
	}

	struct hg_join_cluster_response_wire resp;
	if (read_full(fd, &resp, sizeof(resp)) != (ssize_t)sizeof(resp)) {
		close(fd);
		return -EIO;
	}
	close(fd);

	if (ntohl(resp.magic) != HG_JOIN_CLUSTER_MAGIC)
		return -EPROTO;

	uint32_t raw = ntohl((uint32_t)resp.status);
	return (int)((int32_t)raw);
}

static const struct hive_storage_node *
hg_find_storage_node(unsigned int storage_node_id)
{
	size_t count = 0;
	const struct hive_storage_node *nodes = hifs_get_storage_nodes(&count);

	if ((!nodes || count == 0) && hifs_load_storage_nodes())
		nodes = hifs_get_storage_nodes(&count);
	if (!nodes || count == 0)
		return NULL;
	for (size_t i = 0; i < count; ++i) {
		if (nodes[i].id == storage_node_id)
			return &nodes[i];
	}
	return NULL;
}

int hg_ask_to_join_cluster(unsigned int storage_node_id)
{
	if (storage_node_id == 0)
		return -EINVAL;

	if (hg_guard_local_can_write())
		return hg_acept_request_to_join_cluster(storage_node_id);

	size_t count = 0;
	const struct hive_storage_node *nodes = hifs_get_storage_nodes(&count);
	if ((!nodes || count == 0) && hifs_load_storage_nodes())
		nodes = hifs_get_storage_nodes(&count);
	if (!nodes || count == 0)
		return -ENOENT;

	int rc = -EHOSTUNREACH;
	for (size_t i = 0; i < count; ++i) {
		if (nodes[i].id == storage_node_id)
			continue;
		rc = hg_send_join_request_to_peer(&nodes[i], storage_node_id);
		if (rc == -EAGAIN)
			continue;
		if (rc == 0)
			return 0;
	}
	return rc;
}

int hg_acept_request_to_join_cluster(unsigned int storage_node_id)
{
	if (storage_node_id == 0)
		return -EINVAL;
	if (!hg_guard_local_can_write())
		return -EAGAIN;

	const struct hive_storage_node *node =
		hg_find_storage_node(storage_node_id);
	if (!node)
		return -ENOENT;

	struct hive_guard_storage_update_cmd cmd = {
		.cluster_id = 0,
		.node_id = storage_node_id,
		.node = *node,
		.hive_version = node->hive_version,
		.hive_patch_level = node->hive_patch_level,
	};

	hifs_notice("join request accepted for storage node %u",
		    storage_node_id);
	int rc = hifs_raft_submit_storage_update(&cmd);
	if (rc != 0) {
		hifs_warning("failed to submit storage node %u join rc=%d",
			     storage_node_id,
			     rc);
	}
	return rc;
}
