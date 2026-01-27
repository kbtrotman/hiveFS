import { useCallback, useEffect, useMemo, useRef, useState } from 'react';

const API_BASE =
  import.meta?.env?.VITE_NODES_API_BASE_URL ?? 'http://localhost:8000/api/v1';

export interface DiskStat {
  key?: string | number | null;
  node_id?: number | null;
  s_ts?: string | null;
  cpu?: number | null;
  mem_used?: number | null;
  mem_avail?: number | null;
  read_iops?: number | null;
  write_iops?: number | null;
  total_iops?: number | null;
  writes_mbps?: number | null;
  reads_mbps?: number | null;
  t_throughput?: number | null;
  c_net_in?: number | null;
  c_net_out?: number | null;
  s_net_in?: number | null;
  s_net_out?: number | null;
  meta_chan_ps?: number | null;
  avg_wr_latency?: number | null;
  avg_rd_latency?: number | null;
  sees_warning?: number | null;
  sees_error?: number | null;
  message?: string | null;
  cont1_isok?: number | null;
  cont2_isok?: number | null;
  cont1_message?: string | null;
  cont2_message?: string | null;
  clients?: number | null;
  lavg?: number | null;
  [key: string]: unknown;
}

export type StatHealth = 'error' | 'warning' | 'healthy';

export function resolveStatHealth(stat: DiskStat): StatHealth {
  if ((stat.sees_error ?? 0) > 0) return 'error';
  if ((stat.sees_warning ?? 0) > 0) return 'warning';
  return 'healthy';
}

export function getNodeLabel(stat: DiskStat): string {
  if (typeof stat.node_id === 'number') {
    return `Node ${stat.node_id}`;
  }
  if (stat.key !== undefined && stat.key !== null) {
    return `Node ${stat.key}`;
  }
  return 'Unknown node';
}

export function formatTimestamp(timestamp?: string | null): string {
  if (!timestamp) return '—';
  const date = new Date(timestamp);
  if (Number.isNaN(date.getTime())) return timestamp;
  return date.toLocaleString();
}

export function formatRelativeTime(timestamp?: string | null): string {
  if (!timestamp) return '—';
  const target = new Date(timestamp);
  if (Number.isNaN(target.getTime())) return timestamp;
  const diffMs = target.getTime() - Date.now();
  const absSeconds = Math.round(Math.abs(diffMs) / 1000);
  if (absSeconds < 60) {
    return diffMs >= 0 ? 'in <1m' : '<1m ago';
  }
  const absMinutes = Math.round(absSeconds / 60);
  if (absMinutes < 60) {
    return diffMs >= 0 ? `in ${absMinutes}m` : `${absMinutes}m ago`;
  }
  const absHours = Math.round(absMinutes / 60);
  if (absHours < 24) {
    return diffMs >= 0 ? `in ${absHours}h` : `${absHours}h ago`;
  }
  const absDays = Math.round(absHours / 24);
  return diffMs >= 0 ? `in ${absDays}d` : `${absDays}d ago`;
}

export function calculateMemoryUsage(stat: DiskStat): number | null {
  const used = Number(stat.mem_used ?? 0);
  const avail = Number(stat.mem_avail ?? 0);
  const total = used + avail;
  if (!total) return null;
  return (used / total) * 100;
}

export function safeNumber(value?: number | null, fallback = 0) {
  if (value === undefined || value === null || Number.isNaN(Number(value))) {
    return fallback;
  }
  return Number(value);
}

export function sumStatField(stats: DiskStat[], field: keyof DiskStat) {
  return stats.reduce((sum, stat) => sum + safeNumber(stat[field] as number | null, 0), 0);
}

const NUMERIC_FIELDS: (keyof DiskStat)[] = [
  'cpu',
  'mem_used',
  'mem_avail',
  'read_iops',
  'write_iops',
  'total_iops',
  'writes_mbps',
  'reads_mbps',
  't_throughput',
  'c_net_in',
  'c_net_out',
  's_net_in',
  's_net_out',
  'meta_chan_ps',
  'avg_wr_latency',
  'avg_rd_latency',
  'sees_warning',
  'sees_error',
  'cont1_isok',
  'cont2_isok',
  'clients',
  'lavg',
];

export function aggregateStatsByNode(stats: DiskStat[]): DiskStat[] {
  const groups = new Map<string | number, {
    key: string | number;
    node_id: number | null;
    sums: Record<string, number>;
    counts: Record<string, number>;
    latest?: DiskStat;
  }>();

  for (const stat of stats) {
    const key = stat.node_id ?? stat.key ?? 'unknown';
    const existing = groups.get(key) ?? {
      key,
      node_id: typeof stat.node_id === 'number' ? stat.node_id : null,
      sums: {},
      counts: {},
      latest: undefined as DiskStat | undefined,
    };

    for (const field of NUMERIC_FIELDS) {
      const raw = stat[field];
      const num = Number(raw);
      if (!Number.isFinite(num)) continue;
      existing.sums[field as string] = (existing.sums[field as string] ?? 0) + num;
      existing.counts[field as string] = (existing.counts[field as string] ?? 0) + 1;
    }

    if (
      stat.s_ts &&
      (!existing.latest?.s_ts ||
        new Date(stat.s_ts).getTime() > new Date(existing.latest.s_ts).getTime())
    ) {
      existing.latest = stat;
    } else if (!existing.latest) {
      existing.latest = stat;
    }

    groups.set(key, existing);
  }

  return Array.from(groups.values()).map((group) => {
    const aggregated: DiskStat = {
      ...(group.latest ?? {}),
      key: group.key,
      node_id: group.node_id ?? (typeof group.key === 'number' ? group.key : null),
    };

    for (const field of NUMERIC_FIELDS) {
      const count = group.counts[field as string] ?? 0;
      aggregated[field] =
        count > 0 ? (group.sums[field as string] ?? 0) / count : null;
    }

    if (group.latest?.s_ts) {
      aggregated.s_ts = group.latest.s_ts;
    }

    return aggregated;
  });
}

export interface UseDiskStatsResult {
  stats: DiskStat[];
  isLoading: boolean;
  error: string | null;
  refresh: () => Promise<void>;
  lastUpdated: string | null;
}

export function useDiskStats(pollInterval = 0): UseDiskStatsResult {
  const [stats, setStats] = useState<DiskStat[]>([]);
  const [isLoading, setIsLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [lastUpdated, setLastUpdated] = useState<string | null>(null);
  const isMountedRef = useRef(true);

  useEffect(() => {
    return () => {
      isMountedRef.current = false;
    };
  }, []);

  const fetchStats = useCallback(async () => {
    setIsLoading(true);
    setError(null);
    try {
      const controller = new AbortController();
      const res = await fetch(`${API_BASE}/health/stats`, { signal: controller.signal });
      if (!res.ok) {
        throw new Error(`Failed to load stats (${res.status})`);
      }
      const payload: DiskStat[] = await res.json();
      if (!isMountedRef.current) return;
      setStats(Array.isArray(payload) ? payload : []);
      const latest = Array.isArray(payload)
        ? payload.reduce<string | null>((acc, stat) => {
            const ts = stat?.s_ts;
            if (!ts) return acc;
            if (!acc) return ts;
            return new Date(ts) > new Date(acc) ? ts : acc;
          }, null)
        : null;
      setLastUpdated(latest);
    } catch (err) {
      if (!isMountedRef.current) return;
      setError(err instanceof Error ? err.message : 'Unable to load stats');
    } finally {
      if (isMountedRef.current) {
        setIsLoading(false);
      }
    }
  }, []);

  useEffect(() => {
    fetchStats();

    if (pollInterval <= 0) {
      return;
    }

    const id = window.setInterval(() => {
      fetchStats();
    }, pollInterval);

    return () => {
      window.clearInterval(id);
    };
  }, [fetchStats, pollInterval]);

  return useMemo(
    () => ({
      stats,
      isLoading,
      error,
      refresh: fetchStats,
      lastUpdated,
    }),
    [stats, isLoading, error, fetchStats, lastUpdated],
  );
}
