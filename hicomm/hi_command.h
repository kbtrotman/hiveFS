/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <linux/netlink.h>
#include <linux/genetlink.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>

// In the definitions file, those items are common to hi_command in both kernel-space and
// in user-space.  The definitions file is included in both the kernel-space and user-space
// hi_command files.
// COMMON Definitions Here ONLY!
#include "../hive_fs_defs.h"
// COMMON Definitions Here ONLY!


/*  This file is for definitions specific to the Hi_Command router in user-space.  */

// Prototypes Here:

/* hi_command.c */
void handle_netlink_msg(int sock_fd);

/* hi_command_sql.c */
void execute_sql(char* sql_string);
void init_hive_link(void);
void close_hive_link (void);
int get_hive_vers(void);
int save_binary_data(char *data_block, char *hash);
int register_hive_host(void);

// Prototypes Here:

