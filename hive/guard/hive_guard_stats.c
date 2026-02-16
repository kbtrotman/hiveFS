/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 */

// hg_stats.c

#include <pthread.h>
#include <errno.h>

#include <dirent.h>
#include <sys/statvfs.h>
#include <sys/stat.h>
#include <limits.h>

#include "hive_guard_stats.h"
#include "hive_guard_sql.h"
#include "hive_guard.h"

hg_stats_counters_t g_stats = {0};

uint64_t hg_now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

/* -------------------------
 * Internal thread state
 * ------------------------- */
static pthread_t g_stats_thread;
static atomic_bool g_stats_thread_running = ATOMIC_VAR_INIT(false);
static atomic_bool g_stats_thread_stop    = ATOMIC_VAR_INIT(false);

/* -------------------------
 * Small helpers
 * ------------------------- */

typedef struct {
	char mount_point[PATH_MAX];
	char source[PATH_MAX];
	char fstype[64];
} mountinfo_entry_t;

static void hg_build_dev_path(char *dst, size_t dst_sz, const char *name)
{
	if (!dst || dst_sz == 0)
		return;
	const char *src = name ? name : "";
	size_t max_copy = (dst_sz > 6) ? dst_sz - 6 : 0;
	snprintf(dst, dst_sz, "/dev/%.*s", (int)max_copy, src);
}

static size_t hg_split_csv_paths(char *buf, const char *csv, char *out[], size_t out_cap)
{
	if (!buf || !csv || !out || out_cap == 0)
		return 0;

	size_t n = 0;
	strncpy(buf, csv, 4095);
	buf[4095] = '\0';

	char *save = NULL;
	for (char *tok = strtok_r(buf, ",", &save); tok && n < out_cap;
	     tok = strtok_r(NULL, ",", &save)) {

		while (*tok == ' ' || *tok == '\t')
			++tok;
		char *end = tok + strlen(tok);
		while (end > tok && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\n'))
			*--end = '\0';

		if (*tok)
			out[n++] = tok;
	}
	return n;
}

/* Read /proc/self/mountinfo into a small array for lookups. */
static int hg_read_mountinfo(mountinfo_entry_t *out, size_t cap, size_t *count_out)
{
	FILE *f = fopen("/proc/self/mountinfo", "r");
	if (!f)
		return -1;

	size_t n = 0;
	char line[4096];

	while (fgets(line, sizeof(line), f) && n < cap) {
		/* Find the separator " - " */
		char *sep = strstr(line, " - ");
		if (!sep)
			continue;

		*sep = '\0';
		char *rest = sep + 3;

		/* Tokenize first half to get mount point (field 5) */
		char *save = NULL;
		char *tok = strtok_r(line, " ", &save);
		int field = 0;
		char *mount_point = NULL;

		while (tok) {
			++field;
			if (field == 5) {
				mount_point = tok;
				break;
			}
			tok = strtok_r(NULL, " ", &save);
		}
		if (!mount_point)
			continue;

		/* Tokenize rest: fstype source superopts... */
		char *save2 = NULL;
		char *fstype = strtok_r(rest, " ", &save2);
		char *source = strtok_r(NULL, " ", &save2);

		if (!fstype || !source)
			continue;

		strncpy(out[n].mount_point, mount_point, sizeof(out[n].mount_point) - 1);
		out[n].mount_point[sizeof(out[n].mount_point) - 1] = '\0';

		strncpy(out[n].fstype, fstype, sizeof(out[n].fstype) - 1);
		out[n].fstype[sizeof(out[n].fstype) - 1] = '\0';

		strncpy(out[n].source, source, sizeof(out[n].source) - 1);
		out[n].source[sizeof(out[n].source) - 1] = '\0';

		/* strip trailing newline from source/fstype/mount_point if present */
		for (char *p = out[n].mount_point; *p; ++p) if (*p == '\n') *p = '\0';
		for (char *p = out[n].fstype; *p; ++p) if (*p == '\n') *p = '\0';
		for (char *p = out[n].source; *p; ++p) if (*p == '\n') *p = '\0';

		++n;
	}

	fclose(f);
	if (count_out)
		*count_out = n;
	return 0;
}

static const mountinfo_entry_t *hg_find_mount_by_path(const mountinfo_entry_t *mi, size_t n, const char *path)
{
	if (!mi || !path)
		return NULL;

	for (size_t i = 0; i < n; ++i) {
		if (strcmp(mi[i].mount_point, path) == 0)
			return &mi[i];
	}
	return NULL;
}

/* Best-effort: find a mount whose source path begins with /dev/<disk_name> */
static const mountinfo_entry_t *hg_find_mount_for_disk(const mountinfo_entry_t *mi, size_t n, const char *disk_name)
{
	if (!mi || !disk_name)
		return NULL;

	char prefix[256];
	hg_build_dev_path(prefix, sizeof(prefix), disk_name);
	size_t plen = strlen(prefix);

	for (size_t i = 0; i < n; ++i) {
		if (strncmp(mi[i].source, prefix, plen) == 0)
			return &mi[i];
	}
	return NULL;
}

static const char *hg_health_from_pcts(double fs_used_pct, double in_used_pct, unsigned long f_flag)
{
	/* Default thresholds: warn >=80, crit >=90 */
	bool warn = (fs_used_pct >= 80.0) || (in_used_pct >= 80.0);
	bool crit = (fs_used_pct >= 90.0) || (in_used_pct >= 90.0);

#ifdef ST_RDONLY
	if (f_flag & ST_RDONLY)
		crit = true;
#endif

	if (crit) return "crit";
	if (warn) return "warn";
	return "ok";
}

static void hg_collect_filesystem_stats(uint64_t node_id)
{
	/* Default mount list matches prior Python collector */
	const char *default_csv =
		"/hive/hive_kv,/hive/metadata,/hive/containers,/hive/logs,/hive/cache,/hive";

	const char *csv = getenv("HG_FS_STATS_MOUNTS");
	if (!csv || !csv[0])
		csv = default_csv;

	char csv_buf[4096];
	char *paths[64];
	size_t npaths = hg_split_csv_paths(csv_buf, csv, paths, 64);

	mountinfo_entry_t mi[256];
	size_t mi_n = 0;
	(void)hg_read_mountinfo(mi, 256, &mi_n);

	uint64_t ts_unix = (uint64_t)time(NULL);

	for (size_t i = 0; i < npaths; ++i) {
		const char *p = paths[i];
		if (!p || !p[0])
			continue;

		struct statvfs v;
		if (statvfs(p, &v) != 0) {
			continue;
		}

		uint64_t bsize = (uint64_t)(v.f_frsize ? v.f_frsize : v.f_bsize);

		uint64_t total = bsize * (uint64_t)v.f_blocks;
		uint64_t avail = bsize * (uint64_t)v.f_bavail;
		uint64_t used  = (total >= avail) ? (total - avail) : 0;

		double used_pct = (total > 0) ? ((double)used * 100.0 / (double)total) : 0.0;

		uint64_t in_total = (uint64_t)v.f_files;
		uint64_t in_avail = (uint64_t)v.f_favail;
		uint64_t in_used  = (in_total >= in_avail) ? (in_total - in_avail) : 0;

		double in_used_pct = (in_total > 0) ? ((double)in_used * 100.0 / (double)in_total) : 0.0;

		const mountinfo_entry_t *m = hg_find_mount_by_path(mi, mi_n, p);
		const char *fs_type = m ? m->fstype : "";
		const char *fs_name = m ? m->source : "";

		const char *health = hg_health_from_pcts(used_pct, in_used_pct, v.f_flag);

		(void)hifs_store_fs_stat(node_id, ts_unix,
					 fs_name, p, fs_type,
					 total, used, avail, used_pct,
					 in_total, in_used, in_avail, in_used_pct,
					 health);
	}
}

static int hg_read_sysfs_u64(const char *path, uint64_t *out)
{
	if (!path || !out)
		return -1;

	FILE *f = fopen(path, "r");
	if (!f)
		return -1;

	unsigned long long v = 0;
	int rc = fscanf(f, "%llu", &v);
	fclose(f);
	if (rc != 1)
		return -1;

	*out = (uint64_t)v;
	return 0;
}

static void hg_collect_disk_stats(uint64_t node_id)
{
	DIR *d = opendir("/sys/block");
	if (!d)
		return;

	mountinfo_entry_t mi[256];
	size_t mi_n = 0;
	(void)hg_read_mountinfo(mi, 256, &mi_n);

	uint64_t ts_unix = (uint64_t)time(NULL);

	struct dirent *de;
	while ((de = readdir(d)) != NULL) {
		const char *dev = de->d_name;
		if (!dev || dev[0] == '.')
			continue;

		/* Skip obvious virtual/ephemeral devices */
		if (!strncmp(dev, "loop", 4) || !strncmp(dev, "ram", 3) ||
		    !strncmp(dev, "zram", 4) || !strncmp(dev, "dm-", 3) ||
		    !strncmp(dev, "md", 2) || !strncmp(dev, "sr", 2))
			continue;

		char p_size[PATH_MAX];
		char p_sectsz[PATH_MAX];
		char p_rot[PATH_MAX];
		char p_stat[PATH_MAX];

		snprintf(p_size, sizeof(p_size), "/sys/block/%s/size", dev);
		snprintf(p_sectsz, sizeof(p_sectsz), "/sys/block/%s/queue/hw_sector_size", dev);
		snprintf(p_rot, sizeof(p_rot), "/sys/block/%s/queue/rotational", dev);
		snprintf(p_stat, sizeof(p_stat), "/sys/block/%s/stat", dev);

		uint64_t sectors = 0, sectsz = 512, rotational = 0;
		if (hg_read_sysfs_u64(p_size, &sectors) != 0)
			continue;
		(void)hg_read_sysfs_u64(p_sectsz, &sectsz);
		(void)hg_read_sysfs_u64(p_rot, &rotational);

		uint64_t disk_size_bytes = sectors * sectsz;

		/* Parse /sys/block/<dev>/stat */
		FILE *f = fopen(p_stat, "r");
		if (!f)
			continue;

		unsigned long long reads_completed = 0, reads_merged = 0, sectors_read = 0, ms_reading = 0;
		unsigned long long writes_completed = 0, writes_merged = 0, sectors_written = 0, ms_writing = 0;
		unsigned long long io_in_progress = 0, io_ms = 0, weighted_io_ms = 0;

		int n = fscanf(f, "%llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
			       &reads_completed, &reads_merged, &sectors_read, &ms_reading,
			       &writes_completed, &writes_merged, &sectors_written, &ms_writing,
			       &io_in_progress, &io_ms, &weighted_io_ms);
		fclose(f);
		if (n < 10)
			continue;

		uint64_t read_bytes = (uint64_t)sectors_read * sectsz;
		uint64_t write_bytes = (uint64_t)sectors_written * sectsz;

		char disk_path[256];
		hg_build_dev_path(disk_path, sizeof(disk_path), dev);

		const mountinfo_entry_t *m = hg_find_mount_for_disk(mi, mi_n, dev);
		const char *fs_path = m ? m->mount_point : NULL;

		const char *health = "ok";
		if (io_in_progress >= 64)
			health = "crit";
		else if (io_in_progress >= 16)
			health = "warn";

		(void)hifs_store_disk_stat(node_id, ts_unix,
					   dev, disk_path,
					   disk_size_bytes,
					   (unsigned int)rotational,
					   (uint64_t)reads_completed,
					   (uint64_t)writes_completed,
					   read_bytes, write_bytes,
					   (uint64_t)ms_reading,
					   (uint64_t)ms_writing,
					   (uint64_t)io_in_progress,
					   (uint64_t)io_ms,
					   (uint64_t)weighted_io_ms,
					   fs_path,
					   health);
	}

	closedir(d);
}
static inline uint64_t bytes_per_sec_to_mbps(uint64_t bps)
{
    // Mbps = (bytes/sec)*8 / 1,000,000
    return (bps * 8ull) / 1000000ull;
}

/* Take-and-reset only the counters you truly want “per interval” */
static hg_stats_snapshot_t hg_stats_take_interval(void)
{
    hg_stats_snapshot_t s = {0};

    s.kv_putblock_calls = atomic_exchange(&g_stats.kv_putblock_calls, 0);
    s.kv_putblock_bytes = atomic_exchange(&g_stats.kv_putblock_bytes, 0);
    s.dedup_hits        = atomic_exchange(&g_stats.kv_putblock_dedup_hits, 0);
    s.dedup_misses      = atomic_exchange(&g_stats.kv_putblock_dedup_misses, 0);

    s.rocksdb_writes    = atomic_exchange(&g_stats.kv_rocksdb_writes, 0);
    s.rocksdb_write_ns  = atomic_exchange(&g_stats.kv_rocksdb_write_ns, 0);

    s.contig_calls      = atomic_exchange(&g_stats.contig_write_calls, 0);
    s.contig_bytes      = atomic_exchange(&g_stats.contig_write_bytes, 0);

    s.meta_chan_ops     = atomic_exchange(&g_stats.meta_chan_ops, 0);
    s.read_bytes        = atomic_exchange(&g_stats.storage_read_bytes, 0);
    s.read_latency_ns   = atomic_exchange(&g_stats.read_latency_ns, 0);
    s.read_latency_samples = atomic_exchange(&g_stats.read_latency_samples, 0);

    return s;
}

/* -------------------------
 * /proc readers (your existing code, unchanged)
 * ------------------------- */

static int read_cpu_stat(cpu_stat_t *s)
{
    FILE *f = fopen("/proc/stat", "r");
    if (!f) return -1;

    int rc = fscanf(f, "cpu  %lu %lu %lu %lu %lu %lu %lu %lu",
                    &s->user, &s->nice, &s->system, &s->idle,
                    &s->iowait, &s->irq, &s->softirq, &s->steal);
    fclose(f);
    return (rc == 8) ? 0 : -1;
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
    if (len == 0)
        return -1;
    FILE *f = fopen("/proc/loadavg", "r");
    if (!f) return -1;

    if (fscanf(f, "%9s", out) != 1) {
        fclose(f);
        return -1;
    }
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

    if (frac_digits == 1)
        frac *= 10;

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
		iface = (env_iface && env_iface[0]) ? env_iface : "eth0";
	}

	return iface;
}

static const char *hg_stats_disk_device(void)
{
	static const char *dev;

	if (!dev || !dev[0]) {
		const char *env_dev = getenv("HG_STATS_DISK_DEV");
		dev = (env_dev && env_dev[0]) ? env_dev : DEVICE_NAME;
	}

	return dev;
}

// Function to read disk statistics for a specific device
int hg_read_disk_stats(const char* device, DiskStats* stats)
{
	FILE *fp;
	char line[256];
    char dev_name[32];
    int major, minor;
    unsigned long long discard;

    fp = fopen(STATS_FILE, "r");
    if (fp == NULL) {
        perror("Error opening /proc/diskstats");
        return -1;
    }

    while (fgets(line, sizeof(line), fp) != NULL) {
        if (sscanf(line, "%d %d %s %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
                   &major, &minor, dev_name,
                   &stats->reads_completed, &discard, &discard, &stats->time_spent_reading,
                   &stats->writes_completed, &discard, &discard, &stats->time_spent_writing,
                   &discard, &discard, &discard, &discard) >= 14) {

            if (strcmp(dev_name, device) == 0) {
                stats->total_ios = stats->reads_completed + stats->writes_completed;
                fclose(fp);
                return 0;
            }
        }
    }

    fclose(fp);
    fprintf(stderr, "Device %s not found in %s\n", device, STATS_FILE);
    return -1;
}

int hg_compute_latest_stats(void)
{
	static cpu_stat_t prev_cpu;
	static DiskStats prev_disk;
	static net_stat_t prev_net;
	static int initialized = 0;
	static bool have_disk = false;
	static bool have_net = false;

	cpu_stat_t cur_cpu;
	DiskStats cur_disk;
	net_stat_t cur_net;

	bool cpu_ok = (read_cpu_stat(&cur_cpu) == 0);
	bool disk_ok = (hg_read_disk_stats(hg_stats_disk_device(), &cur_disk) == 0);
	bool net_ok = (hg_read_net_bytes(hg_stats_net_iface(), &cur_net) == 0);

	uint64_t mem_used = 0, mem_avail = 0;
	if (hg_read_mem_info(&mem_used, &mem_avail) == 0) {
		atomic_store(&g_stats.mem_used_counter, mem_used);
		atomic_store(&g_stats.mem_avail_counter, mem_avail);
    }

    char loadavg_buf[16] = {0};
	if (hg_read_loadavg(loadavg_buf, sizeof(loadavg_buf)) == 0)
		atomic_store(&g_stats.load_avg_x100, hg_loadavg_to_x100(loadavg_buf));

	if (!initialized) {
		if (cpu_ok)
			prev_cpu = cur_cpu;
		if (disk_ok) {
			prev_disk = cur_disk;
			have_disk = true;
		}
		if (net_ok) {
			prev_net = cur_net;
			have_net = true;
		}
		atomic_store(&g_stats.cpu_counter, 0);
		atomic_store(&g_stats.read_iops, 0);
		atomic_store(&g_stats.write_iops, 0);
		atomic_store(&g_stats.total_iops, 0);
		atomic_store(&g_stats.s_net_in, 0);
		atomic_store(&g_stats.s_net_out, 0);
		initialized = 1;
		return cpu_ok ? 0 : -1;
	}

	if (cpu_ok) {
		atomic_store(&g_stats.cpu_counter, (uint64_t)hg_cpu_usage_percent(&prev_cpu, &cur_cpu));
		prev_cpu = cur_cpu;
	}

	uint64_t read_delta = 0;
	uint64_t write_delta = 0;

	if (disk_ok && have_disk) {
		if (cur_disk.reads_completed >= prev_disk.reads_completed)
			read_delta = (uint64_t)(cur_disk.reads_completed - prev_disk.reads_completed);
		if (cur_disk.writes_completed >= prev_disk.writes_completed)
			write_delta = (uint64_t)(cur_disk.writes_completed - prev_disk.writes_completed);
		prev_disk = cur_disk;
	} else {
		read_delta = 0;
		write_delta = 0;
		if (disk_ok) {
			prev_disk = cur_disk;
			have_disk = true;
		}
	}

	uint64_t total_delta = read_delta + write_delta;

	atomic_store(&g_stats.read_iops, read_delta / INTERVAL_SEC);
	atomic_store(&g_stats.write_iops, write_delta / INTERVAL_SEC);
	atomic_store(&g_stats.total_iops, total_delta / INTERVAL_SEC);

	uint64_t rx_delta = 0;
	uint64_t tx_delta = 0;

	if (net_ok && have_net) {
		if (cur_net.rx >= prev_net.rx)
			rx_delta = cur_net.rx - prev_net.rx;
		if (cur_net.tx >= prev_net.tx)
			tx_delta = cur_net.tx - prev_net.tx;
		prev_net = cur_net;
	} else {
		rx_delta = 0;
		tx_delta = 0;
		if (net_ok) {
			prev_net = cur_net;
			have_net = true;
		}
	}

	// bytes/sec for the storage-node NIC (from /proc/net/dev)
	atomic_store(&g_stats.s_net_in, rx_delta / INTERVAL_SEC);
	atomic_store(&g_stats.s_net_out, tx_delta / INTERVAL_SEC);

	return cpu_ok ? 0 : -1;
}

/* -------------------------
 * Periodic stats thread
 * ------------------------- */

static void *hg_stats_thread_main(void *arg)
{
    (void)arg;

    // Prime /proc baselines
    hg_compute_latest_stats();

    // Prime TCP baselines (monotonic totals updated by hot paths)
    uint64_t prev_tcp_rx = atomic_load(&g_stats.tcp_rx_bytes);
    uint64_t prev_tcp_tx = atomic_load(&g_stats.tcp_tx_bytes);
    uint64_t last_fs_sample_node = 0;

    while (!atomic_load(&g_stats_thread_stop)) {

        sleep(INTERVAL_SEC);

        // 1) Sample /proc gauges (cpu/mem/load + disk iops + sn net bps)
        (void)hg_compute_latest_stats();

        // 2) Take per-interval deltas from hot-path counters (KV + contig + rocksdb time)
        hg_stats_snapshot_t s = hg_stats_take_interval();
        atomic_store(&g_stats.meta_chan_ps, s.meta_chan_ops / INTERVAL_SEC);

        // 3) Compute client network deltas from monotonic TCP counters
        uint64_t cur_tcp_rx = atomic_load(&g_stats.tcp_rx_bytes);
        uint64_t cur_tcp_tx = atomic_load(&g_stats.tcp_tx_bytes);

        uint64_t rx_delta = (cur_tcp_rx >= prev_tcp_rx) ? (cur_tcp_rx - prev_tcp_rx) : 0;
        uint64_t tx_delta = (cur_tcp_tx >= prev_tcp_tx) ? (cur_tcp_tx - prev_tcp_tx) : 0;

        prev_tcp_rx = cur_tcp_rx;
        prev_tcp_tx = cur_tcp_tx;

        uint64_t c_rx_bps = rx_delta / INTERVAL_SEC;
        uint64_t c_tx_bps = tx_delta / INTERVAL_SEC;

        atomic_store(&g_stats.c_net_in_bps, c_rx_bps);
        atomic_store(&g_stats.c_net_out_bps, c_tx_bps);

        atomic_store(&g_stats.incoming_mbps, bytes_per_sec_to_mbps(c_rx_bps));
        atomic_store(&g_stats.cl_outgoing_mbps, bytes_per_sec_to_mbps(c_tx_bps));

        // 4) Compute throughput from KV logical bytes (writes) and tracked reads
        uint64_t kv_write_bps = s.kv_putblock_bytes / INTERVAL_SEC;
        uint64_t kv_read_bps  = s.read_bytes / INTERVAL_SEC;
        uint64_t writes_mbps  = bytes_per_sec_to_mbps(kv_write_bps);
        uint64_t reads_mbps   = bytes_per_sec_to_mbps(kv_read_bps);

        atomic_store(&g_stats.writes_mbps, writes_mbps);
        atomic_store(&g_stats.reads_mbps, reads_mbps);

        // Total throughput (Mbps)
        atomic_store(&g_stats.t_throughput,
                     (uint64_t)atomic_load(&g_stats.writes_mbps) +
                     (uint64_t)atomic_load(&g_stats.reads_mbps));
        // 5) Avg write latency from RocksDB timing counters (store microseconds)
        if (s.rocksdb_writes > 0) {
            uint64_t avg_ns = s.rocksdb_write_ns / s.rocksdb_writes;
            atomic_store(&g_stats.avg_wr_latency, avg_ns / 1000ull); // us
        } else {
            atomic_store(&g_stats.avg_wr_latency, 0);
        }

        if (s.read_latency_samples > 0) {
            uint64_t avg_ns = s.read_latency_ns / s.read_latency_samples;
            atomic_store(&g_stats.avg_rd_latency, avg_ns / 1000ull);
        } else {
            atomic_store(&g_stats.avg_rd_latency, 0);
        }

        // 6) Optionally compute SN NIC Mbps from /proc/net/dev bytes/sec gauges
        uint64_t sn_in_bps = atomic_load(&g_stats.s_net_in);
        uint64_t sn_out_bps = atomic_load(&g_stats.s_net_out);
        atomic_store(&g_stats.sn_node_in_mbps, bytes_per_sec_to_mbps(sn_in_bps));
        atomic_store(&g_stats.sn_node_out_mbps, bytes_per_sec_to_mbps(sn_out_bps));

        // 7) Persist filesystem/disk health snapshots if we know our node id
        uint64_t node_id = storage_node_id ? (uint64_t)storage_node_id : 0;
        if (node_id != 0) {
            hg_collect_filesystem_stats(node_id);
            hg_collect_disk_stats(node_id);
            last_fs_sample_node = node_id;
        } else if (last_fs_sample_node != 0) {
            hg_collect_filesystem_stats(last_fs_sample_node);
            hg_collect_disk_stats(last_fs_sample_node);
        }

        // 7) Persist one row
        (void)hifs_store_stats(&s);

        // Optional roll-ups (enable if you want)
        // (void)hg_stats_trim_to_five_minute_marks();
        // (void)hg_stats_trim_to_twenty_minute_marks();
    }

    return NULL;
}

void hg_stats_flush_periodic_start(int node_id, const char *mysql_dsn_or_cfg)
{
    (void)node_id;
    (void)mysql_dsn_or_cfg; // keep signature; init SQL in main SQL file

    if (atomic_exchange(&g_stats_thread_running, true)) {
        // already running
        return;
    }

    atomic_store(&g_stats_thread_stop, false);

    int rc = pthread_create(&g_stats_thread, NULL, hg_stats_thread_main, NULL);
    if (rc != 0) {
        atomic_store(&g_stats_thread_running, false);
        atomic_store(&g_stats.sees_error, 1);
        atomic_store(&g_stats.last_error_msg, "failed to start stats thread");
    }
}

void hg_stats_flush_periodic_stop(void)
{
    if (!atomic_load(&g_stats_thread_running))
        return;

    atomic_store(&g_stats_thread_stop, true);
    pthread_join(g_stats_thread, NULL);
    atomic_store(&g_stats_thread_running, false);
}

/* Stats coverage: mirror the definitions in hive/guard/sql/make_hive.sql. */
static const char *const kStorageNodeStatsColumns[] = {
	"node_id",
	"s_ts",
	"cpu",
	"mem_used",
	"mem_avail",
	"read_iops",
	"write_iops",
	"total_iops",
	"meta_chan_ps",
	"incoming_mbps",
	"cl_outgoing_mbps",
	"sn_node_in_mbps",
	"sn_node_out_mbps",
	"writes_mbps",
	"reads_mbps",
	"t_throughput",
	"kv_putblock_calls",
	"kv_putblock_ps",
	"kv_putblock_bytes",
	"kv_putblock_dedup_hits",
	"kv_putblock_dedup_misses",
	"kv_rocksdb_writes",
	"kv_rocksdb_write_ns",
	"contig_write_calls",
	"contig_write_bytes",
	"tcp_rx_bytes",
	"tcp_tx_bytes",
	"c_net_in",
	"c_net_out",
	"s_net_in",
	"s_net_out",
	"avg_wr_latency",
	"avg_rd_latency",
	"lavg",
	"sees_warning",
	"sees_error",
	"message",
	"cont1_isok",
	"cont2_isok",
	"cont1_message",
	"cont2_message",
	"clients"
};

static const char *const kStorageNodeFsStatsColumns[] = {
	"node_id",
	"fs_ts",
	"fs_name",
	"fs_path",
	"fs_type",
	"fs_total_bytes",
	"fs_used_bytes",
	"fs_avail_bytes",
	"fs_used_pct",
	"in_total_bytes",
	"in_used_bytes",
	"in_avail_bytes",
	"in_used_pct",
	"health"
};

static const char *const kStorageNodeDiskStatsColumns[] = {
	"node_id",
	"disk_ts",
	"disk_name",
	"disk_path",
	"disk_size_bytes",
	"disk_rotational",
	"reads_completed",
	"writes_completed",
	"read_bytes",
	"write_bytes",
	"read_ms",
	"write_ms",
	"io_in_progress",
	"io_ms",
	"weighted_io_ms",
	"fs_path",
	"health"
};

static bool hg_stats_check_table_schema(const char *table_name,
					const char *const *columns,
					size_t column_count)
{
	if (!sqldb.sql_init || !sqldb.conn || !table_name || !columns ||
	    column_count == 0)
		return false;

	char sql[256];
	int written = snprintf(sql, sizeof(sql),
			       "SELECT COLUMN_NAME FROM INFORMATION_SCHEMA.COLUMNS "
			       "WHERE TABLE_SCHEMA='%s' AND TABLE_NAME='%s'",
			       DB_NAME, table_name);
	if (written <= 0 || written >= (int)sizeof(sql)) {
		hifs_warning("stats schema query truncated for %s", table_name);
		return false;
	}

	if (mysql_real_query(sqldb.conn, sql, (unsigned long)written) != 0) {
		hifs_warning("stats schema query failed for %s: %s",
			     table_name, mysql_error(sqldb.conn));
		return false;
	}

	MYSQL_RES *res = mysql_store_result(sqldb.conn);
	if (!res) {
		hifs_warning("stats schema fetch failed for %s: %s",
			     table_name, mysql_error(sqldb.conn));
		return false;
	}

	bool ok = true;
	bool *seen = calloc(column_count, sizeof(bool));
	if (!seen) {
		mysql_free_result(res);
		hifs_warning("stats schema allocation failed for %s", table_name);
		return false;
	}

	MYSQL_ROW row;
	while ((row = mysql_fetch_row(res)) != NULL) {
		const char *col = row[0];
		if (!col)
			continue;
		for (size_t i = 0; i < column_count; ++i) {
			if (!seen[i] && strcmp(col, columns[i]) == 0) {
				seen[i] = true;
				break;
			}
		}
	}

	mysql_free_result(res);

	for (size_t i = 0; i < column_count; ++i) {
		if (!seen[i]) {
			ok = false;
			hifs_warning("stats schema missing %s.%s; update hive/guard/sql/make_hive.sql",
				     table_name, columns[i]);
		}
	}

	free(seen);
	return ok;
}

static atomic_bool g_stats_schema_checked = ATOMIC_VAR_INIT(false);
static atomic_bool g_stats_schema_ok = ATOMIC_VAR_INIT(false);

static bool hg_stats_verify_schema_once(void)
{
	if (atomic_load(&g_stats_schema_checked))
		return atomic_load(&g_stats_schema_ok);

	bool ok = true;
	ok &= hg_stats_check_table_schema("storage_node_stats",
					  kStorageNodeStatsColumns,
					  sizeof(kStorageNodeStatsColumns) /
					  sizeof(kStorageNodeStatsColumns[0]));
	ok &= hg_stats_check_table_schema("storage_node_fs_stats",
					  kStorageNodeFsStatsColumns,
					  sizeof(kStorageNodeFsStatsColumns) /
					  sizeof(kStorageNodeFsStatsColumns[0]));
	ok &= hg_stats_check_table_schema("storage_node_disk_stats",
					  kStorageNodeDiskStatsColumns,
					  sizeof(kStorageNodeDiskStatsColumns) /
					  sizeof(kStorageNodeDiskStatsColumns[0]));

	atomic_store(&g_stats_schema_ok, ok);
	atomic_store(&g_stats_schema_checked, true);

	if (!ok) {
		atomic_store(&g_stats.sees_error, 1);
		atomic_store(&g_stats.last_error_msg,
			     "stats schema mismatch; update hive/guard/sql/make_hive.sql");
	}

	return ok;
}

/* -----------------------------------------------------
 * DB insert (fixed to use c_net_in_bps/c_net_out_bps)
 * ----------------------------------------------------- */

int hifs_store_stats(const hg_stats_snapshot_t *snapshot)
{
    if (!snapshot)
        return 0;

    if (!sqldb.sql_init || !sqldb.conn)
        return 0;

    if (!hg_stats_verify_schema_once())
        return 0;

    char sql_query[MAX_QUERY_SIZE];
    char lavg_buf[32];
    char lavg_sql[36];

    uint64_t lavg_x100 = atomic_load(&g_stats.load_avg_x100);
    snprintf(lavg_buf, sizeof(lavg_buf), "%llu.%02llu",
             (unsigned long long)(lavg_x100 / 100ull),
             (unsigned long long)(lavg_x100 % 100ull));
    snprintf(lavg_sql, sizeof(lavg_sql), "'%s'", lavg_buf);

    const char *msg_raw = atomic_load(&g_stats.last_error_msg);
    char *msg_sql = NULL;
    if (msg_raw && msg_raw[0])
        msg_sql = hifs_get_quoted_value(msg_raw);

    const char *cont1_raw = atomic_load(&g_stats.cont1_message);
    char *cont1_sql = NULL;
    if (cont1_raw && cont1_raw[0])
        cont1_sql = hifs_get_quoted_value(cont1_raw);

    const char *cont2_raw = atomic_load(&g_stats.cont2_message);
    char *cont2_sql = NULL;
    if (cont2_raw && cont2_raw[0])
        cont2_sql = hifs_get_quoted_value(cont2_raw);

    unsigned int cpu              = (unsigned int)atomic_load(&g_stats.cpu_counter);
    unsigned int mem_used         = (unsigned int)atomic_load(&g_stats.mem_used_counter);
    unsigned int mem_avail        = (unsigned int)atomic_load(&g_stats.mem_avail_counter);
    unsigned int read_iops        = (unsigned int)atomic_load(&g_stats.read_iops);
    unsigned int write_iops       = (unsigned int)atomic_load(&g_stats.write_iops);
    unsigned int total_iops       = (unsigned int)atomic_load(&g_stats.total_iops);
    unsigned int meta_chan_ps     = (unsigned int)atomic_load(&g_stats.meta_chan_ps);
    unsigned int incoming_mbps    = (unsigned int)atomic_load(&g_stats.incoming_mbps);
    unsigned int cl_outgoing_mbps = (unsigned int)atomic_load(&g_stats.cl_outgoing_mbps);
    unsigned int sn_in_mbps       = (unsigned int)atomic_load(&g_stats.sn_node_in_mbps);
    unsigned int sn_out_mbps      = (unsigned int)atomic_load(&g_stats.sn_node_out_mbps);
    unsigned int writes_mbps      = (unsigned int)atomic_load(&g_stats.writes_mbps);
    unsigned int reads_mbps       = (unsigned int)atomic_load(&g_stats.reads_mbps);
    unsigned int throughput       = (unsigned int)atomic_load(&g_stats.t_throughput);

    uint64_t kv_putblock_calls    = snapshot->kv_putblock_calls;
    unsigned int kv_putblock_ps   = (unsigned int)(kv_putblock_calls / INTERVAL_SEC);
    uint64_t kv_putblock_bytes    = snapshot->kv_putblock_bytes;
    uint64_t kv_dedup_hits        = snapshot->dedup_hits;
    uint64_t kv_dedup_misses      = snapshot->dedup_misses;
    uint64_t kv_rocksdb_writes    = snapshot->rocksdb_writes;
    uint64_t kv_rocksdb_write_ns  = snapshot->rocksdb_write_ns;
    uint64_t contig_calls         = snapshot->contig_calls;
    uint64_t contig_bytes         = snapshot->contig_bytes;
    uint64_t tcp_rx_bytes         = atomic_load(&g_stats.tcp_rx_bytes);
    uint64_t tcp_tx_bytes         = atomic_load(&g_stats.tcp_tx_bytes);

    // FIX: use computed bytes/sec gauges, not the monotonic counters
    unsigned int c_net_in         = (unsigned int)atomic_load(&g_stats.c_net_in_bps);
    unsigned int c_net_out        = (unsigned int)atomic_load(&g_stats.c_net_out_bps);

    unsigned int s_net_in         = (unsigned int)atomic_load(&g_stats.s_net_in);   // bytes/sec
    unsigned int s_net_out        = (unsigned int)atomic_load(&g_stats.s_net_out);  // bytes/sec

    unsigned int avg_wr_latency   = (unsigned int)atomic_load(&g_stats.avg_wr_latency);
    unsigned int avg_rd_latency   = (unsigned int)atomic_load(&g_stats.avg_rd_latency);
    unsigned int sees_warning     = (unsigned int)atomic_load(&g_stats.sees_warning);
    unsigned int sees_error       = (unsigned int)atomic_load(&g_stats.sees_error);
    unsigned int cont1_isok       = (unsigned int)atomic_load(&g_stats.cont1_isok);
    unsigned int cont2_isok       = (unsigned int)atomic_load(&g_stats.cont2_isok);
    unsigned int clients          = (unsigned int)atomic_load(&g_stats.clients);

    uint64_t now_ts = (uint64_t)time(NULL);

    snprintf(sql_query, sizeof(sql_query), SQL_STORAGE_NODE_STATS_INSERT,
             (unsigned long long)storage_node_id,
             (unsigned long long)now_ts,
             cpu,
             mem_used,
             mem_avail,
             read_iops,
             write_iops,
             total_iops,
             meta_chan_ps,
             incoming_mbps,
             cl_outgoing_mbps,
             sn_in_mbps,
             sn_out_mbps,
             writes_mbps,
             reads_mbps,
             throughput,
             (unsigned long long)kv_putblock_calls,
             kv_putblock_ps,
             (unsigned long long)kv_putblock_bytes,
             (unsigned long long)kv_dedup_hits,
             (unsigned long long)kv_dedup_misses,
             (unsigned long long)kv_rocksdb_writes,
             (unsigned long long)kv_rocksdb_write_ns,
             (unsigned long long)contig_calls,
             (unsigned long long)contig_bytes,
             (unsigned long long)tcp_rx_bytes,
             (unsigned long long)tcp_tx_bytes,
             c_net_in,
             c_net_out,
             s_net_in,
             s_net_out,
             avg_wr_latency,
             avg_rd_latency,
             lavg_sql,
             sees_warning,
             sees_error,
             msg_sql ? msg_sql : "''",
             cont1_isok,
             cont2_isok,
             cont1_sql ? cont1_sql : "''",
             cont2_sql ? cont2_sql : "''",
             clients);

    bool ok = hifs_insert_data(sql_query);
    hifs_debug("storage_node_stats insert %s (affected=%llu last_id=%llu)",
               ok ? "succeeded" : "failed",
               (unsigned long long)sqldb.last_affected,
               (unsigned long long)sqldb.last_insert_id);

    free(msg_sql);
    free(cont1_sql);
    free(cont2_sql);

    return ok ? 1 : 0;
}

/* -------------------------
 * Rollups 
 * ------------------------- */

static bool hg_stats_execute_rollup(const char *sql_fmt)
{
    char sql_query[MAX_QUERY_SIZE];

    if (!sqldb.sql_init || !sqldb.conn || storage_node_id == 0)
        return false;

    int written = snprintf(sql_query, sizeof(sql_query), sql_fmt,
                           (unsigned long long)storage_node_id);
    if (written <= 0 || written >= (int)sizeof(sql_query)) {
        hifs_warning("stats roll-up query truncated");
        return false;
    }

    return hifs_insert_data(sql_query);
}

bool hg_stats_trim_to_five_minute_marks(void)
{
    return hg_stats_execute_rollup(SQL_STORAGE_NODE_STATS_TRIM_TO_5_MINUTES);
}

bool hg_stats_trim_to_twenty_minute_marks(void)
{
    return hg_stats_execute_rollup(SQL_STORAGE_NODE_STATS_TRIM_TO_20_MINUTES);
}
