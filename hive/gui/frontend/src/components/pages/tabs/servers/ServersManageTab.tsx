import { useMemo, useState } from 'react';
import type { CSSProperties } from 'react';
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from '../../../ui/card';
import { Button } from '../../../ui/button';
import { Checkbox } from '../../../ui/checkbox';
import { Table, TableBody, TableCell, TableHead, TableHeader, TableRow } from '../../../ui/table';
import { Plus, Minus } from 'lucide-react';
import {
  formatTimestamp,
  getNodeLabel,
  resolveStatHealth,
  aggregateStatsByNode,
  safeNumber,
  sumStatField,
  useDiskStats,
} from '../../../useDiskStats';

const formatNumber = (value: number, options?: Intl.NumberFormatOptions) =>
  Number.isFinite(value) ? new Intl.NumberFormat(undefined, options).format(value) : '—';

export function ServersManageTab() {
  const { stats, error, lastUpdated } = useDiskStats();
  const aggregated = useMemo(() => aggregateStatsByNode(stats), [stats]);
  const [selectedNodes, setSelectedNodes] = useState<Record<string, boolean>>({});
  const framedButtonClass =
    'border-white/55 bg-white/10 text-white/95 backdrop-blur-sm transition hover:bg-white/25 hover:text-white focus-visible:ring-white/70';
  const raisedButtonStyle: CSSProperties = {
    background: 'linear-gradient(135deg, rgba(255,255,255,0.25) 0%, rgba(15,23,42,0.25) 120%)',
    border: '1px solid rgba(255,255,255,0.7)',
    boxShadow: '0 14px 28px rgba(0,0,0,0.38), inset 0 1px 0 rgba(255,255,255,0.35)',
  };
  const removeButtonStyle: CSSProperties = {
    background: 'linear-gradient(135deg, #7f1d1d 0%, #dc2626 60%, #f87171 100%)',
    border: '1px solid rgba(255,255,255,0.85)',
    boxShadow: '0 14px 28px rgba(0,0,0,0.45), inset 0 1px 0 rgba(255,255,255,0.3)',
  };

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

  const alertingNodes = useMemo(
    () => aggregated.filter((stat) => resolveStatHealth(stat) !== 'healthy'),
    [aggregated],
  );

  const allSelected =
    aggregated.length > 0 &&
    aggregated.every((stat, index) => {
      const key = stat.key ?? String(stat.node_id ?? index);
      return selectedNodes[key];
    });

  const handleSelectAll = (checked: boolean) => {
    if (!checked) {
      setSelectedNodes({});
      return;
    }
    const next: Record<string, boolean> = {};
    aggregated.forEach((stat, index) => {
      const key = stat.key ?? String(stat.node_id ?? index);
      next[key] = true;
    });
    setSelectedNodes(next);
  };

  const handleSelectNode = (key: string, checked: boolean) => {
    setSelectedNodes((prev) => {
      const next = { ...prev };
      if (checked) {
        next[key] = true;
      } else {
        delete next[key];
      }
      return next;
    });
  };

  return (
    <div className="space-y-6">
      <div className="flex flex-col gap-4 md:flex-row md:items-center md:justify-between">
        <div>
          <h2>Storage Node Manager</h2>
        </div>
      </div>

      {error && (
        <div className="rounded-md border border-destructive/40 bg-destructive/10 px-4 py-3 text-sm text-destructive">
          {error}
        </div>
      )}

      <Card>
        <CardHeader className="p-0">
          <div
            className="flex w-full flex-wrap items-center justify-between gap-4 rounded-2xl border border-sky-100/60 p-4 text-white shadow-[0_20px_35px_rgba(15,23,42,0.25),_inset_0_2px_0_rgba(255,255,255,0.45)]"
            style={{
              background: 'linear-gradient(90deg, #14b8a6 0%, #0ea5e9 45%, #0369a1 100%)',
            }}
          >
            <div className="min-w-[240px] flex-1 space-y-1">
              <CardTitle className="text-white">Manage a Node</CardTitle>
              <CardDescription className="text-sky-50/90">
                Select nodes to perform management actions
              </CardDescription>
            </div>
            <div className="flex flex-wrap justify-end gap-2">
              <Button className={framedButtonClass} variant="outline" style={raisedButtonStyle}>
                <Plus className="mr-2 h-4 w-4" />
                Add Node
              </Button>
              <Button
                variant="ghost"
                className={`${framedButtonClass} text-white`}
                style={removeButtonStyle}
              >
                <Minus className="mr-2 h-4 w-4" />
                Remove Node
              </Button>
              <Button className={framedButtonClass} variant="outline" style={raisedButtonStyle}>
                Reset Node
              </Button>
              <Button className={framedButtonClass} variant="outline" style={raisedButtonStyle}>
                Turn off Node
              </Button>
              <Button className={framedButtonClass} variant="outline" style={raisedButtonStyle}>
                Diag Node
              </Button>
            </div>
          </div>
        </CardHeader>
        <CardContent>
          <div className="overflow-x-auto">
            <Table>
              <TableHeader>
                <TableRow>
                  <TableHead className="w-12">
                    <Checkbox
                      checked={allSelected}
                      onCheckedChange={(checked) => handleSelectAll(Boolean(checked))}
                      aria-label="Select all nodes"
                    />
                  </TableHead>
                  <TableHead>Node</TableHead>
                  <TableHead>Status</TableHead>
                  <TableHead>CPU</TableHead>
                  <TableHead>Throughput</TableHead>
                  <TableHead>Last Update</TableHead>
                </TableRow>
              </TableHeader>
              <TableBody>
                {aggregated.length === 0 ? (
                  <TableRow>
                    <TableCell colSpan={6} className="text-center text-muted-foreground">
                      No nodes reported stats yet.
                    </TableCell>
                  </TableRow>
                ) : (
                  aggregated.map((stat, index) => {
                    const key = stat.key ?? String(stat.node_id ?? index);
                    const checked = !!selectedNodes[key];
                    return (
                      <TableRow key={key}>
                        <TableCell>
                          <Checkbox
                            checked={checked}
                            onCheckedChange={(value) => handleSelectNode(key, Boolean(value))}
                            aria-label={`Select ${getNodeLabel(stat)}`}
                          />
                        </TableCell>
                        <TableCell className="font-medium">{getNodeLabel(stat)}</TableCell>
                        <TableCell className="capitalize">{resolveStatHealth(stat)}</TableCell>
                        <TableCell>{safeNumber(stat.cpu).toFixed(1)}%</TableCell>
                        <TableCell>{`${formatNumber(safeNumber(stat.t_throughput))} MB/s`}</TableCell>
                        <TableCell>
                          {stat.s_ts ? formatTimestamp(stat.s_ts) : 'No timestamp'}
                        </TableCell>
                      </TableRow>
                    );
                  })
                )}
              </TableBody>
            </Table>
          </div>
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
