/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

#define _GNU_SOURCE
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
 
#include <mysql.h>   // libmariadbclient or libmysqlclient


// ---- Configuration (env) ----------------------------------------------------
static const char* ENV_LISTEN_ADDR  = "WRAP_LISTEN_ADDR";   // default 0.0.0.0
static const char* ENV_LISTEN_PORT  = "WRAP_LISTEN_PORT";   // default 7400
static const char* ENV_IDLE_MS      = "WRAP_IDLE_TIMEOUT_MS"; // default 30000
static const char* ENV_MDB_HOST     = "WRAP_MARIADB_HOST";  // default 127.0.0.1
static const char* ENV_MDB_PORT     = "WRAP_MARIADB_PORT";  // default 3306
static const char* ENV_MDB_USER     = "WRAP_MARIADB_USER";  // default root
static const char* ENV_MDB_PASS     = "WRAP_MARIADB_PASS";  // default ""
static const char* ENV_MDB_DB       = "WRAP_MARIADB_DB";    // default storage
 