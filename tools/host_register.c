/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

/**
 * Simple host registration helper.
 *
 * - Generates a 256-bit symmetric key (private) and derives a public tag
 *   (SHA-256 of the private key).
 * - Stores the private key locally (best-effort).
 * - Sends the public tag plus host identity to hive_guard using the existing
 *   length-prefixed JSON framing.
 *
 * NOTE: hive_guard must support the "register_host_key" RPC that accepts
 *       token + pub_key_b64 alongside the normal host identity fields.
 */

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <openssl/sha.h>

#include "../hicomm/hi_command.h"

#define PRIVKEY_BYTES 32
#define PUBTAG_BYTES 32
#define DEFAULT_PRIV_PATH "/sys/class/hivefs/host_private.key"

static int dial(const char *host, const char *port)
{
	struct addrinfo hints, *res = NULL, *rp = NULL;
	memset(&hints, 0, sizeof(hints));
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_family = AF_UNSPEC;

	if (getaddrinfo(host, port, &hints, &res) != 0)
		return -1;

	int fd = -1;
	for (rp = res; rp; rp = rp->ai_next) {
		fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (fd < 0)
			continue;
		if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0)
			break;
		close(fd);
		fd = -1;
	}
	freeaddrinfo(res);
	return fd;
}

static int send_all(int fd, const void *buf, size_t len)
{
	const uint8_t *p = buf;
	size_t sent = 0;
	while (sent < len) {
		ssize_t n = send(fd, p + sent, len - sent, 0);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (n == 0)
			return -1;
		sent += (size_t)n;
	}
	return 0;
}

static int recv_all(int fd, void *buf, size_t len)
{
	uint8_t *p = buf;
	size_t recvd = 0;
	while (recvd < len) {
		ssize_t n = recv(fd, p + recvd, len - recvd, 0);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (n == 0)
			return -1;
		recvd += (size_t)n;
	}
	return 0;
}

static char *base64_encode(const uint8_t *in, size_t len)
{
	static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	size_t out_len = 4 * ((len + 2) / 3);
	char *out = malloc(out_len + 1);
	if (!out)
		return NULL;
	size_t i = 0, j = 0;
	while (i < len) {
		uint32_t octet_a = i < len ? in[i++] : 0;
		uint32_t octet_b = i < len ? in[i++] : 0;
		uint32_t octet_c = i < len ? in[i++] : 0;
		uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;
		out[j++] = tbl[(triple >> 18) & 0x3F];
		out[j++] = tbl[(triple >> 12) & 0x3F];
		out[j++] = (i - 1 > len) ? '=' : tbl[(triple >> 6) & 0x3F];
		out[j++] = (i > len) ? '=' : tbl[triple & 0x3F];
		if (i - 1 > len)
			out[j - 2] = '=';
		if (i > len)
			out[j - 1] = '=';
	}
	out[j] = '\0';
	return out;
}

static int write_private_key(const uint8_t *priv, size_t len)
{
	const char *paths[] = { DEFAULT_PRIV_PATH, "/etc/hivefs_host_private.key" };
	for (size_t i = 0; i < sizeof(paths)/sizeof(paths[0]); ++i) {
		FILE *fp = fopen(paths[i], "wb");
		if (!fp)
			continue;
		size_t w = fwrite(priv, 1, len, fp);
		fclose(fp);
		if (w == len) {
			printf("Private key stored at %s\n", paths[i]);
			return 0;
		}
	}
	fprintf(stderr, "Warning: failed to store private key\n");
	return -1;
}

int main(int argc, char **argv)
{
	const char *token = NULL;
	if (argc < 2) {
		fprintf(stderr, "Usage: %s <auth-token>\n", argv[0]);
		return 1;
	}
	token = argv[1];

	uint8_t priv[PRIVKEY_BYTES];
	if (getentropy(priv, sizeof(priv)) != 0) {
		perror("getentropy");
		return 1;
	}
	if (write_private_key(priv, sizeof(priv)) != 0) {
		fprintf(stderr, "Continuing despite private key write failure.\n");
	}

	uint8_t pub[PUBTAG_BYTES];
	SHA256(priv, sizeof(priv), pub);
	char *pub_b64 = base64_encode(pub, sizeof(pub));
	if (!pub_b64) {
		fprintf(stderr, "Failed to encode public key\n");
		return 1;
	}

	char hostname[256] = {0};
	gethostname(hostname, sizeof(hostname));
	long host_id = gethostid();
	char *machine_id = NULL;
	{
		static char id_buf[64];
		FILE *fp = fopen("/etc/machine-id", "r");
		if (fp && fgets(id_buf, sizeof(id_buf), fp)) {
			id_buf[strcspn(id_buf, "\n")] = '\0';
			machine_id = id_buf;
		}
		if (fp)
			fclose(fp);
		if (!machine_id)
			machine_id = "unknown";
	}

	int fd = dial(HIFS_GUARD_HOST, HIFS_GUARD_PORT_STR);
	if (fd < 0) {
		fprintf(stderr, "Failed to connect to hive_guard at %s:%s\n",
		        HIFS_GUARD_HOST, HIFS_GUARD_PORT_STR);
		free(pub_b64);
		return 1;
	}

	/* Build JSON request */
	char json[1024];
	int n = snprintf(json, sizeof(json),
	                 "{\"type\":\"register_host_key\","
	                 "\"token\":\"%s\","
	                 "\"machine_id\":\"%s\","
	                 "\"hostname\":\"%s\","
	                 "\"host_id\":%ld,"
	                 "\"public_key_b64\":\"%s\"}",
	                 token, machine_id, hostname, host_id, pub_b64);
	free(pub_b64);
	if (n <= 0 || (size_t)n >= sizeof(json)) {
		fprintf(stderr, "Failed to build JSON request\n");
		close(fd);
		return 1;
	}

	/* Length-prefixed frame */
	uint32_t be_len = htonl((uint32_t)n);
	if (send_all(fd, &be_len, sizeof(be_len)) != 0 ||
	    send_all(fd, json, (size_t)n) != 0) {
		fprintf(stderr, "Failed to send request\n");
		close(fd);
		return 1;
	}

	/* Read reply */
	uint32_t be_reply_len = 0;
	if (recv_all(fd, &be_reply_len, sizeof(be_reply_len)) != 0) {
		fprintf(stderr, "Failed to read reply header\n");
		close(fd);
		return 1;
	}
	uint32_t reply_len = ntohl(be_reply_len);
	char *reply = malloc(reply_len + 1);
	if (!reply) {
		close(fd);
		return 1;
	}
	if (recv_all(fd, reply, reply_len) != 0) {
		fprintf(stderr, "Failed to read reply body\n");
		free(reply);
		close(fd);
		return 1;
	}
	reply[reply_len] = '\0';
	printf("hive_guard reply: %s\n", reply);
	free(reply);
	close(fd);
	return 0;
}
