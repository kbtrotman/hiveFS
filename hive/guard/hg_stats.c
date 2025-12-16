/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

 // hg_stats.c
#include "hg_stats.h"

hg_stats_counters_t g_stats = {0};

static inline uint64_t hg_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static hg_stats_snapshot_t hg_stats_take_interval(void) {
    hg_stats_snapshot_t s = {0};
    s.kv_putblock_calls   = atomic_exchange(&g_stats.kv_putblock_calls, 0);
    s.kv_putblock_bytes   = atomic_exchange(&g_stats.kv_putblock_bytes, 0);
    s.dedup_hits          = atomic_exchange(&g_stats.kv_putblock_dedup_hits, 0);
    s.dedup_misses        = atomic_exchange(&g_stats.kv_putblock_dedup_misses, 0);

    s.rocksdb_writes      = atomic_exchange(&g_stats.kv_rocksdb_writes, 0);
    s.rocksdb_write_ns    = atomic_exchange(&g_stats.kv_rocksdb_write_ns, 0);

    s.contig_calls        = atomic_exchange(&g_stats.contig_write_calls, 0);
    s.contig_bytes        = atomic_exchange(&g_stats.contig_write_bytes, 0);

    s.tcp_rx_bytes        = atomic_exchange(&g_stats.tcp_rx_bytes, 0);
    s.tcp_tx_bytes        = atomic_exchange(&g_stats.tcp_tx_bytes, 0);
    return s;
}


static int read_cpu_stat(cpu_stat_t *s)
{
    FILE *f = fopen("/proc/stat", "r");
    if (!f) return -1;

    fscanf(f, "cpu  %lu %lu %lu %lu %lu %lu %lu %lu",
           &s->user, &s->nice, &s->system, &s->idle,
           &s->iowait, &s->irq, &s->softirq, &s->steal);
    fclose(f);
    return 0;
}

static int hg_cpu_usage_percent(cpu_stat_t *prev, cpu_stat_t *cur)
{
    uint64_t idle_prev = prev->idle + prev->iowait;
    uint64_t idle_cur  = cur->idle  + cur->iowait;

    uint64_t non_idle_prev = prev->user + prev->nice + prev->system +
                             prev->irq + prev->softirq + prev->steal;
    uint64_t non_idle_cur  = cur->user + cur->nice + cur->system +
                             cur->irq + cur->softirq + cur->steal;

    uint64_t total_prev = idle_prev + non_idle_prev;
    uint64_t total_cur  = idle_cur  + non_idle_cur;

    uint64_t totald = total_cur - total_prev;
    uint64_t idled  = idle_cur  - idle_prev;

    if (totald == 0) return 0;
    return (int)((totald - idled) * 100 / totald);
}

static int hg_read_mem_info(uint64_t *used_kb, uint64_t *avail_kb)
{
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return -1;

    uint64_t total = 0, avail = 0;
    char key[64];
    uint64_t val;
    char unit[16];

    while (fscanf(f, "%63s %lu %15s\n", key, &val, unit) == 3) {
        if (!strcmp(key, "MemTotal:"))
            total = val;
        else if (!strcmp(key, "MemAvailable:"))
            avail = val;
    }
    fclose(f);

    if (!total || !avail) return -1;

    *used_kb  = total - avail;
    *avail_kb = avail;
    return 0;
}

static int hg_read_loadavg(char *out, size_t len)
{
    FILE *f = fopen("/proc/loadavg", "r");
    if (!f) return -1;

    fscanf(f, "%9s", out);
    fclose(f);
    return 0;
}

static uint64_t hg_loadavg_to_x100(const char *val)
{
    uint64_t whole = 0;
    uint64_t frac = 0;
    int frac_digits = 0;
    const char *p = val;

    while (*p >= '0' && *p <= '9') {
        whole = whole * 10 + (uint64_t)(*p - '0');
        ++p;
    }

    if (*p == '.') {
        ++p;
        while (*p >= '0' && *p <= '9' && frac_digits < 2) {
            frac = frac * 10 + (uint64_t)(*p - '0');
            ++p;
            ++frac_digits;
        }
    }

    if (frac_digits == 1) {
        frac *= 10;
    }

    return whole * 100 + frac;
}

static int hg_read_net_bytes(const char *iface, net_stat_t *s)
{
    FILE *f = fopen("/proc/net/dev", "r");
    if (!f) return -1;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char name[32];
        uint64_t rx, tx;
        if (sscanf(line, " %31[^:]: %lu %*u %*u %*u %*u %*u %*u %*u %lu",
                   name, &rx, &tx) == 3) {
            if (!strcmp(name, iface)) {
                s->rx = rx;
                s->tx = tx;
                fclose(f);
                return 0;
            }
        }
    }
    fclose(f);
    return -1;
}

static const char *hg_stats_net_iface(void)
{
    static const char *iface;

    if (!iface || !iface[0]) {
        const char *env_iface = getenv("HG_STATS_NET_IFACE");
        if (env_iface && env_iface[0])
            iface = env_iface;
        else
            iface = "eth0";
    }

    return iface;
}

// Function to read disk statistics for a specific device
int hg_read_disk_stats(const char* device, DiskStats* stats) {
    FILE *fp;
    char line[256];
    char dev_name[32];
    int major, minor;
    unsigned long long discard, discard_sectors, flush_requests, flush_time;

    fp = fopen(STATS_FILE, "r");
    if (fp == NULL) {
        perror("Error opening /proc/diskstats");
        return -1;
    }

    // Iterate through lines until the desired device is found
    while (fgets(line, sizeof(line), fp) != NULL) {
        // Use sscanf to parse the fields (there are more than 14, but we only need a few)
        if (sscanf(line, "%d %d %s %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
                   &major, &minor, dev_name,
                   &stats->reads_completed, &discard, &discard, &stats->time_spent_reading,
                   &stats->writes_completed, &discard, &discard, &stats->time_spent_writing,
                   &discard, &discard, &discard, &discard) >= 14) {
            
            if (strcmp(dev_name, device) == 0) {
                stats->total_ios = stats->reads_completed + stats->writes_completed;
                fclose(fp);
                return 0; // Success
            }
        }
    }

    fclose(fp);
    fprintf(stderr, "Device %s not found in %s\n", device, STATS_FILE);
    return -1; // Device not found
}

int hg_compute_latest_stats(void)
{
    static cpu_stat_t prev_cpu;
    static DiskStats prev_disk;
    static net_stat_t prev_net;
    static int initialized = 0;

    cpu_stat_t cur_cpu;
    DiskStats cur_disk;
    net_stat_t cur_net;

    if (read_cpu_stat(&cur_cpu) != 0)
        return -1;
    if (hg_read_disk_stats(DEVICE_NAME, &cur_disk) != 0)
        return -1;
    if (hg_read_net_bytes(hg_stats_net_iface(), &cur_net) != 0)
        return -1;

    uint64_t mem_used = 0, mem_avail = 0;
    if (hg_read_mem_info(&mem_used, &mem_avail) == 0) {
        atomic_store(&g_stats.mem_used_counter, mem_used);
        atomic_store(&g_stats.mem_avail_counter, mem_avail);
    }

    char loadavg_buf[16] = {0};
    if (hg_read_loadavg(loadavg_buf, sizeof(loadavg_buf)) == 0)
        atomic_store(&g_stats.load_avg_x100, hg_loadavg_to_x100(loadavg_buf));

    if (!initialized) {
        prev_cpu = cur_cpu;
        prev_disk = cur_disk;
        prev_net = cur_net;
        atomic_store(&g_stats.cpu_counter, 0);
        atomic_store(&g_stats.read_iops, 0);
        atomic_store(&g_stats.write_iops, 0);
        atomic_store(&g_stats.total_iops, 0);
        atomic_store(&g_stats.s_net_in, 0);
        atomic_store(&g_stats.s_net_out, 0);
        initialized = 1;
        return 0;
    }

    atomic_store(&g_stats.cpu_counter, (uint64_t)hg_cpu_usage_percent(&prev_cpu, &cur_cpu));
    prev_cpu = cur_cpu;

    uint64_t read_delta = 0;
    uint64_t write_delta = 0;

    if (cur_disk.reads_completed >= prev_disk.reads_completed)
        read_delta = (uint64_t)(cur_disk.reads_completed - prev_disk.reads_completed);
    if (cur_disk.writes_completed >= prev_disk.writes_completed)
        write_delta = (uint64_t)(cur_disk.writes_completed - prev_disk.writes_completed);

    uint64_t total_delta = read_delta + write_delta;

    atomic_store(&g_stats.read_iops, read_delta / INTERVAL_SEC);
    atomic_store(&g_stats.write_iops, write_delta / INTERVAL_SEC);
    atomic_store(&g_stats.total_iops, total_delta / INTERVAL_SEC);
    prev_disk = cur_disk;

    uint64_t rx_delta = 0;
    uint64_t tx_delta = 0;

    if (cur_net.rx >= prev_net.rx)
        rx_delta = cur_net.rx - prev_net.rx;
    if (cur_net.tx >= prev_net.tx)
        tx_delta = cur_net.tx - prev_net.tx;

    atomic_store(&g_stats.s_net_in, rx_delta / INTERVAL_SEC);
    atomic_store(&g_stats.s_net_out, tx_delta / INTERVAL_SEC);
    prev_net = cur_net;

    return 0;
}
