/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

/**
 * HiveFS - hive_guard_auth
 *
 * The keypair creator (access) and certificate (authenticator)
 * management routines for hiveFS. This also will need to include
 * user and group management as they relate to tokens and certs.
 *
 * Basically, this is client access, node access, and RBAC.
 *
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <sys/random.h>
#include <errno.h>

#define NODE_CERT_VALID_DAYS 365

struct hive_storage_node;

int hive_guard_generate_node_token(char out[44]);
int add_node_mtls_token(const struct hive_storage_node *node);
int add_node_join_token(uint32_t node_id, const char *token);
int update_node_for_add(struct hive_storage_node *local);
