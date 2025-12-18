#pragma once

#include "hive_bootstrap.h"

extern struct hive_bootstrap_config hbc;

int hive_bootstrap_sock_run(const char *socket_path);
