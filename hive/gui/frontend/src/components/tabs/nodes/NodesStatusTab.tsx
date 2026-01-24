import { useMemo } from 'react';
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from '../../ui/card';
import { Badge } from '../../ui/badge';
import { Server, CheckCircle, AlertCircle, Activity } from 'lucide-react';
import {
  calculateMemoryUsage,
  formatTimestamp,
  getNodeLabel,
  resolveStatHealth,
  aggregateStatsByNode,
  safeNumber,
  useDiskStats,
} from '../../useDiskStats';

export function NodesStatusTab() {
  const { stats, isLoading, error } = useDiskStats();
  const aggregated = useMemo(() => aggregateStatsByNode(stats), [stats]);

  const summary = useMemo(() => {
    const healthy = aggregated.filter((stat) => resolveStatHealth(stat) === 'healthy').length;
    const warnings = aggregated.filter((stat) => resolveStatHealth(stat) === 'warning').length;
    const avgCpu =
      aggregated.length > 0
        ? aggregated.reduce((sum, stat) => sum + safeNumber(stat.cpu), 0) / aggregated.length
        : 0;
    return {
      healthy,
      total: aggregated.length,
      warnings,
      avgCpu,
    };
  }, [aggregated]);

  const nodeRows = useMemo(
    () =>
      aggregated.map((stat, index) => ({
        id: stat.key ?? stat.node_id ?? index,
        name: getNodeLabel(stat),
        status: resolveStatHealth(stat),
        cpu: safeNumber(stat.cpu),
        memory: calculateMemoryUsage(stat),
        timestamp: stat.s_ts,
      })),
    [aggregated],
  );

  return (
    <div className="space-y-6">
      <div>
        <h2>Node Status Dashboard</h2>
        <p className="text-muted-foreground">Real-time monitoring of filesystem nodes</p>
      </div>

      <div className="grid grid-cols-1 md:grid-cols-3 gap-4">
        <Card>
          <CardContent className="pt-6">
            <div className="flex items-center gap-3">
              <CheckCircle className="w-5 h-5 text-green-500" />
              <div>
                <p className="text-sm text-muted-foreground">Healthy Nodes</p>
                <p className="mt-1">
                  {isLoading ? '—' : `${summary.healthy} / ${summary.total || '0'}`}
                </p>
              </div>
            </div>
          </CardContent>
        </Card>
        <Card>
          <CardContent className="pt-6">
            <div className="flex items-center gap-3">
              <AlertCircle className="w-5 h-5 text-yellow-500" />
              <div>
                <p className="text-sm text-muted-foreground">Warnings</p>
                <p className="mt-1">{isLoading ? '—' : summary.warnings}</p>
              </div>
            </div>
          </CardContent>
        </Card>
        <Card>
          <CardContent className="pt-6">
            <div className="flex items-center gap-3">
              <Activity className="w-5 h-5 text-blue-500" />
              <div>
                <p className="text-sm text-muted-foreground">Avg CPU Load</p>
                <p className="mt-1">
                  {isLoading ? '—' : `${summary.avgCpu.toFixed(1)}%`}
                </p>
              </div>
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
          <CardTitle>Node Status</CardTitle>
          <CardDescription>Current status of all nodes in the cluster</CardDescription>
        </CardHeader>
        <CardContent>
          <div className="space-y-3">
            {isLoading ? (
              <p className="text-sm text-muted-foreground px-4">Loading node status…</p>
            ) : nodeRows.length === 0 ? (
              <p className="text-sm text-muted-foreground px-4">No stats available.</p>
            ) : (
              nodeRows.map((node) => (
                <div
                  key={node.id}
                  className="flex items-center justify-between p-4 border border-border rounded-lg"
                >
                  <div className="flex items-center gap-3">
                    <Server className="w-5 h-5 text-muted-foreground" />
                    <div>
                      <p>{node.name}</p>
                      <p className="text-sm text-muted-foreground">
                        Sampled: {node.timestamp ? formatTimestamp(node.timestamp) : '—'}
                      </p>
                    </div>
                  </div>
                  <div className="flex items-center gap-6">
                    <div className="text-right">
                      <p className="text-sm text-muted-foreground">CPU</p>
                      <p className="text-sm">
                        {Number.isFinite(node.cpu) ? `${node.cpu}%` : '—'}
                      </p>
                    </div>
                    <div className="text-right">
                      <p className="text-sm text-muted-foreground">Memory</p>
                      <p className="text-sm">
                        {node.memory !== null ? `${node.memory.toFixed(1)}%` : '—'}
                      </p>
                    </div>
                    <Badge
                      variant={
                        node.status === 'error'
                          ? 'destructive'
                          : node.status === 'warning'
                            ? 'secondary'
                            : 'default'
                      }
                    >
                      {node.status === 'healthy' ? 'Healthy' : node.status === 'warning' ? 'Warning' : 'Error'}
                    </Badge>
                  </div>
                </div>
              ))
            )}
          </div>
        </CardContent>
      </Card>
    </div>
  );
}
