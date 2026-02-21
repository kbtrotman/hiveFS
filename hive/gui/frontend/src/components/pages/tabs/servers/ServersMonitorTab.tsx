import { useEffect, useMemo, useState } from 'react';
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from '../../../ui/card';
import { Button } from '../../../ui/button';
import { Badge } from '../../../ui/badge';
import { Table, TableBody, TableCell, TableHead, TableHeader, TableRow } from '../../../ui/table';
import { Server, Power, PowerOff, MoreVertical, Cpu, HardDrive, Network } from 'lucide-react';
import {
  aggregateStatsByNode,
  formatTimestamp,
  getNodeLabel,
  safeNumber,
  sumStatField,
  useDiskStats,
} from '../../../useDiskStats';

interface StorageNode {
  key: string;
  node: string;
  node_address: string;
  node_uid: string;
  node_serial: string;
  node_guard_port: number;
  node_data_port: number;
  last_heartbeat?: string | null;
  hive_version?: string | null;
  fenced?: boolean | null;
  last_maintenance?: string | null;
  date_added_to_cluster?: string | null;
  storage_capacity_bytes?: number | null;
  storage_used_bytes?: number | null;
  storage_reserved_bytes?: number | null;
  storage_overhead_bytes?: number | null;
  [key: string]: unknown;
}

interface StorageNodeStats {
  key: string;
  s_ts?: string | null;
  cpu?: number | null;
  read_iops?: number | null;
  write_iops?: number | null;
  total_iops?: number | null;
  t_throughput?: number | null;
  c_net_in?: number | null;
  c_net_out?: number | null;
  s_net_in?: number | null;
  s_net_out?: number | null;
  avg_latency?: number | null;
  [key: string]: unknown;
}

const API_BASE = import.meta?.env?.VITE_NODES_API_BASE_URL ?? 'http://localhost:8000/api/v1';

const formatNumber = (value: number, options?: Intl.NumberFormatOptions) =>
  Number.isFinite(value) ? new Intl.NumberFormat(undefined, options).format(value) : '—';

export function ServersMonitorTab() {
  const [nodes, setNodes] = useState<StorageNode[]>([]);
  const [stats, setStats] = useState<Record<string, StorageNodeStats>>({});
  const [isLoading, setIsLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const {
    stats: perfStats,
    isLoading: perfLoading,
  } = useDiskStats();

  useEffect(() => {
    let isMounted = true;

    async function fetchData() {
      setIsLoading(true);
      setError(null);
      try {
        const [nodeRes, statsRes] = await Promise.all([
          fetch(`${API_BASE}/nodes`),
          fetch(`${API_BASE}/snstats`),
        ]);

        if (!nodeRes.ok) {
          throw new Error(`Failed to load nodes (${nodeRes.status})`);
        }
        if (!statsRes.ok) {
          throw new Error(`Failed to load stats (${statsRes.status})`);
        }

        const nodesPayload: StorageNode[] = await nodeRes.json();
        const statsPayload: StorageNodeStats[] = await statsRes.json();

        if (!isMounted) return;
        setNodes(nodesPayload);
        setStats(
          statsPayload.reduce<Record<string, StorageNodeStats>>((acc, stat) => {
            if (stat.key) acc[stat.key] = stat;
            return acc;
          }, {}),
        );
      } catch (err) {
        if (!isMounted) return;
        setError(err instanceof Error ? err.message : 'Unable to load nodes');
      } finally {
        if (isMounted) {
          setIsLoading(false);
        }
      }
    }

    fetchData();
    return () => {
      isMounted = false;
    };
  }, []);

  const { totalNodes, runningNodes, stoppedNodes, clusterHealth } = useMemo(() => {
    const total = nodes.length;
    const running = nodes.filter((node) => !node.fenced).length;
    const stopped = total - running;
    return {
      totalNodes: total,
      runningNodes: running,
      stoppedNodes: stopped,
      clusterHealth: stopped > 0 ? 'Attention required' : total > 0 ? 'Healthy' : 'Unknown',
    };
  }, [nodes]);

  const aggregated = useMemo(() => aggregateStatsByNode(perfStats), [perfStats]);

  const perfOverview = useMemo(() => {
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

    return { nodeCount, avgCpu, throughput, avgLatency };
  }, [aggregated]);

  const topCpu = useMemo(
    () =>
      aggregated
        .slice()
        .sort((a, b) => safeNumber(b.cpu) - safeNumber(a.cpu))
        .slice(0, 5),
    [aggregated],
  );

  const formatPercent = (value?: number | null, fractionDigits = 0) => {
    if (value === null || value === undefined || Number.isNaN(value)) return '—';
    return `${value.toFixed(fractionDigits)}%`;
  };

  const formatDiskUsage = (node: StorageNode) => {
    const cap = Number(node.storage_capacity_bytes ?? 0);
    const used = Number(node.storage_used_bytes ?? 0);
    if (!cap) return '—';
    return `${Math.round((used / cap) * 100)}%`;
  };

  const formatHeartbeat = (timestamp?: string | null) => {
    if (!timestamp) return '—';
    try {
      return new Date(timestamp).toLocaleString();
    } catch {
      return timestamp;
    }
  };

  const resolveStatus = (node: StorageNode) => (node.fenced ? 'fenced' : 'running');

  const combinedStats = [
    {
      key: 'total-nodes',
      label: 'Total Nodes',
      value: isLoading ? '…' : totalNodes,
      icon: <Server className="w-5 h-5 text-blue-500" />,
    },
    {
      key: 'running-nodes',
      label: 'Running',
      value: isLoading ? '…' : runningNodes,
      icon: <Power className="w-5 h-5 text-green-500" />,
    },
    {
      key: 'stopped-nodes',
      label: 'Stopped',
      value: isLoading ? '…' : stoppedNodes,
      icon: <PowerOff className="w-5 h-5 text-red-500" />,
    },
    {
      key: 'cluster-health',
      label: 'Cluster Health',
      value: isLoading ? '—' : clusterHealth,
      icon: <Server className="w-5 h-5 text-green-500" />,
    },
    {
      key: 'active-nodes',
      label: 'Active Nodes',
      value: perfLoading ? '—' : perfOverview.nodeCount,
      icon: <Server className="w-5 h-5 text-emerald-500" />,
    },
    {
      key: 'avg-cpu',
      label: 'Avg CPU Load',
      value: perfLoading ? '—' : `${perfOverview.avgCpu.toFixed(1)}%`,
      icon: <Cpu className="w-5 h-5 text-blue-500" />,
    },
    {
      key: 'throughput',
      label: 'Total Throughput',
      value: perfLoading ? '—' : `${formatNumber(perfOverview.throughput)} MB/s`,
      icon: <HardDrive className="w-5 h-5 text-purple-500" />,
    },
    {
      key: 'avg-latency',
      label: 'Avg Read Latency',
      value: perfLoading ? '—' : `${formatNumber(perfOverview.avgLatency, { maximumFractionDigits: 2 })} ms`,
      icon: <Network className="w-5 h-5 text-orange-500" />,
    },
  ];

  return (
    <div className="space-y-6">
      <div>
        <h2>Node Monitor</h2>
        <p className="text-muted-foreground">Monitor and manage your HiveFS node cluster</p>
      </div>

      <div className="grid gap-4 sm:grid-cols-2 lg:grid-cols-4 xl:grid-cols-8 2xl:grid-cols-8">
        {combinedStats.map((stat) => (
          <Card key={stat.key}>
            <CardContent className="pt-6">
              <div className="flex items-center justify-between">
                <div>
                  <p className="text-sm text-muted-foreground">{stat.label}</p>
                  <p className="mt-2">{stat.value}</p>
                </div>
                {stat.icon}
              </div>
            </CardContent>
          </Card>
        ))}
      </div>

      <Card>
        <CardHeader>
          <CardTitle>Top Utilization</CardTitle>
          <CardDescription>Nodes driving the highest CPU usage</CardDescription>
        </CardHeader>
        <CardContent className="space-y-3">
          {perfLoading ? (
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

      {error && (
        <div className="rounded-md border border-destructive/40 bg-destructive/10 px-4 py-3 text-sm text-destructive">
          {error}
        </div>
      )}

      <Card>
        <CardHeader>
          <CardTitle>Node List</CardTitle>
          <CardDescription>All nodes in the HiveFS cluster</CardDescription>
        </CardHeader>
        <CardContent>
          <Table>
            <TableHeader>
              <TableRow>
                <TableHead>Node Name</TableHead>
                <TableHead>Version</TableHead>
                <TableHead>Status</TableHead>
                <TableHead>CPU</TableHead>
                <TableHead>Throughput</TableHead>
                <TableHead>Disk Usage</TableHead>
                <TableHead>Last Heartbeat</TableHead>
                <TableHead></TableHead>
              </TableRow>
            </TableHeader>
            <TableBody>
              {isLoading ? (
                <TableRow>
                  <TableCell colSpan={8} className="text-center text-muted-foreground">
                    Loading nodes…
                  </TableCell>
                </TableRow>
              ) : nodes.length === 0 ? (
                <TableRow>
                  <TableCell colSpan={8} className="text-center text-muted-foreground">
                    No nodes found.
                  </TableCell>
                </TableRow>
              ) : (
                nodes.map((node) => {
                  const nodeStats = stats[node.key];
                  const status = resolveStatus(node);
                  return (
                    <TableRow key={node.key}>
                      <TableCell>{node.node}</TableCell>
                      <TableCell>
                        <Badge variant="outline">{node.hive_version ?? 'Unknown'}</Badge>
                      </TableCell>
                      <TableCell>
                        <Badge variant={status === 'running' ? 'default' : 'secondary'}>
                          {status}
                        </Badge>
                      </TableCell>
                      <TableCell className="text-muted-foreground">
                        {formatPercent(nodeStats?.cpu ?? null)}
                      </TableCell>
                      <TableCell className="text-muted-foreground">
                        {nodeStats?.t_throughput ? `${nodeStats.t_throughput} MB/s` : '—'}
                      </TableCell>
                      <TableCell className="text-muted-foreground">{formatDiskUsage(node)}</TableCell>
                      <TableCell className="text-muted-foreground">
                        {formatHeartbeat(node.last_heartbeat)}
                      </TableCell>
                      <TableCell>
                        <Button variant="ghost" size="icon">
                          <MoreVertical className="w-4 h-4" />
                        </Button>
                      </TableCell>
                    </TableRow>
                  );
                })
              )}
            </TableBody>
          </Table>
        </CardContent>
      </Card>
    </div>
  );
}
