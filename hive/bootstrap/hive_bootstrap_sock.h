#pragma once

#include "hive_bootstrap.h"

void bootstrap_status_update(const char *message, unsigned int percent,
				    const char *state);
int hive_bootstrap_sock_run(const char *socket_path);
