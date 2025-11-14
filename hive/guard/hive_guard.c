/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */


/**
 * Thin wrapper so the build keeps the historical entry point while the
 * implementation lives in hive_guard_tcp_coms.c.
 */

#include "hive_guard.h"

int main(void)
{
	return hive_guard_server_main();
}
