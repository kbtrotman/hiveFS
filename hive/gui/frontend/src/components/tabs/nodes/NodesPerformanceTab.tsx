import { useMemo, type ReactNode } from 'react';
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from '../../ui/card';
import { Activity, TrendingUp, TrendingDown } from 'lucide-react';
import {
  calculateMemoryUsage,
  getNodeLabel,
  aggregateStatsByNode,
  safeNumber,
  sumStatField,
  useDiskStats,
} from '../../useDiskStats';

const formatNumber = (value: number, options?: Intl.NumberFormatOptions) =>
  Number.isFinite(value) ? new Intl.NumberFormat(undefined, options).format(value) : '—';

export function NodesPerformanceTab() {
  const { stats, isLoading, error } = useDiskStats();
  const aggregated = useMemo(() => aggregateStatsByNode(stats), [stats]);

  const totals = useMemo(() => {
    const totalIops = sumStatField(aggregated, 'total_iops');
    const readOps = sumStatField(aggregated, 'read_iops');
    const writeOps = sumStatField(aggregated, 'write_iops');
    const throughput = sumStatField(aggregated, 't_throughput');
    const latency =
      aggregated.length > 0
        ? aggregated.reduce((sum, stat) => sum + safeNumber(stat.avg_rd_latency), 0) /
          aggregated.length
        : 0;
    return { totalIops, readOps, writeOps, throughput, latency };
  }, [aggregated]);

  const cpuData = useMemo(
    () =>
      aggregated
        .map((stat, index) => ({
          label: getNodeLabel(stat),
          value: safeNumber(stat.cpu),
          key: stat.key ?? stat.node_id ?? index,
        }))
        .sort((a, b) => b.value - a.value),
    [aggregated],
  );

  const memoryData = useMemo(
    () =>
      aggregated
        .map((stat, index) => ({
          label: getNodeLabel(stat),
          value: calculateMemoryUsage(stat),
          key: stat.key ?? stat.node_id ?? index,
        }))
        .sort((a, b) => (b.value ?? 0) - (a.value ?? 0)),
    [aggregated],
  );

  return (
    <div className="space-y-6">
      <div>
        <h2>Node Performance Metrics</h2>
        <p className="text-muted-foreground">Detailed performance statistics for all nodes</p>
      </div>

      <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-4 gap-4">
        <SummaryCard
          label="Total IOPS"
          value={formatNumber(totals.totalIops)}
          icon={<Activity className="w-5 h-5 text-blue-500" />}
        />
        <SummaryCard
          label="Read Ops/s"
          value={formatNumber(totals.readOps)}
          icon={<Activity className="w-5 h-5 text-green-500" />}
        />
        <SummaryCard
          label="Write Ops/s"
          value={formatNumber(totals.writeOps)}
          icon={<TrendingDown className="w-5 h-5 text-purple-500" />}
        />
        <SummaryCard
          label="Throughput"
          value={`${formatNumber(totals.throughput)} MB/s`}
          icon={<TrendingUp className="w-5 h-5 text-orange-500" />}
          subtext={`Avg latency ${formatNumber(totals.latency, { maximumFractionDigits: 2 })} ms`}
        />
      </div>

      {error && (
        <div className="rounded-md border border-destructive/40 bg-destructive/10 px-4 py-3 text-sm text-destructive">
          {error}
        </div>
      )}

      <div className="grid grid-cols-1 lg:grid-cols-2 gap-6">
        <Card>
          <CardHeader>
            <CardTitle>CPU Usage by Node</CardTitle>
            <CardDescription>Current CPU utilization across all nodes</CardDescription>
          </CardHeader>
          <CardContent className="space-y-4">
            {isLoading ? (
              <p className="text-sm text-muted-foreground">Loading CPU metrics…</p>
            ) : cpuData.length === 0 ? (
              <p className="text-sm text-muted-foreground">No CPU data available.</p>
            ) : (
              cpuData.map((node) => (
                <ProgressRow key={node.key} label={node.label} value={node.value} colorClass="bg-primary" />
              ))
            )}
          </CardContent>
        </Card>

        <Card>
          <CardHeader>
            <CardTitle>Memory Usage by Node</CardTitle>
            <CardDescription>Current memory utilization across all nodes</CardDescription>
          </CardHeader>
          <CardContent className="space-y-4">
            {isLoading ? (
              <p className="text-sm text-muted-foreground">Loading memory metrics…</p>
            ) : memoryData.length === 0 ? (
              <p className="text-sm text-muted-foreground">No memory data available.</p>
            ) : (
              memoryData.map((node) => (
                <ProgressRow
                  key={node.key}
                  label={node.label}
                  value={node.value ?? 0}
                  colorClass="bg-green-500"
                />
              ))
            )}
          </CardContent>
        </Card>
      </div>

      <Card>
        <CardHeader>
          <CardTitle>Network Performance</CardTitle>
          <CardDescription>Network throughput and latency metrics</CardDescription>
        </CardHeader>
        <CardContent>
          <div className="grid grid-cols-1 md:grid-cols-3 gap-4">
            <MetricBox label="Storage In" value={`${formatNumber(sumStatField(aggregated, 's_net_in'))} MB/s`} />
            <MetricBox label="Storage Out" value={`${formatNumber(sumStatField(aggregated, 's_net_out'))} MB/s`} />
            <MetricBox
              label="Client In"
              value={`${formatNumber(sumStatField(aggregated, 'c_net_in'))} MB/s`}
            />
            <MetricBox
              label="Client Out"
              value={`${formatNumber(sumStatField(aggregated, 'c_net_out'))} MB/s`}
            />
            <MetricBox
              label="Meta Ops/s"
              value={formatNumber(sumStatField(aggregated, 'meta_chan_ps'))}
            />
            <MetricBox
              label="Avg Load"
              value={
                aggregated.length > 0
                  ? formatNumber(sumStatField(aggregated, 'lavg') / aggregated.length, {
                      maximumFractionDigits: 2,
                    })
                  : '—'
              }
            />
          </div>
        </CardContent>
      </Card>
    </div>
  );
}

interface SummaryCardProps {
  label: string;
  value: string;
  icon: ReactNode;
  subtext?: string;
}

function SummaryCard({ label, value, icon, subtext }: SummaryCardProps) {
  return (
    <Card>
      <CardContent className="pt-6">
        <div className="flex items-center justify-between">
          <div>
            <p className="text-sm text-muted-foreground">{label}</p>
            <p className="mt-2">{value}</p>
            {subtext && <p className="text-xs text-muted-foreground">{subtext}</p>}
          </div>
          {icon}
        </div>
      </CardContent>
    </Card>
  );
}

interface ProgressRowProps {
  label: string;
  value: number;
  colorClass: string;
}

function ProgressRow({ label, value, colorClass }: ProgressRowProps) {
  return (
    <div className="space-y-2">
      <div className="flex justify-between text-sm">
        <span>{label}</span>
        <span>{Number.isFinite(value) ? `${value.toFixed(1)}%` : '—'}</span>
      </div>
      <div className="w-full bg-muted rounded-full h-2">
        <div className={`${colorClass} h-2 rounded-full`} style={{ width: `${Math.min(value, 100)}%` }} />
      </div>
    </div>
  );
}

interface MetricBoxProps {
  label: string;
  value: string;
}

function MetricBox({ label, value }: MetricBoxProps) {
  return (
    <div className="p-4 border border-border rounded-lg">
      <p className="text-sm text-muted-foreground">{label}</p>
      <p className="mt-2">{value}</p>
    </div>
  );
}
