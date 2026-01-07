/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

/**
 * HiveFS
 *
 * Common helpers shared by bootstrap & guard socket listeners.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

struct hive_common_sock_params {
	const char *path;
	const char *default_path;
	int backlog;
	mode_t mode;
	char *resolved_path;
	size_t resolved_path_len;
};

int hive_common_sock_create_listener(const struct hive_common_sock_params *params);
void hive_common_sock_destroy_listener(int fd, const char *path);
bool hive_common_sock_read_json_request(int fd, char *buf, size_t buf_sz);
void hive_common_sock_respond(int fd, const char *msg);
