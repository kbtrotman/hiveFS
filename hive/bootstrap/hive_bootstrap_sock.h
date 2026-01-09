#pragma once

#include "hive_bootstrap.h"

#define HIVE_BOOTSTRAP_SOCK_PATH "/run/hive_bootstrap.sock"
#define HIVE_BOOTSTRAP_MSG_MAX   4096
#define HIVE_BOOTSTRAP_BACKLOG   4

enum bootstrap_request_type {
	BOOTSTRAP_REQ_UNKNOWN = 0,
	BOOTSTRAP_REQ_CLUSTER,
	BOOTSTRAP_REQ_NODE_JOIN,
};

extern char g_status_message[64];
extern unsigned int g_status_percent;
extern char g_config_state[16];
extern enum bootstrap_request_type g_pending_request_type;

extern struct hive_bootstrap_config hbc;

int hive_bootstrap_sock_run(const char *socket_path);
