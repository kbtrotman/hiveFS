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
 * This is intentionally simple and synchronous for now; it can evolve into
 * a fully-fledged inter-cluster communication as encryption and security is
 * added later.
 */

#include "hive_guard_sn_tcp.h"
#include "hive_guard_kv.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
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
