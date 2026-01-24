import { useMemo } from 'react';
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from '../../ui/card';
import { Button } from '../../ui/button';
import { Server, Cpu, HardDrive, Network } from 'lucide-react';
import {
  formatTimestamp,
  getNodeLabel,
  resolveStatHealth,
  aggregateStatsByNode,
  safeNumber,
  sumStatField,
  useDiskStats,
} from '../../useDiskStats';

const formatNumber = (value: number, options?: Intl.NumberFormatOptions) =>
  Number.isFinite(value) ? new Intl.NumberFormat(undefined, options).format(value) : '—';

export function NodesDashboardTab() {
  const { stats, isLoading, error, lastUpdated } = useDiskStats();
  const aggregated = useMemo(() => aggregateStatsByNode(stats), [stats]);

  const overview = useMemo(() => {
    const nodeCount = aggregated.length;
    const avgCpu =
      nodeCount > 0
        ? aggregated.reduce((sum, stat) => sum + safeNumber(stat.cpu), 0) / nodeCount
        : 0;
    const throughput = sumStatField(aggregated, 't_throughput');
    const avgLatency =
      nodeCount > 0
        ? aggregated.reduce((sum, stat) => sum + safeNumber(stat.avg_rd_latency), 0) / nodeCount
        : 0;
    const controllerAlerts = aggregated.filter(
      (stat) => (stat.cont1_isok ?? 1) === 0 || (stat.cont2_isok ?? 1) === 0,
    );

    return { nodeCount, avgCpu, throughput, avgLatency, controllerAlerts };
  }, [aggregated]);

  const topCpu = useMemo(
    () =>
      aggregated
        .slice()
        .sort((a, b) => safeNumber(b.cpu) - safeNumber(a.cpu))
        .slice(0, 5),
    [aggregated],
  );

  const alertingNodes = useMemo(
    () => aggregated.filter((stat) => resolveStatHealth(stat) !== 'healthy'),
    [aggregated],
  );

  return (
    <div className="space-y-6">
      <div className="flex justify-between items-center">
        <div>
          <h2>Nodes Dashboard</h2>
          <p className="text-muted-foreground">Configure your custom nodes dashboard view</p>
        </div>
        <Button>Customize Layout</Button>
      </div>

      <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-4 gap-4">
        <Card>
          <CardContent className="pt-6">
            <div className="flex items-center justify-between">
              <div>
                <p className="text-sm text-muted-foreground">Active Nodes</p>
                <p className="mt-2">{isLoading ? '—' : overview.nodeCount}</p>
              </div>
              <Server className="w-5 h-5 text-green-500" />
            </div>
          </CardContent>
        </Card>
        <Card>
          <CardContent className="pt-6">
            <div className="flex items-center justify-between">
              <div>
                <p className="text-sm text-muted-foreground">Avg CPU Load</p>
                <p className="mt-2">
                  {isLoading ? '—' : `${overview.avgCpu.toFixed(1)}%`}
                </p>
              </div>
              <Cpu className="w-5 h-5 text-blue-500" />
            </div>
          </CardContent>
        </Card>
        <Card>
          <CardContent className="pt-6">
            <div className="flex items-center justify-between">
              <div>
                <p className="text-sm text-muted-foreground">Total Throughput</p>
                <p className="mt-2">
                  {isLoading ? '—' : `${formatNumber(overview.throughput)} MB/s`}
                </p>
              </div>
              <HardDrive className="w-5 h-5 text-purple-500" />
            </div>
          </CardContent>
        </Card>
        <Card>
          <CardContent className="pt-6">
            <div className="flex items-center justify-between">
              <div>
                <p className="text-sm text-muted-foreground">Avg Read Latency</p>
                <p className="mt-2">
                  {isLoading ? '—' : `${formatNumber(overview.avgLatency, { maximumFractionDigits: 2 })} ms`}
                </p>
              </div>
              <Network className="w-5 h-5 text-orange-500" />
            </div>
          </CardContent>
        </Card>
      </div>

      {error && (
        <div className="rounded-md border border-destructive/40 bg-destructive/10 px-4 py-3 text-sm text-destructive">
          {error}
        </div>
      )}

      <Card>
        <CardHeader>
          <CardTitle>Top Utilization</CardTitle>
          <CardDescription>Nodes driving the highest CPU usage</CardDescription>
        </CardHeader>
        <CardContent className="space-y-3">
          {isLoading ? (
            <p className="text-sm text-muted-foreground">Loading node metrics…</p>
          ) : topCpu.length === 0 ? (
            <p className="text-sm text-muted-foreground">No stats to display.</p>
          ) : (
            topCpu.map((stat, index) => (
              <div
                key={stat.key ?? stat.node_id ?? index}
                className="flex items-center justify-between p-3 border border-border rounded-md"
              >
                <div>
                  <p>{getNodeLabel(stat)}</p>
                  <p className="text-sm text-muted-foreground">
                    {stat.s_ts ? formatTimestamp(stat.s_ts) : 'No timestamp'}
                  </p>
                </div>
                <div className="text-right">
                  <p className="text-sm text-muted-foreground">CPU</p>
                  <p className="text-sm">{safeNumber(stat.cpu).toFixed(1)}%</p>
                </div>
              </div>
            ))
          )}
        </CardContent>
      </Card>

      <Card>
        <CardHeader>
          <CardTitle>Alerts & Controllers</CardTitle>
          <CardDescription>Nodes reporting warnings or controller events</CardDescription>
        </CardHeader>
        <CardContent className="space-y-4">
          {alertingNodes.length === 0 && overview.controllerAlerts.length === 0 ? (
            <p className="text-sm text-muted-foreground">No active alerts reported.</p>
          ) : (
            <>
              {alertingNodes.map((stat, index) => (
                <div
                  key={`alert-${stat.key ?? stat.node_id ?? index}`}
                  className="p-3 border border-border rounded-md"
                >
                  <p className="font-medium">{getNodeLabel(stat)}</p>
                  <p className="text-sm text-muted-foreground">
                    Status: {resolveStatHealth(stat)} • {stat.message ?? 'No additional context'}
                  </p>
                </div>
              ))}
              {overview.controllerAlerts.map((stat, index) => (
                <div
                  key={`ctrl-${stat.key ?? stat.node_id ?? index}`}
                  className="p-3 border border-border rounded-md bg-secondary/30"
                >
                  <p className="font-medium">{getNodeLabel(stat)}</p>
                  <p className="text-sm text-muted-foreground">
                    Controller 1:{' '}
                    {(stat.cont1_isok ?? 1) === 1 ? 'OK' : stat.cont1_message ?? 'Issue detected'}
                  </p>
                  <p className="text-sm text-muted-foreground">
                    Controller 2:{' '}
                    {(stat.cont2_isok ?? 1) === 1 ? 'OK' : stat.cont2_message ?? 'Issue detected'}
                  </p>
                </div>
              ))}
            </>
          )}
          <p className="text-xs text-muted-foreground">
            Last updated {lastUpdated ? formatTimestamp(lastUpdated) : '—'}
          </p>
        </CardContent>
      </Card>
    </div>
  );
}
