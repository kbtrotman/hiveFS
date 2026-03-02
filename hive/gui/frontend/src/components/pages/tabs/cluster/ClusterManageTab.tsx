import { useEffect, useMemo, useState } from 'react';
import {
  AlertTriangle,
  Power,
  RefreshCw,
  ShieldAlert,
  ShieldCheck,
  CheckCircle,
  XCircle,
  Plus,
} from 'lucide-react';
import { Card, CardContent, CardHeader, CardTitle } from '../../../ui/card';
import { Button } from '../../../ui/button';
import { Separator } from '../../../ui/separator';
import { Badge } from '../../../ui/badge';
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '../../../ui/select';
import { useApiResource } from '../../../useApiResource';
import { aggregateStatsByNode, resolveStatHealth, useDiskStats } from '../../../useDiskStats';

interface StorageNode {
  node_id?: number | null;
  node_name?: string | null;
  node_address?: string | null;
  node_guard_port?: number | null;
  node_data_port?: number | null;
  fenced?: boolean | number | null;
}

interface AlertRecord {
  alert_id?: number | string;
  source?: string | null;
  title?: string | null;
  message?: string | null;
  severity?: 'critical' | 'warning' | 'info';
  status?: string | null;
  triggered_at?: string | null;
  metadata?: Record<string, unknown> | null;
}

interface SettingsPayload {
  cluster_notification_forwarder?: string | null;
  net_domain_name?: string | null;
  cluster_allow_gui_management?: boolean;
}

const severityBadge = {
  critical: 'bg-destructive text-destructive-foreground',
  warning: 'bg-amber-500 text-white',
  info: 'bg-blue-500 text-white',
};

const formatTimeAgo = (timestamp?: string | null) => {
  if (!timestamp) return 'unknown';
  const date = new Date(timestamp);
  const minutes = Math.floor((Date.now() - date.getTime()) / 60000);
  if (minutes < 1) return '<1m ago';
  if (minutes < 60) return `${minutes}m ago`;
  const hours = Math.floor(minutes / 60);
  if (hours < 24) return `${hours}h ago`;
  const days = Math.floor(hours / 24);
  return `${days}d ago`;
};

export function ClusterManageTab() {
  const { stats } = useDiskStats();
  const aggregated = useMemo(() => aggregateStatsByNode(stats), [stats]);

  const {
    data: nodes,
    isLoading: isLoadingNodes,
    error: nodesError,
  } = useApiResource<StorageNode[]>('nodes', {
    initialData: [],
    transform: (payload) => (Array.isArray(payload) ? payload : []),
  });
  const {
    data: rawAlerts,
    isLoading: isLoadingAlerts,
    error: alertsError,
  } = useApiResource<AlertRecord[]>('alerts?limit=25', {
    initialData: [],
    transform: (payload) => (Array.isArray(payload) ? payload : []),
  });
  const {
    data: settings,
    isLoading: isLoadingSettings,
  } = useApiResource<SettingsPayload | null>('settings', {
    initialData: null,
  });

  const [selectedNode, setSelectedNode] = useState<string>('');
  const [dismissedAlertIds, setDismissedAlertIds] = useState<Set<string>>(new Set());

  const nodeOptions = useMemo(
    () =>
      nodes.map((node) => ({
        id: String(node.node_id ?? node.node_name ?? Math.random()),
        label: `${node.node_name ?? `node-${node.node_id ?? 'n/a'}`} (${node.node_address ?? 'unknown'})`,
      })),
    [nodes],
  );

  useEffect(() => {
    if (!selectedNode && nodeOptions.length) {
      setSelectedNode(nodeOptions[0].id);
    }
  }, [nodeOptions, selectedNode]);

  const visibleAlerts = useMemo(
    () =>
      rawAlerts.filter((alert) => !dismissedAlertIds.has(String(alert.alert_id ?? alert.title))),
    [rawAlerts, dismissedAlertIds],
  );

  const clusterHealth = useMemo(() => {
    if (visibleAlerts.some((alert) => alert.severity === 'critical')) {
      return { status: 'critical', icon: <ShieldAlert className="w-4 h-4" /> };
    }
    if (visibleAlerts.some((alert) => alert.severity === 'warning' || alert.status === 'acknowledged')) {
      return { status: 'warning', icon: <AlertTriangle className="w-4 h-4" /> };
    }
    if (nodes.some((node) => node.fenced)) {
      return { status: 'maintenance', icon: <ShieldCheck className="w-4 h-4" /> };
    }
    const unhealthyStats = aggregated.filter((stat) => resolveStatHealth(stat) !== 'healthy').length;
    if (unhealthyStats > 0) {
      return { status: 'warning', icon: <AlertTriangle className="w-4 h-4" /> };
    }
    return { status: 'healthy', icon: <CheckCircle className="w-4 h-4" /> };
  }, [aggregated, nodes, visibleAlerts]);

  const knownClusters = useMemo(() => {
    if (!nodes.length) {
      return [];
    }
    return [
      {
        id: 'local',
        name: settings?.cluster_notification_forwarder || 'Local Cluster',
        location: settings?.net_domain_name || 'Unknown domain',
        nodes: nodes.length,
        status: clusterHealth.status,
      },
    ];
  }, [clusterHealth.status, nodes, settings]);

  const dismissAlert = (id: string) => {
    setDismissedAlertIds((prev) => {
      const next = new Set(prev);
      next.add(id);
      return next;
    });
  };

  const errorMessage = nodesError ?? alertsError;

  return (
    <div className="flex h-full w-full flex-col gap-6 overflow-auto bg-gradient-to-br from-background via-primary/5 to-background p-8">
      <div>
        <h1 className="text-2xl font-semibold text-foreground">Cluster Management</h1>
        <p className="mt-1 text-sm text-muted-foreground">
          Manage cluster operations, node states, and active alerts
        </p>
      </div>

      {(isLoadingNodes || isLoadingAlerts || isLoadingSettings) && (
        <div className="text-xs text-muted-foreground">Loading cluster data…</div>
      )}
      {errorMessage && (
        <div className="rounded-md border border-destructive/40 bg-destructive/10 px-4 py-2 text-sm text-destructive">
          {errorMessage}
        </div>
      )}

      <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
        <CardHeader className="flex flex-row items-center justify-between">
          <div>
            <CardTitle className="text-foreground/90">Known Clusters</CardTitle>
            <p className="text-sm text-muted-foreground">Manage local and connected peer clusters.</p>
          </div>
          <Button variant="outline">
            <Plus className="w-4 h-4 mr-2" />
            Add Foreign Cluster
          </Button>
        </CardHeader>
        <CardContent className="flex flex-wrap gap-4">
          {knownClusters.length === 0 && (
            <p className="text-sm text-muted-foreground">No nodes are reporting yet.</p>
          )}
          {knownClusters.map((cluster) => (
            <div
              key={cluster.id}
              className="flex flex-col rounded-lg border border-border/60 bg-muted/30 px-4 py-3 min-w-[220px]"
            >
              <div className="flex items-center justify-between">
                <p className="text-sm font-semibold">{cluster.name}</p>
                <Badge
                  className={
                    cluster.status === 'healthy'
                      ? 'bg-emerald-500'
                      : cluster.status === 'critical'
                        ? 'bg-destructive'
                        : 'bg-amber-500'
                  }
                >
                  {cluster.status}
                </Badge>
              </div>
              <p className="text-xs text-muted-foreground">{cluster.location}</p>
              <p className="text-xs text-muted-foreground mt-1">{cluster.nodes} nodes</p>
            </div>
          ))}
        </CardContent>
      </Card>

      <div className="grid grid-cols-1 gap-6 lg:grid-cols-2">
        <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
          <CardHeader>
            <CardTitle className="text-foreground/90 flex items-center gap-2">
              <Power className="w-5 h-5" />
              Cluster Operations
            </CardTitle>
          </CardHeader>
          <CardContent className="space-y-4">
            <div>
              <p className="text-sm text-foreground/75 mb-3">
                Pause cluster services for scheduled maintenance or emergency operations.
              </p>
              <div className="flex gap-3">
                <Button variant="outline" className="flex-1">
                  <Power className="w-4 h-4 mr-2" />
                  Pause Services
                </Button>
                <Button variant="outline" className="flex-1">
                  <RefreshCw className="w-4 h-4 mr-2" />
                  Resume Services
                </Button>
              </div>
            </div>

            <Separator />

            <div>
              <p className="text-sm text-foreground/75 mb-3">
                Perform cluster-wide operations that affect all nodes.
              </p>
              <Button variant="outline" className="w-full">
                <RefreshCw className="w-4 h-4 mr-2" />
                Rolling Restart All Nodes
              </Button>
            </div>

            <p className="text-muted-foreground opacity-60 text-xs leading-relaxed">
              Cluster status: {clusterHealth.status}. Keep critical alerts clear before scheduling maintenance.
            </p>
          </CardContent>
        </Card>

        <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
          <CardHeader>
            <CardTitle className="text-foreground/90 flex items-center gap-2">
              <RefreshCw className="w-5 h-5" />
              Node Operations
            </CardTitle>
          </CardHeader>
          <CardContent className="space-y-4">
            <div>
              <label htmlFor="node-select" className="text-sm block mb-2">
                Select Node
              </label>
              <Select value={selectedNode} onValueChange={setSelectedNode}>
                <SelectTrigger id="node-select" className="text-sm">
                  <SelectValue />
                </SelectTrigger>
                <SelectContent>
                  {nodeOptions.map((node) => (
                    <SelectItem key={node.id} value={node.id}>
                      {node.label}
                    </SelectItem>
                  ))}
                </SelectContent>
              </Select>
            </div>

            <div className="flex gap-3">
              <Button variant="outline" className="flex-1">
                <RefreshCw className="w-4 h-4 mr-2" />
                Restart Node
              </Button>
              <Button variant="outline" className="flex-1">
                <Power className="w-4 h-4 mr-2" />
                Shutdown Node
              </Button>
            </div>

            <Separator />

            <p className="text-xs text-muted-foreground">
              Selected node operations are limited to what the HiveFS guard port allows. Ensure the
              node is reachable on port {nodes.find((n) => String(n.node_id ?? n.node_name) === selectedNode)?.node_guard_port ?? 22}.
            </p>
          </CardContent>
        </Card>
      </div>

      <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
        <CardHeader>
          <CardTitle className="text-foreground/90 flex items-center gap-2">
            {clusterHealth.icon}
            Active Alerts
          </CardTitle>
        </CardHeader>
        <CardContent className="space-y-3">
          {isLoadingAlerts && (
            <p className="text-sm text-muted-foreground">Loading alerts…</p>
          )}
          {!isLoadingAlerts && visibleAlerts.length === 0 && (
            <p className="text-sm text-muted-foreground">No active alerts reported.</p>
          )}
          {visibleAlerts.map((alert) => {
            const severityClass =
              severityBadge[alert.severity ?? 'info'] ?? 'bg-muted text-muted-foreground';
            return (
              <div
                key={alert.alert_id ?? alert.title}
                className="flex items-start justify-between rounded-lg border border-border/60 p-3"
              >
                <div className="flex-1">
                  <div className="flex items-center gap-2 mb-1">
                    <Badge className={severityClass}>{alert.severity ?? 'info'}</Badge>
                    <p className="font-medium text-sm">{alert.title ?? alert.source ?? 'Alert'}</p>
                  </div>
                  <p className="text-xs text-muted-foreground mb-1">
                    {alert.message ?? 'Alert raised by workflow'} • {formatTimeAgo(alert.triggered_at)}
                  </p>
                  {alert.metadata?.node_id && (
                    <p className="text-xs text-muted-foreground">
                      Node {String(alert.metadata.node_id)}
                    </p>
                  )}
                </div>
                <div className="flex gap-2">
                  <Button
                    variant="ghost"
                    size="icon"
                    onClick={() => dismissAlert(String(alert.alert_id ?? alert.title))}
                  >
                    <XCircle className="w-4 h-4" />
                  </Button>
                </div>
              </div>
            );
          })}
        </CardContent>
      </Card>
    </div>
  );
}
