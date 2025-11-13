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
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <sys/socket.h>


#include "../hifs_shared_defs.h"

#define HIFS_GUARD_HOST 127.0.0.1
#define HIFS_GUARD_PORT_STR 6060

#define HIFS_COMM_DEVICE_PATH "/dev/" HIFS_COMM_DEVICE_NAME

void hicomm_log(int level, const char *fmt, ...);

/* ======================= EC profile (single source of truth) ======================= */
#define HIFS_EC_K         6
#define HIFS_EC_M         3
#define HIFS_EC_W         8
#define HIFS_EC_CHECKSUM  CHKSUM_CRC32

/* ======================= Erasure Coding Module context ======================= */
typedef struct {
    int     desc;        /* liberasurecode instance handle */
    size_t  k, m, w;     /* profile in use */
    int     checksum;    /* CHKSUM_* */
    int     initialized; /* 0/1 */
} ec_ctx_t;

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
int hicomm_comm_recv_data(int fd, struct hifs_data_frame *frame, bool nonblock);
int hicomm_comm_get_status(int fd, struct hifs_comm_status *status);
int hicomm_comm_get_cache_status(int fd, struct hifs_cache_status *status);
int hicomm_comm_send_cmd(int fd, const struct hifs_cmds *cmd);
int hicomm_send_cmd_str(int fd, const char *cmd);
int hicomm_comm_send_data(int fd, const struct hifs_data_frame *frame);
void hicomm_safe_cleanup(void);

/* hi_command_prot.c */
int hicomm_handle_command(int fd, const struct hifs_cmds *cmd);
void hicomm_print_inode(const struct hifs_inode *inode);

/* hi_command_erasure_encoding.c */
int hifs_ec_ensure_init(void);
int hicomm_erasure_coding_init(void);
int hicomm_erasure_coding_encode(const uint8_t *data, size_t data_len,
                                 uint8_t **encoded_chunks, size_t *chunk_size,
                                 size_t num_data_chunks, size_t num_parity_chunks);
int hicomm_erasure_coding_decode(uint8_t **encoded_chunks, size_t chunk_size,
                                 size_t num_data_chunks, size_t num_parity_chunks,
                                 uint8_t *decoded_data, size_t *data_len);
int hicomm_erasure_coding_rebuild_from_partial(uint8_t **encoded_chunks, size_t chunk_size,
                                 size_t num_data_chunks, size_t num_parity_chunks,
                                 uint8_t *decoded_data, size_t *data_len);

/* Utility */
const char *hifs_link_state_string(enum hifs_link_state state);
