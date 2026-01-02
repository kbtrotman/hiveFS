#pragma once

#include "hive_bootstrap.h"

#define HIVE_BOOTSTRAP_SOCK_PATH "/run/hive_bootstrap.sock"
#define HIVE_BOOTSTRAP_MSG_MAX   4096
#define HIVE_BOOTSTRAP_BACKLOG   4
#define NODE_CERT_KEY_BITS       2048
#define NODE_CERT_VALID_DAYS     365


extern struct hive_bootstrap_config hbc;

int hive_bootstrap_sock_run(const char *socket_path);
