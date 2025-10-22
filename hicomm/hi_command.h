/**
 * HiveFS
 *
 * Hive Mind Filesystem user-space helpers
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */
#pragma once

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "../hifs_shared_defs.h"

#define HIFS_COMM_DEVICE_PATH "/dev/" HIFS_COMM_DEVICE_NAME

#define hifs_emerg(fmt, ...) fprintf(stderr, "hi_command: EMERG: " fmt "\n", ##__VA_ARGS__)
#define hifs_alert(fmt, ...) fprintf(stderr, "hi_command: ALERT: " fmt "\n", ##__VA_ARGS__)
#define hifs_crit(fmt, ...) fprintf(stderr, "hi_command: CRIT: " fmt "\n", ##__VA_ARGS__)
#define hifs_err(fmt, ...) fprintf(stderr, "hi_command: ERROR: " fmt "\n", ##__VA_ARGS__)
#define hifs_warning(fmt, ...) fprintf(stderr, "hi_command: WARN: " fmt "\n", ##__VA_ARGS__)
#define hifs_notice(fmt, ...) fprintf(stdout, "hi_command: NOTICE: " fmt "\n", ##__VA_ARGS__)
#define hifs_info(fmt, ...) fprintf(stdout, "hi_command: INFO: " fmt "\n", ##__VA_ARGS__)
#define hifs_debug(fmt, ...) fprintf(stdout, "hi_command: DEBUG: " fmt "\n", ##__VA_ARGS__)

/* hi_command_mm.c */
int hifs_comm_open(bool nonblock);
void hifs_comm_close(int fd);
int hifs_comm_recv_cmd(int fd, struct hifs_cmds_user *cmd, bool nonblock);
int hifs_comm_recv_inode(int fd, struct hifs_inode_user *inode, bool nonblock);
int hifs_comm_get_status(int fd, struct hifs_comm_status *status);
int hifs_comm_send_cmd(int fd, const struct hifs_cmds_user *cmd);
int hifs_comm_send_cmd_string(int fd, const char *cmd);
int hifs_comm_send_inode(int fd, const struct hifs_inode_user *inode);
void hi_comm_safe_cleanup(void);

/* hi_command_prot.c */
int hifs_handle_command(int fd, const struct hifs_cmds_user *cmd);
void hifs_print_inode(const struct hifs_inode_user *inode);

/* Utility */
const char *hifs_link_state_string(enum hifs_link_state state);
