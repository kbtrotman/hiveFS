import { useMemo } from 'react';
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from '../../ui/card';
import { Button } from '../../ui/button';
import { Badge } from '../../ui/badge';
import { User, Plus, Settings } from 'lucide-react';
import {
  getNodeLabel,
  resolveStatHealth,
  aggregateStatsByNode,
  safeNumber,
  sumStatField,
  useDiskStats,
} from '../../useDiskStats';

export function NodesClientsTab() {
  const { stats, isLoading, error } = useDiskStats();
  const aggregated = useMemo(() => aggregateStatsByNode(stats), [stats]);

  const overview = useMemo(() => {
    const totalClients = sumStatField(aggregated, 'clients');
    const active = aggregated.filter((stat) => safeNumber(stat.clients) > 0).length;
    const idle = aggregated.length - active;
    return { totalClients, active, idle };
  }, [aggregated]);

  const nodeClients = useMemo(
    () =>
      aggregated
        .map((stat, index) => ({
          id: stat.key ?? stat.node_id ?? index,
          name: getNodeLabel(stat),
          clients: safeNumber(stat.clients),
          status: resolveStatHealth(stat),
        }))
        .sort((a, b) => b.clients - a.clients),
    [aggregated],
  );

  const totalClientCount = overview.totalClients || 1;

  return (
    <div className="space-y-6">
      <div className="flex justify-between items-center">
        <div>
          <h2>Client Configuration</h2>
          <p className="text-muted-foreground">Manage clients and their node assignments</p>
        </div>
        <Button>
          <Plus className="w-4 h-4 mr-2" />
          Add Client
        </Button>
      </div>

      <div className="grid grid-cols-1 md:grid-cols-3 gap-4">
        <MetricCard label="Total Clients" value={overview.totalClients} color="text-blue-500" />
        <MetricCard label="Active Nodes" value={overview.active} color="text-green-500" />
        <MetricCard label="Idle Nodes" value={overview.idle} color="text-muted-foreground" />
      </div>

      {error && (
        <div className="rounded-md border border-destructive/40 bg-destructive/10 px-4 py-3 text-sm text-destructive">
          {error}
        </div>
      )}

      <Card>
        <CardHeader>
          <CardTitle>Client List</CardTitle>
          <CardDescription>All clients and their current node assignments</CardDescription>
        </CardHeader>
        <CardContent>
          <div className="space-y-3">
            {isLoading ? (
              <p className="text-sm text-muted-foreground px-4">Loading client data…</p>
            ) : nodeClients.length === 0 ? (
              <p className="text-sm text-muted-foreground px-4">No client stats available.</p>
            ) : (
              nodeClients.map((client) => (
                <div
                  key={client.id}
                  className="flex items-center justify-between p-4 border border-border rounded-lg"
                >
                  <div className="flex items-center gap-3">
                    <User className="w-5 h-5 text-muted-foreground" />
                    <div>
                      <p>{client.name}</p>
                      <p className="text-sm text-muted-foreground">
                        {client.clients} active client{client.clients === 1 ? '' : 's'}
                      </p>
                    </div>
                  </div>
                  <div className="flex items-center gap-6">
                    <Badge
                      variant={
                        client.status === 'error'
                          ? 'destructive'
                          : client.status === 'warning'
                            ? 'secondary'
                            : 'default'
                      }
                    >
                      {client.status}
                    </Badge>
                    <Button variant="ghost" size="icon">
                      <Settings className="w-4 h-4" />
                    </Button>
                  </div>
                </div>
              ))
            )}
          </div>
        </CardContent>
      </Card>

      <Card>
        <CardHeader>
          <CardTitle>Client Distribution</CardTitle>
          <CardDescription>How clients are distributed across nodes</CardDescription>
        </CardHeader>
        <CardContent>
          <div className="space-y-4">
            {isLoading ? (
              <p className="text-sm text-muted-foreground">Building distribution…</p>
            ) : nodeClients.length === 0 ? (
              <p className="text-sm text-muted-foreground">No data to display.</p>
            ) : (
              nodeClients.map((node) => (
                <div key={node.id} className="space-y-2">
                  <div className="flex justify-between text-sm">
                    <span>{node.name}</span>
                    <span>{node.clients} clients</span>
                  </div>
                  <div className="w-full bg-muted rounded-full h-2">
                    <div
                      className="bg-purple-500 h-2 rounded-full"
                      style={{
                        width: `${Math.min((node.clients / totalClientCount) * 100, 100)}%`,
                      }}
                    />
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

interface MetricCardProps {
  label: string;
  value: number;
  color: string;
}

function MetricCard({ label, value, color }: MetricCardProps) {
  return (
    <Card>
      <CardContent className="pt-6">
        <div className="flex items-center gap-3">
          <User className={`w-5 h-5 ${color}`} />
          <div>
            <p className="text-sm text-muted-foreground">{label}</p>
            <p className="mt-1">{value}</p>
          </div>
        </div>
      </CardContent>
    </Card>
  );
}
