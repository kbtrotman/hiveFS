/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/epoll.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/uio.h>
#include <sys/stat.h>
 
#include <mysql.h>   // libmariadbclient or libmysqlclient

#include <raft.h>      // core algorithm state
#include <raft/uv.h>   // libuv-based I/O driver

#include "../../hicomm/hi_command.h"



/* Node identity & cluster config */
#define HIFS_GUARD_HOST "127.0.0.1"
#define N_SERVERS 3    /* Number of servers in the example cluster */
#define APPLY_RATE 125 /* Apply a new entry every 125 milliseconds */

#define Log(SERVER_ID, FORMAT) printf("%d: " FORMAT "\n", SERVER_ID)
#define Logf(SERVER_ID, FORMAT, ...) \
    printf("%d: " FORMAT "\n", SERVER_ID, __VA_ARGS__)

struct hg_raft_peer {
    uint64_t id;
    const char *address;  /* "ip:port" */
};

struct hg_raft_config {
    uint64_t        self_id;
    const char     *self_address; /* "ip:port" string */
    const char     *data_dir;     /* directory for Raft log, e.g. /var/lib/hive_guard/raft */
    struct hg_raft_peer *peers;
    unsigned        num_peers;
};

// Prototypes

/* Initialize the Raft node and start its event loop in the background. */
int  hg_raft_init(const struct hg_raft_config *cfg);

/* Clean shutdown (if/when you add it). */
void hg_raft_shutdown(void);

/* Local decision helper: is this node currently the Raft leader? */
bool hg_guard_local_can_write(void);