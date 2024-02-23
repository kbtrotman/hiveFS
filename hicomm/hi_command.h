/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <libpq-fe.h>
#include <string.h>

// In the definitions file, those items are common to hi_command in both kernel-space and
// in user-space.  The definitions file is included in both the kernel-space and user-space
// hi_command files.
// COMMON Definitions Here ONLY!
#include "../hifs_shared_defs.h"
// COMMON Definitions Here ONLY!

#define prerr(...) fprintf(stderr, "error: " __VA_ARGS__)

/*  This file is for definitions specific to the Hi_Command router in user-space.  */
extern const char *atomic_device;
extern char atomic_path[20];
extern char atomic_device_name[256]; 

// Prototypes Here:

/* hi_command.c */

/* hi_command_memman.c */
int read_from_atomic(void);
int write_to_atomic(int value);
int hi_comm_queue_init(void);

/* hi_command_sql.c */
void execute_sql(char* sql_string);
void init_hive_link(void);
void close_hive_link (void);
int get_hive_vers(void);
int save_binary_data(char *data_block, char *hash);
int register_hive_host(void);

// Prototypes Here/