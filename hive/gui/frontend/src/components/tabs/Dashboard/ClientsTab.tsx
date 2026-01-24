import { useMemo } from 'react';
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from '../../ui/card';
import { Table, TableBody, TableCell, TableHead, TableHeader, TableRow } from '../../ui/table';
import { Badge } from '../../ui/badge';
import { Button } from '../../ui/button';
import { MoreVertical } from 'lucide-react';
import {
  formatTimestamp,
  getNodeLabel,
  resolveStatHealth,
  aggregateStatsByNode,
  safeNumber,
  sumStatField,
  useDiskStats,
} from '../../useDiskStats';

const formatNumber = (value: number) =>
  Number.isFinite(value) ? new Intl.NumberFormat().format(value) : '—';

export function ClientsTab() {
  const { stats, isLoading, error } = useDiskStats();
  const aggregated = useMemo(() => aggregateStatsByNode(stats), [stats]);

  const overview = useMemo(() => {
    const totalClients = sumStatField(aggregated, 'clients');
    const activeNodes = aggregated.filter((stat) => safeNumber(stat.clients) > 0).length;
    const avgConnections = aggregated.length ? totalClients / aggregated.length : 0;
    return { totalClients, activeNodes, avgConnections };
  }, [aggregated]);

  const clientRows = useMemo(
    () =>
      aggregated
        .map((stat, index) => ({
          id: stat.key ?? stat.node_id ?? index,
          name: getNodeLabel(stat),
          clients: safeNumber(stat.clients),
          throughput: safeNumber(stat.t_throughput),
          cpu: safeNumber(stat.cpu),
          status: resolveStatHealth(stat),
          timestamp: stat.s_ts,
        }))
        .sort((a, b) => b.clients - a.clients),
    [aggregated],
  );

  return (
    <div className="space-y-6">
      <div className="flex justify-between items-center">
        <div>
          <h2>Connected Clients</h2>
          <p className="text-muted-foreground">Manage and monitor client connections</p>
        </div>
        <Button>Add Client</Button>
      </div>

      <div className="grid grid-cols-1 md:grid-cols-3 gap-4">
        <MetricCard label="Total Clients" value={formatNumber(overview.totalClients)} />
        <MetricCard label="Nodes With Clients" value={isLoading ? '—' : overview.activeNodes.toString()} />
        <MetricCard
          label="Avg Clients / Node"
          value={
            isLoading
              ? '—'
              : Number.isFinite(overview.avgConnections)
                ? Number(overview.avgConnections.toFixed(1)).toString()
                : '—'
          }
        />
      </div>

      {error && (
        <div className="rounded-md border border-destructive/40 bg-destructive/10 px-4 py-3 text-sm text-destructive">
          {error}
        </div>
      )}

      <Card>
        <CardHeader>
          <CardTitle>Client List</CardTitle>
          <CardDescription>Node client load based on live stats</CardDescription>
        </CardHeader>
        <CardContent>
          <Table>
            <TableHeader>
              <TableRow>
                <TableHead>Node</TableHead>
                <TableHead>Clients</TableHead>
                <TableHead>CPU</TableHead>
                <TableHead>Throughput</TableHead>
                <TableHead>Last Sample</TableHead>
                <TableHead>Status</TableHead>
                <TableHead></TableHead>
              </TableRow>
            </TableHeader>
            <TableBody>
              {isLoading ? (
                <TableRow>
                  <TableCell colSpan={7} className="text-center text-muted-foreground">
                    Loading client stats…
                  </TableCell>
                </TableRow>
              ) : clientRows.length === 0 ? (
                <TableRow>
                  <TableCell colSpan={7} className="text-center text-muted-foreground">
                    No stats available.
                  </TableCell>
                </TableRow>
              ) : (
                clientRows.map((client) => (
                  <TableRow key={client.id}>
                    <TableCell>{client.name}</TableCell>
                    <TableCell>{formatNumber(client.clients)}</TableCell>
                    <TableCell className="text-muted-foreground">
                      {Number.isFinite(client.cpu) ? `${client.cpu}%` : '—'}
                    </TableCell>
                    <TableCell className="text-muted-foreground">
                      {Number.isFinite(client.throughput) ? `${client.throughput} MB/s` : '—'}
                    </TableCell>
                    <TableCell className="text-muted-foreground">
                      {client.timestamp ? formatTimestamp(client.timestamp) : '—'}
                    </TableCell>
                    <TableCell>
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
                    </TableCell>
                    <TableCell>
                      <Button variant="ghost" size="icon">
                        <MoreVertical className="w-4 h-4" />
                      </Button>
                    </TableCell>
                  </TableRow>
                ))
              )}
            </TableBody>
          </Table>
        </CardContent>
      </Card>
    </div>
  );
}

interface MetricCardProps {
  label: string;
  value: string;
}

function MetricCard({ label, value }: MetricCardProps) {
  return (
    <Card>
      <CardContent className="pt-6">
        <p className="text-sm text-muted-foreground">{label}</p>
        <p className="mt-2">{value}</p>
      </CardContent>
    </Card>
  );
}
