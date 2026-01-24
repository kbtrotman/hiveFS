import { useMemo } from 'react';
import { Card, CardContent, CardHeader, CardTitle } from '../../ui/card';
import { PerformanceChart } from '../../PerformanceChart';
import { Activity, HardDrive, Zap, TrendingUp } from 'lucide-react';
import { aggregateStatsByNode, formatTimestamp, safeNumber, sumStatField, useDiskStats } from '../../useDiskStats';

const formatNumber = (value: number, options?: Intl.NumberFormatOptions) => {
  if (!Number.isFinite(value)) return '—';
  return new Intl.NumberFormat(undefined, { maximumFractionDigits: 1, ...options }).format(value);
};

export function PerformanceTab() {
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
  }, [stats]);

  const chartData = useMemo(
    () =>
      stats.map((stat, index) => ({
        time: stat.s_ts ? formatTimestamp(stat.s_ts) : `Node ${index + 1}`,
        iops: safeNumber(stat.total_iops),
        reads: safeNumber(stat.read_iops),
        writes: safeNumber(stat.write_iops),
        throughput: safeNumber(stat.t_throughput),
      })),
    [stats],
  );

  const cards = [
    {
      label: 'Total IOPS',
      value: `${formatNumber(summary.totalIops)}`,
      subtext: `${summary.nodeCount} nodes reporting`,
      icon: Activity,
      color: 'text-blue-500',
    },
    {
      label: 'Read Ops/s',
      value: `${formatNumber(summary.readOps)}`,
      subtext: 'Cluster total',
      icon: HardDrive,
      color: 'text-green-500',
    },
    {
      label: 'Write Ops/s',
      value: `${formatNumber(summary.writeOps)}`,
      subtext: 'Cluster total',
      icon: TrendingUp,
      color: 'text-purple-500',
    },
    {
      label: 'Throughput',
      value: `${formatNumber(summary.throughput)} MB/s`,
      subtext: `Avg latency ${formatNumber(summary.avgLatency, { maximumFractionDigits: 2 })} ms`,
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
          title="IOPS"
          description="Input/Output Operations Per Second"
          dataKey="iops"
          color="#3b82f6"
          data={chartData}
          isLoading={isLoading}
        />
        <PerformanceChart
          title="Read Operations"
          description="Read operations per second"
          dataKey="reads"
          color="#10b981"
          data={chartData}
          isLoading={isLoading}
        />
        <PerformanceChart
          title="Write Operations"
          description="Write operations per second"
          dataKey="writes"
          color="#f59e0b"
          data={chartData}
          isLoading={isLoading}
        />
        <PerformanceChart
          title="Throughput"
          description="Data transfer rate (MB/s)"
          dataKey="throughput"
          color="#8b5cf6"
          data={chartData}
          isLoading={isLoading}
        />
      </div>

      {/* Additional Metrics */}
      <div className="grid grid-cols-1 lg:grid-cols-3 gap-6">
        <Card>
          <CardHeader>
            <CardTitle>Latency Overview</CardTitle>
          </CardHeader>
          <CardContent>
            <div className="space-y-3">
              <MetricRow label="Avg Read" value={`${formatNumber(summary.avgLatency)} ms`} />
              <MetricRow
                label="Avg Write"
                value={`${formatNumber(sumStatField(aggregated, 'avg_wr_latency') / (aggregated.length || 1))} ms`}
              />
              <MetricRow
                label="Meta Ops/s"
                value={`${formatNumber(sumStatField(aggregated, 'meta_chan_ps'))}`}
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
              <MetricRow label="Load Avg" value={formatNumber(sumStatField(aggregated, 'lavg') / (aggregated.length || 1), { maximumFractionDigits: 2 })} />
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
    <div className="flex justify-between items-center">
      <span className="text-sm text-muted-foreground">{label}</span>
      <span>{value}</span>
    </div>
  );
}
