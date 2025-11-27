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
#define ENV_MDB_HOST     "WRAP_MARIADB_HOST"
#define ENV_MDB_PORT     "WRAP_MARIADB_PORT"
#define ENV_MDB_USER     "WRAP_MARIADB_USER"
#define ENV_MDB_PASS     "WRAP_MARIADB_PASS"
#define ENV_MDB_DB       "WRAP_MARIADB_DB"

#define MAXC 1024

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
