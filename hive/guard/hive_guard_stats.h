/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 */

// hg_stats.h

#pragma once
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#define STATS_FILE "/proc/diskstats"
#define DEVICE_NAME "sda" // *** CHANGE THIS to the device name later (e.g., vda, nvme0n1) ***
#define INTERVAL_SEC 30   // Sampling interval in seconds

uint64_t hg_now_ns(void);

typedef struct {
    uint64_t user, nice, system, idle, iowait, irq, softirq, steal;
} cpu_stat_t;

typedef struct {
    uint64_t rx;
    uint64_t tx;
} net_stat_t;

// Structure to hold relevant disk stats fields
typedef struct {
    unsigned long long reads_completed;
    unsigned long long writes_completed;
    unsigned long long total_ios; // reads_completed + writes_completed
    long long time_spent_reading;
    long long time_spent_writing;
} DiskStats;

typedef struct hg_stats_counters {
    // Gauges from /proc
    atomic_uint_fast64_t cpu_counter;
    atomic_uint_fast64_t load_avg_x100;
    atomic_uint_fast64_t mem_used_counter;   // kB
    atomic_uint_fast64_t mem_avail_counter;  // kB

    atomic_uint_fast64_t sees_warning;
    atomic_uint_fast64_t sees_error;
    _Atomic(const char *) last_error_msg;

    // Hot-path counters (monotonic, reset by exchange only where explicitly done)
    atomic_uint_fast64_t kv_putblock_calls;
    atomic_uint_fast64_t kv_putblock_bytes;     // logical bytes (4K blocks or chunk size)
    atomic_uint_fast64_t kv_putblock_dedup_hits;
    atomic_uint_fast64_t kv_putblock_dedup_misses;

    atomic_uint_fast64_t kv_rocksdb_writes;     // number of rocksdb_write() calls
    atomic_uint_fast64_t kv_rocksdb_write_ns;   // total time spent inside rocksdb_write()

    atomic_uint_fast64_t contig_write_calls;    // extent batching (elsewhere)
    atomic_uint_fast64_t contig_write_bytes;

    // Gauges computed from /proc/diskstats deltas
    atomic_uint_fast64_t read_iops;
    atomic_uint_fast64_t write_iops;
    atomic_uint_fast64_t total_iops;

    atomic_uint_fast64_t meta_chan_ps;

    // Gauges computed by stats thread
    atomic_uint_fast64_t incoming_mbps;
    atomic_uint_fast64_t cl_outgoing_mbps;

    atomic_uint_fast64_t sn_node_in_mbps;
    atomic_uint_fast64_t sn_node_out_mbps;

    atomic_uint_fast64_t writes_mbps;
    atomic_uint_fast64_t reads_mbps;
    atomic_uint_fast64_t t_throughput;

    // Hot-path monotonic counters: updated by TCP/client code
    atomic_uint_fast64_t tcp_rx_bytes;
    atomic_uint_fast64_t tcp_tx_bytes;

    // Computed gauge for DB columns "c_net_in/out" (bytes/sec)
    atomic_uint_fast64_t c_net_in_bps;
    atomic_uint_fast64_t c_net_out_bps;

    // From /proc/net/dev deltas (already computed by hg_compute_latest_stats as bytes/sec)
    atomic_uint_fast64_t s_net_in;   // bytes/sec
    atomic_uint_fast64_t s_net_out;  // bytes/sec

    atomic_uint_fast64_t avg_wr_latency; // microseconds (as stored)
    atomic_uint_fast64_t avg_rd_latency;

    atomic_uint_fast64_t clients;

    atomic_uint_fast64_t cont1_isok;
    atomic_uint_fast64_t cont2_isok;
    _Atomic(const char *) cont1_message;
    _Atomic(const char *) cont2_message;
} hg_stats_counters_t;

extern hg_stats_counters_t g_stats;

typedef struct hg_stats_snapshot {
    uint64_t kv_putblock_calls;
    uint64_t kv_putblock_bytes;
    uint64_t dedup_hits;
    uint64_t dedup_misses;

    uint64_t rocksdb_writes;
    uint64_t rocksdb_write_ns;

    uint64_t contig_calls;
    uint64_t contig_bytes;
} hg_stats_snapshot_t;

// Existing helpers
int hg_compute_latest_stats(void);
int hg_read_disk_stats(const char *device, DiskStats *stats);
int hifs_store_stats(struct hg_stats_counters stats);

// Thread control
void hg_stats_flush_periodic_start(int node_id, const char *mysql_dsn_or_cfg);
void hg_stats_flush_periodic_stop(void);

// Rollups
bool hg_stats_trim_to_five_minute_marks(void);
bool hg_stats_trim_to_twenty_minute_marks(void);
