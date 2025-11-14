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


#define HIFS_GUARD_HOST "127.0.0.1"
#define HIFS_GUARD_PORT_STR "6060"

/* Erasure coding profile shared with hive_guard */
#define HIFS_EC_K         6
#define HIFS_EC_M         3
#define HIFS_EC_W         8
#define HIFS_EC_CHECKSUM  CHKSUM_CRC32

typedef struct {
    int     desc;        /* liberasurecode instance handle */
    size_t  k, m, w;     /* profile in use */
    int     checksum;    /* CHKSUM_* */
    int     initialized; /* 0/1 */
} ec_ctx_t;

#define HIFS_COMM_DEVICE_PATH "/dev/" HIFS_COMM_DEVICE_NAME

struct superblock {
	uint64_t volume_id;
	uint32_t s_magic;
	uint32_t s_blocksize;
	uint32_t s_blocksize_bits;
	uint64_t s_blocks_count;
	uint64_t s_free_blocks;
	uint64_t s_inodes_count;
	uint64_t s_free_inodes;
};

struct machine {
	char *serial;
	char *name;
	long host_id;
	char *os_name;
	char *os_version;
	char *create_time;
};

void hicomm_log(int level, const char *fmt, ...);

void init_hive_link(void);
void close_hive_link(void);
int register_hive_host(void);
int hifs_get_hive_host_sbs(void);


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


/* Utility */
const char *hifs_link_state_string(enum hifs_link_state state);

/* Remote superblock helpers */
bool hifs_volume_super_get(uint64_t volume_id, struct hifs_volume_superblock *out);
bool hifs_volume_super_set(uint64_t volume_id, const struct hifs_volume_superblock *vsb);
bool hifs_root_dentry_load(uint64_t volume_id, struct hifs_volume_root_dentry *out);
bool hifs_root_dentry_store(uint64_t volume_id, const struct hifs_volume_root_dentry *root);
bool hifs_volume_dentry_load_by_inode(uint64_t volume_id, uint64_t inode,
                                      struct hifs_volume_dentry *out);
bool hifs_volume_dentry_load_by_name(uint64_t volume_id, uint64_t parent,
                                     const char *name_hex, uint32_t name_hex_len,
                                     struct hifs_volume_dentry *out);
bool hifs_volume_dentry_store(uint64_t volume_id,
                              const struct hifs_volume_dentry *dent);
bool hifs_volume_inode_load(uint64_t volume_id, uint64_t inode,
                            struct hifs_inode_wire *out);
bool hifs_volume_inode_store(uint64_t volume_id,
                             const struct hifs_inode_wire *inode);
bool hifs_volume_block_load(uint64_t volume_id, uint64_t block_no,
                            uint8_t *buf, uint32_t *len);
bool hifs_volume_block_store(uint64_t volume_id, uint64_t block_no,
                             const uint8_t *buf, uint32_t len);
