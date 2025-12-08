/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

#pragma once


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
#include <sys/uio.h>
#include <sys/stat.h>
 
#include <mysql.h>   // libmariadbclient or libmysqlclient

#include "../../hicomm/hi_command.h"


#define ENV_LISTEN_ADDR  "WRAP_LISTEN_ADDR"
#define ENV_LISTEN_PORT  "WRAP_LISTEN_PORT"
#define ENV_IDLE_MS      "WRAP_IDLE_TIMEOUT_MS"
#define CLIENT_TCP_IDLE_TIMEOUT_PATH "/sys/class/hivefs/CLIENT_TCP_IDLE_TIMEOUT"
#define ENV_MDB_HOST     "WRAP_MARIADB_HOST"
#define ENV_MDB_PORT     "WRAP_MARIADB_PORT"
#define ENV_MDB_USER     "WRAP_MARIADB_USER"
#define ENV_MDB_PASS     "WRAP_MARIADB_PASS"
#define ENV_MDB_DB       "WRAP_MARIADB_DB"

#define MAXC 1024

extern unsigned int storage_node_id;
extern char storage_node_name[50];
extern char storage_node_address[64];
extern char storage_node_uid[128];
extern char storage_node_serial[100];
extern uint16_t storage_node_guard_port;
extern uint16_t storage_node_stripe_port;
extern	uint64_t storage_node_last_heartbeat;
extern	uint8_t  storage_node_fenced;
extern	uint32_t storage_node_hive_version;
extern	uint32_t storage_node_hive_patch_level;
extern	uint64_t storage_node_last_maintenance;
extern	char     storage_node_maintenance_reason[256];
extern	uint64_t storage_node_maintenance_started_at;
extern	uint64_t storage_node_maintenance_ended_at;
extern	uint64_t storage_node_date_added_to_cluster;
extern	uint64_t storage_node_storage_capacity_bytes;
extern	uint64_t storage_node_storage_used_bytes;
extern	uint64_t storage_node_storage_reserved_bytes;
extern	uint64_t storage_node_storage_overhead_bytes;
extern	uint32_t storage_node_client_connect_timeout_ms;
extern	uint32_t storage_node_storage_node_connect_timeout_ms;

struct hive_storage_node {
	uint32_t id;
	char	 name[100];
	char     address[64];  /* "ip:port" */
	char     uid[128];
	char     serial[100];
	uint16_t guard_port;
	uint16_t stripe_port;
	uint64_t last_heartbeat;
	uint8_t  online;
	uint8_t  fenced;
	uint32_t hive_version;
	uint32_t hive_patch_level;
	uint64_t last_maintenance;
	char     maintenance_reason[256];
	uint64_t maintenance_started_at;
	uint64_t maintenance_ended_at;
	uint64_t date_added_to_cluster;
	uint64_t storage_capacity_bytes;
	uint64_t storage_used_bytes;
	uint64_t storage_reserved_bytes;
	uint64_t storage_overhead_bytes;
	uint32_t client_connect_timeout_ms;
	uint32_t storage_node_connect_timeout_ms;
};

// Prototypes
int hive_guard_server_main(void);

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

struct hifs_ec_stripe_set {
	size_t chunk_count;
	size_t chunk_len;
	uint8_t **chunks;
	enum hifs_hash_algorithm hash_algo;
	uint8_t hash[HIFS_BLOCK_HASH_SIZE];
};

bool hifs_volume_block_ec_encode(const uint8_t *buf, uint32_t len,
				 enum hifs_hash_algorithm algo,
				 struct hifs_ec_stripe_set *out);
void hifs_volume_block_ec_free(struct hifs_ec_stripe_set *ec);
bool hifs_volume_block_store_encoded(uint64_t volume_id, uint64_t block_no,
				     const struct hifs_ec_stripe_set *ec);
int hifs_put_block_stripes(uint64_t volume_id, uint64_t block_no,
			   const struct hifs_ec_stripe_set *ec,
			   enum hifs_hash_algorithm algo);
int hifs_put_block(uint64_t volume_id, uint64_t block_no,
		   const void *data, size_t len,
		   enum hifs_hash_algorithm algo);
void hifs_guard_notify_write_ack(uint64_t volume_id, uint64_t block_no,
			 const uint8_t *hash, size_t hash_len);
