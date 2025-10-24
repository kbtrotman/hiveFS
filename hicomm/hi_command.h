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
#include <stdarg.h>
#include <syslog.h>
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

void hifs_log_user(int level, const char *fmt, ...);

/* Convenience macros mirroring the kernel names */
#define hifs_emerg(fmt, ...)   hifs_log_user(LOG_EMERG,   "hivefs: " fmt, ##__VA_ARGS__)
#define hifs_alert(fmt, ...)   hifs_log_user(LOG_ALERT,   "hivefs: " fmt, ##__VA_ARGS__)
#define hifs_crit(fmt, ...)    hifs_log_user(LOG_CRIT,    "hivefs: " fmt, ##__VA_ARGS__)
#define hifs_err(fmt, ...)     hifs_log_user(LOG_ERR,     "hivefs: " fmt, ##__VA_ARGS__)
#define hifs_warning(fmt, ...) hifs_log_user(LOG_WARNING, "hivefs: " fmt, ##__VA_ARGS__)
#define hifs_notice(fmt, ...)  hifs_log_user(LOG_NOTICE,  "hivefs: " fmt, ##__VA_ARGS__)
#define hifs_info(fmt, ...)    hifs_log_user(LOG_INFO,    "hivefs: " fmt, ##__VA_ARGS__)
#define hifs_debug(fmt, ...)   hifs_log_user(LOG_DEBUG,   "hivefs: " fmt, ##__VA_ARGS__)

/* hi_command_mm.c */
int hifs_comm_open(bool nonblock);
void hifs_comm_close(int fd);
int hifs_comm_recv_cmd(int fd, struct hifs_cmds *cmd, bool nonblock);
int hifs_comm_recv_inode(int fd, struct hifs_inode *inode, bool nonblock);
int hifs_comm_get_status(int fd, struct hifs_comm_status *status);
int hifs_comm_send_cmd(int fd, const struct hifs_cmds *cmd);
int hifs_comm_send_cmd_string(int fd, const char *cmd);
int hifs_comm_send_inode(int fd, const struct hifs_inode *inode);
void hi_comm_safe_cleanup(void);

/* hi_command_prot.c */
int hifs_handle_command(int fd, const struct hifs_cmds *cmd);
void hifs_print_inode(const struct hifs_inode *inode);

/* Utility */
const char *hifs_link_state_string(enum hifs_link_state state);
