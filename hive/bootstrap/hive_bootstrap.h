/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

#pragma once


#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/uio.h>
#include <sys/stat.h>
 
#include <mysql.h>   // libmariadbclient or libmysqlclient
#include "../../hicomm/hi_command.h"
#include "../common/hive_common.h"



// Prototypes
int hive_bootstrap_main(void);
bool hive_bootstrap_set_stage(enum hive_bootstrap_stage stage, bool reset_attempts);
bool hive_bootstrap_reset_stage_attempts(void);
bool hive_bootstrap_increment_stage_attempts(void);
bool hive_bootstrap_update_string_field(const char *key, const char *value);
bool hive_bootstrap_update_number_field(const char *key, uint64_t value);
