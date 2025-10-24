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

void hicomm_log(int level, const char *fmt, ...);

/* Convenience macros mirroring the kernel names */
#define hifs_emerg(fmt, ...)   hicomm_log(LOG_EMERG,   "hivefs: " fmt, ##__VA_ARGS__)
#define hifs_alert(fmt, ...)   hicomm_log(LOG_ALERT,   "hivefs: " fmt, ##__VA_ARGS__)
#define hifs_crit(fmt, ...)    hicomm_log(LOG_CRIT,    "hivefs: " fmt, ##__VA_ARGS__)
#define hifs_err(fmt, ...)     hicomm_log(LOG_ERR,     "hivefs: " fmt, ##__VA_ARGS__)
#define hifs_warning(fmt, ...) hicomm_log(LOG_WARNING, "hivefs: " fmt, ##__VA_ARGS__)
#define hifs_notice(fmt, ...)  hicomm_log(LOG_NOTICE,  "hivefs: " fmt, ##__VA_ARGS__)
#define hifs_info(fmt, ...)    hicomm_log(LOG_INFO,    "hivefs: " fmt, ##__VA_ARGS__)
#define hifs_debug(fmt, ...)   hicomm_log(LOG_DEBUG,   "hivefs: " fmt, ##__VA_ARGS__)

/* hi_command_mm.c */
int hicomm_comm_open(bool nonblock);
void hicomm_comm_close(int fd);
int hicomm_recv_cmd(int fd, struct hifs_cmds *cmd, bool nonblock);
int hicomm_comm_recv_inode(int fd, struct hifs_inode *inode, bool nonblock);
int hicomm_comm_get_status(int fd, struct hifs_comm_status *status);
int hicomm_comm_send_cmd(int fd, const struct hifs_cmds *cmd);
int hicomm_send_cmd_str(int fd, const char *cmd);
int hicomm_comm_send_inode(int fd, const struct hifs_inode *inode);
void hicomm_safe_cleanup(void);

/* hi_command_prot.c */
int hicomm_handle_command(int fd, const struct hifs_cmds *cmd);
void hicomm_print_inode(const struct hifs_inode *inode);

/* Utility */
const char *hifs_link_state_string(enum hifs_link_state state);
