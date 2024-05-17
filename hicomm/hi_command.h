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
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <poll.h>

// A user-space definition for a kernel-style Doubly Linked List
// Modified by:  kazutomo@mcs.anl.gov
#include "ext_kernel_style_linked_list_4_user.h"

// The definitions file is included in both the kernel-space and user-space
// hi_command file.
// COMMON Definitions Here ONLY!
#include "../hifs_shared_defs.h"
// COMMON Definitions Here ONLY!


#include "sql/hi_command_sql.h"
#define prerr(...) fprintf(stderr, "error: " __VA_ARGS__)

/*  This file is for definitions specific to the Hi_Command router in user-space.  */
extern const char *kern_atomic_device;
extern const char *user_atomic_device;
extern char kern_atomic_device_name[256];
extern char user_atomic_device_name[256];
extern char kern_atomic_path[20];
extern char user_atomic_path[20];
extern int kern_atomic_value;
extern int user_atomic_value;

extern char *device_file_inode;
extern char *device_file_block;
extern char *device_file_cmd;
extern int fd_cmd, fd_inode, fd_block;


// Prototypes Here:

/* hi_command.c */

/* hi_command_proto.c */
void write_to_queue(void);
void read_from_queue(void);
int hifs_init_queues(void);
int hifs_comm_check_program_up(int program);
int hifs_comm_set_program_up( int program );
int hifs_comm_set_program_down(int program);

/* hi_command_memman.c */
int read_from_atomic(enum hifs_module program);
int write_to_atomic(int value, enum hifs_module program);
int write_to_inode_dev(void);
int write_to_block_dev(void);
int write_to_cmd_dev(void);
int read_from_inode_dev(char *dev_file);
int read_from_block_dev(char *dev_file);
int read_from_cmd_dev(char *dev_file);
void hi_comm_close_queues(void);
char *hifs_strlcpy( char *dest_string, char *src_string, int max_size );

/* hi_command_sql.c */
void execute_sql(char* sql_string);
void init_hive_link(void);
void close_hive_link (void);
int get_hive_vers(void);
int save_binary_data(char *data_block, char *hash);
int register_hive_host(void);

// Prototypes Here/