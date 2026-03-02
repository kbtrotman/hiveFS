import { useMemo } from 'react';
import { Card, CardContent, CardHeader, CardTitle } from '../../../ui/card';
import { PerformanceChart } from '../../../PerformanceChart';
import { Activity, HardDrive, TrendingUp, Zap } from 'lucide-react';
import {
  aggregateStatsByNode,
  calculateMemoryUsage,
  formatTimestamp,
  getNodeLabel,
  safeNumber,
  sumStatField,
  useDiskStats,
} from '../../../useDiskStats';
import type { DiskStat } from '../../../useDiskStats';

const COLOR_PALETTE = ['#2563eb', '#7c3aed', '#059669', '#f97316', '#ec4899', '#0ea5e9'];
const MAX_POINTS = 50;

const formatNumber = (value: number, options?: Intl.NumberFormatOptions) => {
  if (!Number.isFinite(value)) return '—';
  return new Intl.NumberFormat(undefined, { maximumFractionDigits: 1, ...options }).format(value);
};

const formatMetricValue = (value: number | null, suffix = '', options?: Intl.NumberFormatOptions) => {
  if (value === null || Number.isNaN(value)) {
    return '—';
  }
  return `${formatNumber(value, options)}${suffix}`;
};

export function ServersPerformanceTab() {
  const { stats, isLoading, error, lastUpdated } = useDiskStats();
  const aggregated = useMemo(() => aggregateStatsByNode(stats), [stats]);

  const summary = useMemo(() => {
    const nodeCount = aggregated.length;
    const totalIops = sumStatField(aggregated, 'total_iops');
    const readOps = sumStatField(aggregated, 'read_iops');
    const writeOps = sumStatField(aggregated, 'write_iops');
    const throughput = sumStatField(aggregated, 't_throughput');
    const avgLatency =
      nodeCount > 0
        ? (sumStatField(aggregated, 'avg_rd_latency') + sumStatField(aggregated, 'avg_wr_latency')) /
          (nodeCount * 2)
        : 0;

    return {
      nodeCount,
      totalIops,
      readOps,
      writeOps,
      throughput,
      avgLatency,
    };
  }, [aggregated]);

  const topMetrics = useMemo(() => {
    const cpu = findTopMetric(aggregated, (stat) => (stat.cpu ?? null) as number | null);
    const memory = findTopMetric(aggregated, (stat) => calculateMemoryUsage(stat));
    const clientNet = findTopMetric(aggregated, (stat) =>
      safeNumber((stat.c_net_in as number | null) ?? 0) + safeNumber((stat.c_net_out as number | null) ?? 0),
    );
    const clusterNet = findTopMetric(aggregated, (stat) =>
      safeNumber((stat.s_net_in as number | null) ?? 0) + safeNumber((stat.s_net_out as number | null) ?? 0),
    );
    return { cpu, memory, clientNet, clusterNet };
  }, [aggregated]);

  const cpuSeries = useMemo(() => buildNodeSeriesData(stats, (stat) => (stat.cpu ?? null) as number | null), [stats]);
  const memorySeries = useMemo(
    () => buildNodeSeriesData(stats, (stat) => calculateMemoryUsage(stat)),
    [stats],
  );
  const clientInterfaceSeries = useMemo(
    () => buildNetworkSeries(stats, 'c_net_in', 'c_net_out'),
    [stats],
  );
  const clusterInterfaceSeries = useMemo(
    () => buildNetworkSeries(stats, 's_net_in', 's_net_out'),
    [stats],
  );
  const kvPutSeries = useMemo(
    () => buildNodeSeriesData(stats, (stat) => safeNumber(stat.kv_putblock_ps as number | null)),
    [stats],
  );
  const kvBytesSeries = useMemo(
    () => buildNodeSeriesData(stats, (stat) => safeNumber(stat.kv_putblock_bytes as number | null)),
    [stats],
  );

  const cards = [
    {
      label: 'Peak CPU Usage',
      value: formatMetricValue(topMetrics.cpu.value, '%'),
      subtext: topMetrics.cpu.label,
      icon: Activity,
      color: 'text-blue-500',
    },
    {
      label: 'Peak Memory Usage',
      value: formatMetricValue(topMetrics.memory.value, '%'),
      subtext: topMetrics.memory.label,
      icon: HardDrive,
      color: 'text-green-500',
    },
    {
      label: 'Top Client Interface',
      value: `${formatMetricValue(topMetrics.clientNet.value, ' MB/s')}`,
      subtext: topMetrics.clientNet.label,
      icon: TrendingUp,
      color: 'text-purple-500',
    },
    {
      label: 'Top Cluster Interface',
      value: `${formatMetricValue(topMetrics.clusterNet.value, ' MB/s')}`,
      subtext: topMetrics.clusterNet.label,
      icon: Zap,
      color: 'text-yellow-500',
    },
  ];

  return (
    <div className="space-y-6">
      {/* Statistics Cards */}
      <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-4 gap-4">
        {cards.map((stat, index) => {
          const Icon = stat.icon;
          return (
            <Card key={index}>
              <CardContent className="pt-6">
                <div className="flex items-start justify-between">
                  <div>
                    <p className="text-sm text-muted-foreground">{stat.label}</p>
                    <p className="mt-2 mb-1">{stat.value}</p>
                    <p className="text-xs text-muted-foreground">{stat.subtext}</p>
                  </div>
                  <Icon className={`w-5 h-5 ${stat.color}`} />
                </div>
              </CardContent>
            </Card>
          );
        })}
      </div>

      {error && (
        <div className="rounded-md border border-destructive/40 bg-destructive/10 px-4 py-3 text-sm text-destructive">
          {error}
        </div>
      )}

      {/* Performance Charts */}
      <div className="grid grid-cols-1 lg:grid-cols-2 gap-6">
        <PerformanceChart
          title="Node CPU Usage"
          description="CPU utilization per node (%)"
          data={cpuSeries.data}
          series={cpuSeries.series}
          isLoading={isLoading}
        />
        <PerformanceChart
          title="Memory Usage"
          description="Memory consumption per node (%)"
          data={memorySeries.data}
          series={memorySeries.series}
          isLoading={isLoading}
        />
        <PerformanceChart
          title="Network Client Interface"
          description="Client ingress vs egress (MB/s)"
          data={clientInterfaceSeries.data}
          series={clientInterfaceSeries.series}
          isLoading={isLoading}
        />
        <PerformanceChart
          title="Network Cluster Interface"
          description="Cluster ingress vs egress (MB/s)"
          data={clusterInterfaceSeries.data}
          series={clusterInterfaceSeries.series}
          isLoading={isLoading}
        />
        <PerformanceChart
          title="Key-Value Store Blocks Put/sec"
          description="Per-node KV block puts per second"
          data={kvPutSeries.data}
          series={kvPutSeries.series}
          isLoading={isLoading}
        />
        <PerformanceChart
          title="Key-Value Store Bytes/Sec"
          description="Per-node KV throughput (bytes/sec)"
          data={kvBytesSeries.data}
          series={kvBytesSeries.series}
          isLoading={isLoading}
        />
      </div>

      {/* Additional Metrics */}
      <div className="grid grid-cols-1 lg:grid-cols-3 gap-6">
        <Card>
          <CardHeader>
            <CardTitle>Key-Value Store Overview</CardTitle>
          </CardHeader>
          <CardContent>
            <div className="space-y-3">
              <MetricRow label="KV Put Calls" value={formatNumber(sumStatField(aggregated, 'kv_putblock_calls'))} />
              <MetricRow label="KV Dedup Hits" value={formatNumber(sumStatField(aggregated, 'kv_putblock_dedup_hits'))} />
              <MetricRow
                label="KV Dedup Misses"
                value={formatNumber(sumStatField(aggregated, 'kv_putblock_dedup_misses'))}
              />
              <MetricRow
                label="KV Writes"
                value={formatNumber(sumStatField(aggregated, 'kv_rocksdb_writes'))}
              />
              <MetricRow
                label="KV Write Time"
                value={`${formatNumber(sumStatField(aggregated, 'kv_rocksdb_write_ns') / 1e6, {
                  maximumFractionDigits: 2,
                })} ms`}
              />
              <MetricRow
                label="Contiguous Write Calls"
                value={formatNumber(sumStatField(aggregated, 'contig_write_calls'))}
              />
              <MetricRow
                label="Contiguous Write Bytes"
                value={`${formatNumber(sumStatField(aggregated, 'contig_write_bytes'))} B`}
              />
              <MetricRow label="Last Sample" value={lastUpdated ? formatTimestamp(lastUpdated) : '—'} />
            </div>
          </CardContent>
        </Card>

        <Card>
          <CardHeader>
            <CardTitle>Network Performance</CardTitle>
          </CardHeader>
          <CardContent>
            <div className="space-y-3">
              <MetricRow label="Client In" value={`${formatNumber(sumStatField(aggregated, 'c_net_in'))} MB/s`} />
              <MetricRow label="Client Out" value={`${formatNumber(sumStatField(aggregated, 'c_net_out'))} MB/s`} />
              <MetricRow label="Storage In" value={`${formatNumber(sumStatField(aggregated, 's_net_in'))} MB/s`} />
              <MetricRow label="Storage Out" value={`${formatNumber(sumStatField(aggregated, 's_net_out'))} MB/s`} />
            </div>
          </CardContent>
        </Card>

        <Card>
          <CardHeader>
            <CardTitle>Connected Clients</CardTitle>
          </CardHeader>
          <CardContent>
            <div className="space-y-3">
              <MetricRow label="Total" value={formatNumber(sumStatField(aggregated, 'clients'))} />
              <MetricRow
                label="Avg / Node"
                value={
                  summary.nodeCount
                    ? formatNumber(sumStatField(aggregated, 'clients') / summary.nodeCount)
                    : '—'
                }
              />
              <MetricRow
                label="Load Avg"
                value={formatNumber(sumStatField(aggregated, 'lavg') / (aggregated.length || 1), { maximumFractionDigits: 2 })}
              />
              <MetricRow
                label="Controller Alerts"
                value={`${aggregated.filter((s) => (s.cont1_isok ?? 1) === 0 || (s.cont2_isok ?? 1) === 0).length}`}
              />
            </div>
          </CardContent>
        </Card>
      </div>
    </div>
  );
}

interface MetricRowProps {
  label: string;
  value: string;
}

function MetricRow({ label, value }: MetricRowProps) {
  return (
    <div className="flex items-center justify-between py-1">
      <span className="text-sm text-muted-foreground">{label}</span>
      <span className="text-sm font-medium">{value}</span>
    </div>
  );
}

function findTopMetric(
  stats: DiskStat[],
  selector: (stat: DiskStat) => number | null | undefined,
): { value: number | null; label: string } {
  let best = -Infinity;
  let label = 'No data';

  stats.forEach((stat) => {
    const value = selector(stat);
    if (value === null || value === undefined || Number.isNaN(value)) {
      return;
    }
    if (value > best) {
      best = value;
      label = getNodeLabel(stat);
    }
  });

  if (best === -Infinity) {
    return { value: null, label };
  }
  return { value: best, label };
}

function buildNodeSeriesData(
  stats: DiskStat[],
  selector: (stat: DiskStat) => number | null | undefined,
) {
  const dataMap = new Map<
    string,
    {
      sortKey: number;
      time: string;
      [nodeLabel: string]: number;
    }
  >();
  const seriesOrder: string[] = [];

  stats.forEach((stat, index) => {
    const value = selector(stat);
    if (value === null || value === undefined || Number.isNaN(value)) {
      return;
    }
    const { mapKey, sortKey, label } = createTimePoint(stat.s_ts, index);
    const entry = dataMap.get(mapKey) ?? { sortKey, time: label };
    const nodeLabel = getNodeLabel(stat);
    entry[nodeLabel] = value;
    dataMap.set(mapKey, entry);
    if (!seriesOrder.includes(nodeLabel)) {
      seriesOrder.push(nodeLabel);
    }
  });

  const data = Array.from(dataMap.values())
    .sort((a, b) => a.sortKey - b.sortKey)
    .slice(-MAX_POINTS)
    .map(({ sortKey, ...rest }) => rest);

  const series = seriesOrder.map((nodeLabel, idx) => ({
    dataKey: nodeLabel,
    color: COLOR_PALETTE[idx % COLOR_PALETTE.length],
  }));

  return { data, series };
}

function buildNetworkSeries(
  stats: DiskStat[],
  inboundField: keyof DiskStat,
  outboundField: keyof DiskStat,
) {
  const dataMap = new Map<
    string,
    { sortKey: number; time: string; inbound: number; outbound: number }
  >();

  stats.forEach((stat, index) => {
    const { mapKey, sortKey, label } = createTimePoint(stat.s_ts, index);
    const entry = dataMap.get(mapKey) ?? { sortKey, time: label, inbound: 0, outbound: 0 };
    entry.inbound += safeNumber(stat[inboundField] as number | null);
    entry.outbound += safeNumber(stat[outboundField] as number | null);
    dataMap.set(mapKey, entry);
  });

  let data = Array.from(dataMap.values())
    .sort((a, b) => a.sortKey - b.sortKey)
    .slice(-MAX_POINTS)
    .map(({ sortKey, ...rest }) => rest);

  if (data.length === 0) {
    data = [{ time: 'No samples', inbound: 0, outbound: 0 }];
  }

  const series = [
    { dataKey: 'inbound', color: '#0ea5e9', label: 'Inbound' },
    { dataKey: 'outbound', color: '#f97316', label: 'Outbound' },
  ];

  return { data, series };
}

function createTimePoint(timestamp?: string | null, fallbackIndex = 0) {
  if (timestamp) {
    const date = new Date(timestamp);
    if (!Number.isNaN(date.getTime())) {
      return {
        mapKey: date.getTime().toString(),
        sortKey: date.getTime(),
        label: date.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' }),
      };
    }
  }
  return {
    mapKey: `sample-${fallbackIndex}`,
    sortKey: fallbackIndex,
    label: `Sample ${fallbackIndex + 1}`,
  };
}
