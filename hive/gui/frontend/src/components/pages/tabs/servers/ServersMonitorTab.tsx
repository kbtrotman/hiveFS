import { useEffect, useMemo, useState } from 'react';
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from '../../../ui/card';
import { Button } from '../../../ui/button';
import { Badge } from '../../../ui/badge';
import { Table, TableBody, TableCell, TableHead, TableHeader, TableRow } from '../../../ui/table';
import { Server, Plus, Power, PowerOff, MoreVertical } from 'lucide-react';

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

export function ServersMonitorTab() {
  const [nodes, setNodes] = useState<StorageNode[]>([]);
  const [stats, setStats] = useState<Record<string, StorageNodeStats>>({});
  const [isLoading, setIsLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

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

  return (
    <div className="space-y-6">
      <div className="flex justify-between items-center">
        <div>
          <h2>Node Management</h2>
          <p className="text-muted-foreground">Monitor and manage your HiveFS node cluster</p>
        </div>
        <Button>
          <Plus className="w-4 h-4 mr-2" />
          Add Node
        </Button>
      </div>

      <div className="grid grid-cols-1 md:grid-cols-4 gap-4">
        <Card>
          <CardContent className="pt-6">
            <div className="flex items-center justify-between">
              <div>
                <p className="text-sm text-muted-foreground">Total Nodes</p>
                <p className="mt-2">{isLoading ? '…' : totalNodes}</p>
              </div>
              <Server className="w-5 h-5 text-blue-500" />
            </div>
          </CardContent>
        </Card>
        <Card>
          <CardContent className="pt-6">
            <div className="flex items-center justify-between">
              <div>
                <p className="text-sm text-muted-foreground">Running</p>
                <p className="mt-2 text-green-500">{isLoading ? '…' : runningNodes}</p>
              </div>
              <Power className="w-5 h-5 text-green-500" />
            </div>
          </CardContent>
        </Card>
        <Card>
          <CardContent className="pt-6">
            <div className="flex items-center justify-between">
              <div>
                <p className="text-sm text-muted-foreground">Stopped</p>
                <p className="mt-2 text-red-500">{isLoading ? '…' : stoppedNodes}</p>
              </div>
              <PowerOff className="w-5 h-5 text-red-500" />
            </div>
          </CardContent>
        </Card>
        <Card>
          <CardContent className="pt-6">
            <p className="text-sm text-muted-foreground">Cluster Health</p>
            <p className="mt-2 text-green-500">{isLoading ? '—' : clusterHealth}</p>
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
