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
#include "hive_guard_auth.h"
#include "../common/hive_common.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <netdb.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#define OPENSSL_SUPPRESS_DEPRECATED
#include <openssl/sha.h>

#define HIFS_STRIPE_TCP_BACKLOG      8

struct stripe_msg_header {
	uint32_t storage_node_id;
	uint32_t shard_id;
	uint64_t estripe_id;
	uint32_t payload_len;
} __attribute__((packed));

#define HG_TOKEN_METADATA_MAGIC 0x48544D44u
#define HG_TOKEN_METADATA_VERSION 1
#define HG_TOKEN_METADATA_CMD_STORE 1
#define HG_TOKEN_METADATA_TIMEOUT_MS 3000

struct hg_token_metadata_header {
	uint32_t magic;
	uint16_t version;
	uint16_t command;
	uint32_t payload_len;
} __attribute__((packed));

struct hg_token_metadata_payload {
	uint64_t cluster_id_be;
	uint8_t has_cluster_id;
	uint8_t reserved[7];
	char bootstrap_token[HIVE_GUARD_BOOTSTRAP_TOKEN_LEN];
	char first_boot_ts[HIVE_GUARD_TOKEN_TS_LEN];
	char tid[HIVE_GUARD_UUID_STR_LEN];
	char token_type[HIVE_GUARD_TOKEN_TYPE_LEN];
	char host_uuid[HIVE_GUARD_UID_LEN];
	char host_mid[HIVE_GUARD_UID_LEN];
	char issued_at[HIVE_GUARD_TOKEN_TS_LEN];
	char expires_at[HIVE_GUARD_TOKEN_TS_LEN];
	char approved_at[HIVE_GUARD_TOKEN_TS_LEN];
	char approved_by[HIVE_USER_ID_LEN];
} __attribute__((packed));

struct hg_token_metadata_response {
	uint32_t magic;
	int32_t status;
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

static void hg_token_metadata_payload_from_meta(
	struct hg_token_metadata_payload *dst,
	const struct hive_guard_token_metadata *src)
{
	if (!dst) {
		return;
	}
	memset(dst, 0, sizeof(*dst));
	if (!src) {
		return;
	}
	dst->cluster_id_be = htonll(src->cluster_id);
	dst->has_cluster_id = src->has_cluster_id ? 1 : 0;
	memcpy(dst->bootstrap_token,
	       src->bootstrap_token,
	       sizeof(dst->bootstrap_token));
	memcpy(dst->first_boot_ts,
	       src->first_boot_ts,
	       sizeof(dst->first_boot_ts));
	memcpy(dst->tid, src->tid, sizeof(dst->tid));
	memcpy(dst->token_type, src->token_type, sizeof(dst->token_type));
	memcpy(dst->host_uuid, src->host_uuid, sizeof(dst->host_uuid));
	memcpy(dst->host_mid, src->host_mid, sizeof(dst->host_mid));
	memcpy(dst->issued_at, src->issued_at, sizeof(dst->issued_at));
	memcpy(dst->expires_at, src->expires_at, sizeof(dst->expires_at));
	memcpy(dst->approved_at, src->approved_at, sizeof(dst->approved_at));
	memcpy(dst->approved_by, src->approved_by, sizeof(dst->approved_by));
}

static void hg_token_metadata_payload_to_meta(
	struct hive_guard_token_metadata *dst,
	const struct hg_token_metadata_payload *src)
{
	if (!dst || !src) {
		return;
	}
	memset(dst, 0, sizeof(*dst));
	dst->cluster_id = ntohll(src->cluster_id_be);
	dst->has_cluster_id = src->has_cluster_id ? true : false;
	memcpy(dst->bootstrap_token,
	       src->bootstrap_token,
	       sizeof(dst->bootstrap_token));
	dst->bootstrap_token[sizeof(dst->bootstrap_token) - 1] = '\0';
	memcpy(dst->first_boot_ts,
	       src->first_boot_ts,
	       sizeof(dst->first_boot_ts));
	dst->first_boot_ts[sizeof(dst->first_boot_ts) - 1] = '\0';
	memcpy(dst->tid, src->tid, sizeof(dst->tid));
	dst->tid[sizeof(dst->tid) - 1] = '\0';
	memcpy(dst->token_type, src->token_type, sizeof(dst->token_type));
	dst->token_type[sizeof(dst->token_type) - 1] = '\0';
	memcpy(dst->host_uuid, src->host_uuid, sizeof(dst->host_uuid));
	dst->host_uuid[sizeof(dst->host_uuid) - 1] = '\0';
	memcpy(dst->host_mid, src->host_mid, sizeof(dst->host_mid));
	dst->host_mid[sizeof(dst->host_mid) - 1] = '\0';
	memcpy(dst->issued_at, src->issued_at, sizeof(dst->issued_at));
	dst->issued_at[sizeof(dst->issued_at) - 1] = '\0';
	memcpy(dst->expires_at, src->expires_at, sizeof(dst->expires_at));
	dst->expires_at[sizeof(dst->expires_at) - 1] = '\0';
	memcpy(dst->approved_at, src->approved_at, sizeof(dst->approved_at));
	dst->approved_at[sizeof(dst->approved_at) - 1] = '\0';
	memcpy(dst->approved_by, src->approved_by, sizeof(dst->approved_by));
	dst->approved_by[sizeof(dst->approved_by) - 1] = '\0';
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

static int hg_wait_for_fd(int fd, short events, int timeout_ms)
{
	struct pollfd pfd = {
		.fd = fd,
		.events = events,
	};
	for (;;) {
		int rc = poll(&pfd, 1, timeout_ms);
		if (rc < 0) {
			if (errno == EINTR)
				continue;
			int err = errno;
			return err ? -err : -EIO;
		}
		if (rc == 0)
			return -ETIMEDOUT;
		if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))
			return -EIO;
		return 0;
	}
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
#define HG_JOIN_CLUSTER_FLAG_NEW_NODE    0x1u
#define HG_JOIN_CLUSTER_FLAG_LEARNER     0x2u
#define HG_JOIN_CLUSTER_TIMEOUT_MS       3000
#define HG_JOIN_CLUSTER_MAX_ATTEMPTS     3

struct hg_join_cluster_request_wire {
	uint32_t magic;
	uint16_t version;
	uint16_t reserved;
	uint32_t storage_node_id;
	uint32_t flags;
} __attribute__((packed));

struct hg_join_cluster_response_wire {
	uint32_t magic;
	int32_t status;
} __attribute__((packed));

static void handle_join_cluster_request(int fd);
static int hg_send_join_request_to_peer(const struct hive_storage_node *peer,
					unsigned int storage_node_id,
					uint32_t join_flags);
int hg_ask_to_join_cluster(unsigned int storage_node_id, uint32_t join_flags);
int hg_acept_request_to_join_cluster(unsigned int storage_node_id,
				     uint32_t join_flags);

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

struct snapshot_meta_record {
	char relative_path[PATH_MAX];
	char sha256[SHA256_DIGEST_LENGTH * 2 + 1];
	uint64_t snap_id;
	uint64_t raft_index;
};

static const char *snapshot_meta_filename(void)
{
	const char *slash = strrchr(HIVE_GUARD_SNAPSHOT_META_FILE, '/');
	return slash ? slash + 1 : HIVE_GUARD_SNAPSHOT_META_FILE;
}

static int snapshot_meta_path_from_base(const char *base_dir,
					char *out_path,
					size_t out_len)
{
	if (!out_path || out_len == 0)
		return -EINVAL;
	const char *dir = base_dir ? base_dir : "";
	size_t dir_len = strlen(dir);
	bool has_sep = dir_len > 0 && dir[dir_len - 1] == '/';
	int written = snprintf(out_path,
			       out_len,
			       "%s%s%s",
			       dir,
			       has_sep ? "" : "/",
			       snapshot_meta_filename());
	if (written < 0 || (size_t)written >= out_len)
		return -ENAMETOOLONG;
	return 0;
}

static void snapshot_digest_to_hex(const uint8_t *in, size_t len, char *out)
{
	static const char hex[] = "0123456789abcdef";
	for (size_t i = 0; i < len; ++i) {
		out[2 * i] = hex[(in[i] >> 4) & 0x0F];
		out[2 * i + 1] = hex[in[i] & 0x0F];
	}
	out[2 * len] = '\0';
}

static int snapshot_sha256_file_hex(const char *path, char *out_hex, size_t out_len)
{
	if (!path || !out_hex || out_len < (SHA256_DIGEST_LENGTH * 2 + 1))
		return -EINVAL;

	FILE *fp = fopen(path, "rb");
	if (!fp)
		return -errno;

	SHA256_CTX ctx;
	if (SHA256_Init(&ctx) != 1) {
		fclose(fp);
		return -EIO;
	}

	unsigned char buf[256 * 1024];
	size_t n = 0;
	while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
		if (SHA256_Update(&ctx, buf, n) != 1) {
			fclose(fp);
			return -EIO;
		}
	}
	if (ferror(fp)) {
		int err = -errno;
		fclose(fp);
		return err;
	}
	fclose(fp);

	unsigned char digest[SHA256_DIGEST_LENGTH];
	if (SHA256_Final(digest, &ctx) != 1)
		return -EIO;
	snapshot_digest_to_hex(digest, SHA256_DIGEST_LENGTH, out_hex);
	return 0;
}

static int snapshot_meta_parse_string(const char *json,
				      const char *key,
				      char *out,
				      size_t out_len)
{
	if (!json || !key || !out || out_len == 0)
		return -EINVAL;

	char needle[64];
	int written = snprintf(needle, sizeof(needle), "\"%s\":\"", key);
	if (written < 0 || (size_t)written >= sizeof(needle))
		return -EINVAL;

	const char *start = strstr(json, needle);
	if (!start)
		return -EINVAL;
	start += written;
	const char *end = strchr(start, '"');
	if (!end)
		return -EINVAL;
	size_t len = (size_t)(end - start);
	if (len >= out_len)
		return -ENAMETOOLONG;
	memcpy(out, start, len);
	out[len] = '\0';
	return 0;
}

static int snapshot_meta_parse_u64(const char *json,
				   const char *key,
				   uint64_t *out_val)
{
	if (!json || !key || !out_val)
		return -EINVAL;

	char needle[64];
	int written = snprintf(needle, sizeof(needle), "\"%s\":", key);
	if (written < 0 || (size_t)written >= sizeof(needle))
		return -EINVAL;

	const char *start = strstr(json, needle);
	if (!start)
		return -EINVAL;
	start += written;

	errno = 0;
	char *endptr = NULL;
	unsigned long long val = strtoull(start, &endptr, 10);
	if (start == endptr || errno != 0)
		return -EINVAL;
	*out_val = (uint64_t)val;
	return 0;
}

static int snapshot_meta_load(const char *base_dir,
			      struct snapshot_meta_record *out)
{
	if (!out)
		return -EINVAL;

	char meta_path[PATH_MAX];
	int rc = snapshot_meta_path_from_base(base_dir, meta_path, sizeof(meta_path));
	if (rc != 0)
		return rc;

	FILE *fp = fopen(meta_path, "rb");
	if (!fp)
		return -errno;

	char buf[2048];
	size_t total = fread(buf, 1, sizeof(buf) - 1, fp);
	if (ferror(fp)) {
		rc = -errno;
		fclose(fp);
		return rc;
	}
	int c = fgetc(fp);
	if (c != EOF) {
		fclose(fp);
		return -EOVERFLOW;
	}
	buf[total] = '\0';
	fclose(fp);

	rc = snapshot_meta_parse_string(buf, "relative_path", out->relative_path, sizeof(out->relative_path));
	if (rc != 0)
		return rc;
	rc = snapshot_meta_parse_string(buf, "sha256", out->sha256, sizeof(out->sha256));
	if (rc != 0)
		return rc;
	if (strlen(out->sha256) != SHA256_DIGEST_LENGTH * 2)
		return -EINVAL;
	rc = snapshot_meta_parse_u64(buf, "snapshot_id", &out->snap_id);
	if (rc != 0)
		return rc;
	return snapshot_meta_parse_u64(buf, "raft_index", &out->raft_index);
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

static int hg_handle_token_metadata_locally(
	uint16_t command,
	const struct hive_guard_token_metadata *meta)
{
	if (!meta)
		return -EINVAL;
	if (command != HG_TOKEN_METADATA_CMD_STORE)
		return -ENOTSUP;
	return hg_raft_call_update_token(meta);
}

static void handle_token_metadata_request(int fd)
{
	if (fd < 0)
		return;

	struct hg_token_metadata_header hdr;
	struct hg_token_metadata_response resp = {
		.magic = htonl(HG_TOKEN_METADATA_MAGIC),
		.status = htonl(-EPROTO),
	};

	if (read_full(fd, &hdr, sizeof(hdr)) != (ssize_t)sizeof(hdr)) {
		close(fd);
		return;
	}

	uint32_t magic = ntohl(hdr.magic);
	uint16_t version = ntohs(hdr.version);
	uint32_t payload_len = ntohl(hdr.payload_len);
	uint16_t command = ntohs(hdr.command);

	if (magic != HG_TOKEN_METADATA_MAGIC ||
	    version != HG_TOKEN_METADATA_VERSION ||
	    payload_len != sizeof(struct hg_token_metadata_payload)) {
		write_full(fd, &resp, sizeof(resp));
		close(fd);
		return;
	}

	struct hg_token_metadata_payload payload;
	if (read_full(fd, &payload, sizeof(payload)) !=
	    (ssize_t)sizeof(payload)) {
		close(fd);
		return;
	}

	struct hive_guard_token_metadata meta;
	hg_token_metadata_payload_to_meta(&meta, &payload);

	int rc = -EAGAIN;
	if (hg_guard_local_can_write())
		rc = hg_handle_token_metadata_locally(command, &meta);

	resp.status = htonl((uint32_t)rc);
	write_full(fd, &resp, sizeof(resp));
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

	uint32_t join_flags = ntohl(req.flags);
	if (hg_guard_local_can_write())
		rc = hg_acept_request_to_join_cluster(node_id, join_flags);

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
			if (magic == HG_TOKEN_METADATA_MAGIC) {
				handle_token_metadata_request(fd);
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

	const char *meta_name = snapshot_meta_filename();
	bool is_meta_file = (relative && strcmp(relative, meta_name) == 0);

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

	mode_t dir_mode = is_meta_file ? 0755 : 0700;
	if (mkdir(snap_dir, dir_mode) != 0) {
		if (!(is_meta_file && errno == EEXIST)) {
			rc = (errno == EEXIST) ? -EEXIST : -errno;
			hifs_warning("tcp_recv_file_on_new_node: mkdir %s failed rc=%d",
				     snap_dir,
				     rc);
			goto done;
		}
	} else if (!is_meta_file) {
		snap_dir_created = true;
	}

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

	if (is_meta_file) {
		hifs_notice("tcp_recv_file_on_new_node: received snapshot metadata from %s into %s",
			    source ? source : "(unknown)",
			    final_path);
		goto done;
	}

	uint64_t snap_id = 0;
	uint64_t snap_index = 0;
	rc = snapshot_ids_from_path(final_path, &snap_id, &snap_index);
	if (rc != 0) {
		hifs_warning("tcp_recv_file_on_new_node: unable to decode snap ids from %s rc=%d",
			     final_path,
			     rc);
		goto done;
	}

	struct snapshot_meta_record meta = {0};
	rc = snapshot_meta_load(base_dir, &meta);
	if (rc != 0) {
		hifs_warning("tcp_recv_file_on_new_node: missing snapshot metadata rc=%d",
			     rc);
		unlink(final_path);
		final_ready = false;
		goto done;
	}
	if (strcmp(meta.relative_path, relative) != 0 ||
	    meta.snap_id != snap_id ||
	    meta.raft_index != snap_index) {
		hifs_warning("tcp_recv_file_on_new_node: metadata mismatch rel=%s meta_rel=%s snap_id=%llu meta_id=%llu idx=%llu meta_idx=%llu",
			     relative,
			     meta.relative_path,
			     (unsigned long long)snap_id,
			     (unsigned long long)meta.snap_id,
			     (unsigned long long)snap_index,
			     (unsigned long long)meta.raft_index);
		rc = -EINVAL;
		unlink(final_path);
		final_ready = false;
		goto done;
	}

	char calc_sha[SHA256_DIGEST_LENGTH * 2 + 1];
	rc = snapshot_sha256_file_hex(final_path, calc_sha, sizeof(calc_sha));
	if (rc != 0) {
		hifs_warning("tcp_recv_file_on_new_node: unable to checksum %s rc=%d",
			     final_path,
			     rc);
		unlink(final_path);
		final_ready = false;
		goto done;
	}
	if (strcmp(calc_sha, meta.sha256) != 0) {
		hifs_warning("tcp_recv_file_on_new_node: checksum mismatch for %s expected=%s got=%s",
			     final_path,
			     meta.sha256,
			     calc_sha);
		rc = -EBADMSG;
		unlink(final_path);
		final_ready = false;
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
	
	hg_ask_to_join_cluster(storage_node_id,
			       HG_JOIN_CLUSTER_FLAG_NEW_NODE |
			       HG_JOIN_CLUSTER_FLAG_LEARNER);

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
					unsigned int storage_node_id,
					uint32_t join_flags)
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

	struct hg_join_cluster_request_wire req = {
		.magic = htonl(HG_JOIN_CLUSTER_MAGIC),
		.version = htons(1),
		.storage_node_id = htonl(storage_node_id),
		.flags = htonl(join_flags),
	};

	for (int attempt = 0; attempt < HG_JOIN_CLUSTER_MAX_ATTEMPTS; ++attempt) {
		int fd = connect_to_host_port(host, port);
		if (fd < 0)
			return -ECONNREFUSED;

		if (write_full(fd, &req, sizeof(req)) != (ssize_t)sizeof(req)) {
			close(fd);
			return -EIO;
		}

		int wait_rc = hg_wait_for_fd(fd, POLLIN, HG_JOIN_CLUSTER_TIMEOUT_MS);
		if (wait_rc != 0) {
			close(fd);
			if (wait_rc == -ETIMEDOUT &&
			    attempt + 1 < HG_JOIN_CLUSTER_MAX_ATTEMPTS)
				continue;
			return (wait_rc == -ETIMEDOUT) ? -EAGAIN : wait_rc;
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

	return -EAGAIN;
}

static int hg_send_token_metadata_to_peer(
	const struct hive_storage_node *peer,
	const struct hive_guard_token_metadata *meta,
	uint16_t command)
{
	if (!peer || !meta)
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
		return -EHOSTUNREACH;

	struct hg_token_metadata_payload payload;
	hg_token_metadata_payload_from_meta(&payload, meta);

	struct hg_token_metadata_header hdr = {
		.magic = htonl(HG_TOKEN_METADATA_MAGIC),
		.version = htons(HG_TOKEN_METADATA_VERSION),
		.command = htons(command),
		.payload_len = htonl((uint32_t)sizeof(payload)),
	};

	if (write_full(fd, &hdr, sizeof(hdr)) != (ssize_t)sizeof(hdr) ||
	    write_full(fd, &payload, sizeof(payload)) != (ssize_t)sizeof(payload)) {
		close(fd);
		return -EIO;
	}

	int wait_rc = hg_wait_for_fd(fd, POLLIN, HG_TOKEN_METADATA_TIMEOUT_MS);
	if (wait_rc != 0) {
		close(fd);
		return wait_rc;
	}

	struct hg_token_metadata_response resp;
	if (read_full(fd, &resp, sizeof(resp)) != (ssize_t)sizeof(resp)) {
		close(fd);
		return -EIO;
	}
	close(fd);

	if (ntohl(resp.magic) != HG_TOKEN_METADATA_MAGIC)
		return -EPROTO;

	int32_t status = (int32_t)ntohl((uint32_t)resp.status);
	return status;
}

int hg_ask_to_join_cluster(unsigned int storage_node_id, uint32_t join_flags)
{
	if (storage_node_id == 0)
		return -EINVAL;

	if (hg_guard_local_can_write())
		return hg_acept_request_to_join_cluster(storage_node_id,
							join_flags);

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
		rc = hg_send_join_request_to_peer(&nodes[i],
						  storage_node_id,
						  join_flags);
		if (rc == -EAGAIN)
			continue;
		if (rc == 0)
			return 0;
	}
	return rc;
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

static int hg_submit_join_sec_for_new_node(const struct hive_storage_node *node,
					   uint32_t join_flags)
{
	if (!node)
		return -EINVAL;

	char progress[8];
	char patch_level[16];
	snprintf(progress, sizeof(progress), "0%%");
	snprintf(patch_level,
		 sizeof(patch_level),
		 "%u",
		 (unsigned int)node->hive_patch_level);

	const char *machine_uid = node->uid[0] ? node->uid : NULL;

	struct hive_guard_join_context ctx = {
		.cluster_id = 0,
		.node_id = node->id,
		.machine_uid = machine_uid,
		.cduid = NULL,
		.node_record = node,
		.cluster_state = "configuring",
		.database_state = "configured",
		.kv_state = "configured",
		.cont1_state = "configured",
		.cont2_state = "configured",
		.min_nodes_required = 1,
		.flags = join_flags,
		.first_boot_ts = NULL,
		.config_status = "node_join",
		.config_progress = progress,
		.config_message = "join-as-learner",
		.hive_version = HIFS_VERSION,
		.hive_patch_level = patch_level,
		.action = "node_join",
		.node_version = node->hive_version,
		.node_patch_level = node->hive_patch_level,
		.raft_replay = 0,
	};
	return hifs_raft_submit_join_sec(&ctx);
}

int hg_acept_request_to_join_cluster(unsigned int storage_node_id,
				     uint32_t join_flags)
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
		return rc;
	}

	if (join_flags & HG_JOIN_CLUSTER_FLAG_NEW_NODE) {
		int join_rc = hg_submit_join_sec_for_new_node(node, join_flags);
		if (join_rc != 0) {
			hifs_warning("failed to submit learner join for node %u rc=%d",
				     storage_node_id,
				     join_rc);
			return join_rc;
		}
	}
	return 0;
}

int hg_send_token_metadata_to_leader(
	const struct hive_guard_token_metadata *meta)
{
	if (!meta)
		return -EINVAL;

	if (hg_guard_local_can_write())
		return hg_handle_token_metadata_locally(
			HG_TOKEN_METADATA_CMD_STORE,
			meta);

	size_t count = 0;
	const struct hive_storage_node *nodes = hifs_get_storage_nodes(&count);
	if ((!nodes || count == 0) && hifs_load_storage_nodes())
		nodes = hifs_get_storage_nodes(&count);
	if (!nodes || count == 0)
		return -ENOENT;

	int rc = -EHOSTUNREACH;
	for (size_t i = 0; i < count; ++i) {
		const struct hive_storage_node *peer = &nodes[i];
		if (!peer->id || peer->id == storage_node_id)
			continue;

		int send_rc = hg_send_token_metadata_to_peer(
			peer,
			meta,
			HG_TOKEN_METADATA_CMD_STORE);
		if (send_rc == 0)
			return 0;
		if (send_rc == -EAGAIN)
			continue;
		rc = send_rc;
	}
	return rc;
}
