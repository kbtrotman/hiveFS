import { useState } from 'react';
import { AlertTriangle, Power, RefreshCw, ShieldAlert, ShieldCheck, CheckCircle, XCircle, Plus } from 'lucide-react';
import { Card, CardContent, CardHeader, CardTitle } from '../../../ui/card';
import { Button } from '../../../ui/button';
import { Separator } from '../../../ui/separator';
import { Badge } from '../../../ui/badge';
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '../../../ui/select';

interface Alert {
  id: string;
  type: 'cluster' | 'node';
  severity: 'critical' | 'warning' | 'info';
  message: string;
  node?: string;
  timestamp: Date;
}

export function ClusterManageTab() {
  const [selectedNode, setSelectedNode] = useState<string>('node-1');
  const [knownClusters] = useState([
    { id: 'primary', name: 'Primary Cluster', location: 'San Jose', nodes: 18, status: 'healthy' },
    { id: 'dr-east', name: 'DR East', location: 'Ashburn', nodes: 12, status: 'warning' },
  ]);
  const [alerts, setAlerts] = useState<Alert[]>([
    {
      id: '1',
      type: 'node',
      severity: 'critical',
      message: 'High memory usage detected',
      node: 'node-3',
      timestamp: new Date(Date.now() - 1000 * 60 * 15),
    },
    {
      id: '2',
      type: 'cluster',
      severity: 'warning',
      message: 'Cluster quorum at minimum threshold',
      timestamp: new Date(Date.now() - 1000 * 60 * 45),
    },
    {
      id: '3',
      type: 'node',
      severity: 'warning',
      message: 'Network latency spike detected',
      node: 'node-2',
      timestamp: new Date(Date.now() - 1000 * 60 * 60 * 2),
    },
  ]);

  const formatTimeAgo = (date: Date) => {
    const minutes = Math.floor((Date.now() - date.getTime()) / 1000 / 60);
    if (minutes < 60) return `${minutes}m ago`;
    const hours = Math.floor(minutes / 60);
    return `${hours}h ago`;
  };

  const dismissAlert = (id: string) => {
    setAlerts(alerts.filter((alert) => alert.id !== id));
  };

  const getSeverityColor = (severity: string) => {
    switch (severity) {
      case 'critical':
        return 'bg-destructive text-destructive-foreground';
      case 'warning':
        return 'bg-amber-500 text-white';
      case 'info':
        return 'bg-blue-500 text-white';
      default:
        return 'bg-muted text-muted-foreground';
    }
  };

  return (
    <div className="flex h-full w-full flex-col gap-6 overflow-auto bg-gradient-to-br from-background via-primary/5 to-background p-8">
      {/* Page Header */}
      <div>
        <h1 className="text-2xl font-semibold text-foreground">Cluster Management</h1>
        <p className="mt-1 text-sm text-muted-foreground">
          Manage cluster operations, node states, and active alerts
        </p>
      </div>
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
          {knownClusters.map((cluster) => (
            <div
              key={cluster.id}
              className="flex flex-col rounded-lg border border-border/60 bg-muted/30 px-4 py-3 min-w-[220px]"
            >
              <div className="flex items-center justify-between">
                <p className="text-sm font-semibold">{cluster.name}</p>
                <Badge
                  className={cluster.status === 'healthy' ? 'bg-emerald-500' : 'bg-amber-500'}
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

      {/* Top Row - Cluster and Node Operations */}
      <div className="grid grid-cols-1 gap-6 lg:grid-cols-2">
        {/* Cluster Operations Card */}
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

            <p
              className="text-muted-foreground opacity-60"
              style={{ fontSize: '0.7rem', lineHeight: '1.3' }}
            >
              Cluster operations may temporarily affect service availability. Plan accordingly.
            </p>
          </CardContent>
        </Card>

        {/* Node Operations Card */}
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
                  <SelectItem value="node-1">node-1 (192.168.1.10)</SelectItem>
                  <SelectItem value="node-2">node-2 (192.168.1.11)</SelectItem>
                  <SelectItem value="node-3">node-3 (192.168.1.12)</SelectItem>
                  <SelectItem value="node-4">node-4 (192.168.1.13)</SelectItem>
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

            <div>
              <p className="text-sm text-foreground/75 mb-3">
                Fence or un-fence node to isolate or restore access.
              </p>
              <div className="flex gap-3">
                <Button variant="outline" className="flex-1">
                  <ShieldAlert className="w-4 h-4 mr-2 text-destructive" />
                  Fence Node
                </Button>
                <Button variant="outline" className="flex-1">
                  <ShieldCheck className="w-4 h-4 mr-2 text-green-600" />
                  Un-fence Node
                </Button>
              </div>
            </div>

            <p
              className="text-muted-foreground opacity-60"
              style={{ fontSize: '0.7rem', lineHeight: '1.3' }}
            >
              Fencing isolates a node from the cluster. Un-fencing restores normal operation.
            </p>
          </CardContent>
        </Card>
      </div>

      {/* Bottom Section - Active Alerts */}
      <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
        <CardHeader>
          <div className="flex items-center justify-between">
            <CardTitle className="text-foreground/90 flex items-center gap-2">
              <AlertTriangle className="w-5 h-5" />
              Active Alerts Requiring Action
            </CardTitle>
            <Badge variant="secondary" className="text-xs">
              {alerts.length} Active
            </Badge>
          </div>
        </CardHeader>
        <CardContent>
          {alerts.length === 0 ? (
            <div className="text-center py-8 text-muted-foreground">
              <CheckCircle className="w-12 h-12 mx-auto mb-3 opacity-50" />
              <p className="text-sm">No active alerts. All systems operating normally.</p>
            </div>
          ) : (
            <div className="space-y-3">
              {alerts.map((alert) => (
                <div
                  key={alert.id}
                  className="flex items-start gap-4 p-4 rounded-lg border border-border bg-muted/30"
                >
                  <div className="flex-1">
                    <div className="flex items-center gap-2 mb-1">
                      <Badge className={getSeverityColor(alert.severity)} style={{ fontSize: '0.65rem', padding: '2px 6px' }}>
                        {alert.severity.toUpperCase()}
                      </Badge>
                      <Badge variant="outline" className="text-xs">
                        {alert.type === 'cluster' ? 'Cluster-wide' : alert.node}
                      </Badge>
                      <span className="text-xs text-muted-foreground ml-auto">
                        {formatTimeAgo(alert.timestamp)}
                      </span>
                    </div>
                    <p className="text-sm text-foreground/90">{alert.message}</p>
                  </div>
                  <div className="flex gap-2">
                    <Button
                      size="sm"
                      variant="outline"
                      className="text-xs h-8"
                    >
                      <CheckCircle className="w-3 h-3 mr-1" />
                      Resolve
                    </Button>
                    <Button
                      size="sm"
                      variant="ghost"
                      className="text-xs h-8"
                      onClick={() => dismissAlert(alert.id)}
                    >
                      <XCircle className="w-3 h-3 mr-1" />
                      Dismiss
                    </Button>
                  </div>
                </div>
              ))}
            </div>
          )}

          <Separator className="my-4" />

          <div className="flex justify-between items-center">
            <p
              className="text-muted-foreground opacity-60"
              style={{ fontSize: '0.7rem', lineHeight: '1.3' }}
            >
              View all alerts and historical data on the Alerts page.
            </p>
            <Button variant="link" className="text-xs h-auto p-0">
              View All Alerts →
            </Button>
          </div>
        </CardContent>
      </Card>
    </div>
  );
}
