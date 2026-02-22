import { useMemo } from 'react';
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from '../../../ui/card';
import { Badge } from '../../../ui/badge';
import { AlertCircle, CheckCircle2, AlertTriangle, Activity } from 'lucide-react';
import {
  formatRelativeTime,
  formatTimestamp,
  getNodeLabel,
  resolveStatHealth,
  aggregateStatsByNode,
  useDiskStats,
  useDiskStatus,
  DiskStatusRecord,
} from '../../../useDiskStats';

type ComponentState = {
  name: string;
  status: 'healthy' | 'warning' | 'error';
  message?: string | null;
  timestamp?: string | null;
};

export function DiskMonitorTab() {
  const { stats, isLoading, error, lastUpdated } = useDiskStats();
  const {
    diskStatus,
    isLoading: diskStatusLoading,
    error: diskStatusError,
  } = useDiskStatus();
  const aggregated = useMemo(() => aggregateStatsByNode(stats), [stats]);

  const summary = useMemo(() => {
    const statuses = aggregated.map((stat) => resolveStatHealth(stat));
    const errors = statuses.filter((status) => status === 'error').length;
    const warnings = statuses.filter((status) => status === 'warning').length;
    const overall =
      errors > 0 ? 'Attention required' : warnings > 0 ? 'Warnings detected' : 'Healthy';

    return {
      overall,
      alerts: errors + warnings,
      reporting: aggregated.length,
      lastSample: lastUpdated,
    };
  }, [aggregated, lastUpdated]);

  const recentEvents = useMemo(() => {
    return stats
      .filter((stat) => stat.message)
      .sort((a, b) => {
        const aTime = a.s_ts ? new Date(a.s_ts).getTime() : 0;
        const bTime = b.s_ts ? new Date(b.s_ts).getTime() : 0;
        return bTime - aTime;
      })
      .slice(0, 5)
      .map((stat) => ({
        severity: resolveStatHealth(stat),
        message: stat.message ?? 'No message provided',
        time: formatRelativeTime(stat.s_ts),
        label: getNodeLabel(stat),
      }));
  }, [stats]);

  return (
    <div className="space-y-6">
      <div>
        <h2>System Status</h2>
        <p className="text-muted-foreground">Real-time monitoring of all system components</p>
      </div>

      <div className="grid grid-cols-1 md:grid-cols-4 gap-4">
        <Card>
          <CardContent className="pt-6">
            <div className="flex items-center justify-between">
              <div>
                <p className="text-sm text-muted-foreground">Overall Status</p>
                <p className="mt-2 text-green-500">
                  {isLoading ? 'Loading…' : summary.overall}
                </p>
              </div>
              <Activity className="w-5 h-5 text-green-500" />
            </div>
          </CardContent>
        </Card>
        <Card>
          <CardContent className="pt-6">
            <p className="text-sm text-muted-foreground">Active Alerts</p>
            <p className="mt-2">{isLoading ? '—' : summary.alerts}</p>
          </CardContent>
        </Card>
        <Card>
          <CardContent className="pt-6">
            <p className="text-sm text-muted-foreground">Nodes Reporting</p>
            <p className="mt-2">{isLoading ? '—' : summary.reporting}</p>
          </CardContent>
        </Card>
        <Card>
          <CardContent className="pt-6">
            <p className="text-sm text-muted-foreground">Last Sample</p>
            <p className="mt-2">{summary.lastSample ? formatTimestamp(summary.lastSample) : '—'}</p>
          </CardContent>
        </Card>
      </div>

      {error && (
        <div className="rounded-md border border-destructive/40 bg-destructive/10 px-4 py-3 text-sm text-destructive">
          {error}
        </div>
      )}

      <PhysicalDiskStatusCard
        records={diskStatus}
        isLoading={diskStatusLoading}
        error={diskStatusError}
      />

      <Card>
        <CardHeader>
          <CardTitle>Recent Events</CardTitle>
          <CardDescription>System events and notifications from the last 24 hours</CardDescription>
        </CardHeader>
        <CardContent>
          <div className="space-y-4">
            {recentEvents.length === 0 ? (
              <p className="text-sm text-muted-foreground">No recent events reported.</p>
            ) : (
              recentEvents.map((event, index) => (
                <div key={index} className="flex gap-3 py-2">
                  {event.severity === 'error' && (
                    <AlertCircle className="w-5 h-5 text-red-500 mt-0.5" />
                  )}
                  {event.severity === 'warning' && (
                    <AlertTriangle className="w-5 h-5 text-yellow-500 mt-0.5" />
                  )}
                  {event.severity === 'healthy' && (
                    <CheckCircle2 className="w-5 h-5 text-blue-500 mt-0.5" />
                  )}
                  <div className="flex-1">
                    <p>{event.message}</p>
                    <p className="text-sm text-muted-foreground">
                      {event.label} • {event.time}
                    </p>
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

type DiskStatusCardProps = {
  records: DiskStatusRecord[];
  isLoading: boolean;
  error: string | null;
};

function PhysicalDiskStatusCard({ records, isLoading, error }: DiskStatusCardProps) {
  const formatCapacity = (bytes?: number | null) => {
    if (!bytes || Number.isNaN(Number(bytes))) return '—';
    const units = ['B', 'KB', 'MB', 'GB', 'TB', 'PB'];
    let value = Number(bytes);
    let unitIndex = 0;
    while (value >= 1024 && unitIndex < units.length - 1) {
      value /= 1024;
      unitIndex += 1;
    }
    return `${value.toFixed(1)} ${units[unitIndex]}`;
  };

  const healthVariant = (health?: string | null) => {
    const normalized = (health ?? '').toLowerCase();
    if (normalized === 'crit' || normalized === 'critical') return 'destructive';
    if (normalized === 'warn' || normalized === 'warning') return 'secondary';
    return 'default';
  };

  const rows = records ?? [];

  return (
    <Card>
      <CardHeader>
        <CardTitle>Physical Disk Status</CardTitle>
      </CardHeader>
      <CardContent className="space-y-3">
        {error && (
          <div className="rounded-md border border-destructive/40 bg-destructive/10 px-4 py-2 text-sm text-destructive">
            {error}
          </div>
        )}
        <div className="overflow-x-auto">
          <table className="w-full text-sm">
            <thead>
              <tr className="text-left text-muted-foreground">
                <th className="px-3 py-2">Node</th>
                <th className="px-3 py-2">Disk</th>
                <th className="px-3 py-2">Model</th>
                <th className="px-3 py-2">Capacity</th>
                <th className="px-3 py-2">Media / Interface</th>
                <th className="px-3 py-2">Temp (°C)</th>
                <th className="px-3 py-2">Health</th>
                <th className="px-3 py-2">Failures</th>
                <th className="px-3 py-2">Last Seen</th>
                <th className="px-3 py-2">Notes</th>
              </tr>
            </thead>
            <tbody>
              {isLoading ? (
                <tr>
                  <td colSpan={10} className="px-3 py-4 text-center text-muted-foreground">
                    Loading disk status…
                  </td>
                </tr>
              ) : rows.length === 0 ? (
                <tr>
                  <td colSpan={10} className="px-3 py-4 text-center text-muted-foreground">
                    No disk status reported.
                  </td>
                </tr>
              ) : (
                rows.map((record, index) => (
                  <tr key={`${record.disk_serial ?? index}`} className="border-t border-border/50">
                    <td className="px-3 py-2">Node {record.node_id ?? '—'}</td>
                    <td className="px-3 py-2">
                      <div className="font-medium">{record.disk_name ?? 'Unknown'}</div>
                      <div className="text-xs text-muted-foreground">
                        Serial: {record.disk_serial ?? '—'}
                      </div>
                    </td>
                    <td className="px-3 py-2">
                      {record.disk_model ?? '—'}
                      <div className="text-xs text-muted-foreground">
                        {record.disk_vendor ?? ''} {record.disk_firmware ?? ''}
                      </div>
                    </td>
                    <td className="px-3 py-2">{formatCapacity(record.disk_capacity_bytes)}</td>
                    <td className="px-3 py-2">
                      {[record.media_type, record.interface_type].filter(Boolean).join(' / ') || '—'}
                    </td>
                    <td className="px-3 py-2">
                      {record.temperature_c !== undefined && record.temperature_c !== null
                        ? `${record.temperature_c.toFixed(1)}`
                        : '—'}
                    </td>
                    <td className="px-3 py-2">
                      <Badge variant={healthVariant(record.smart_health) as 'default' | 'secondary' | 'destructive'}>
                        {record.smart_health ?? 'unknown'}
                      </Badge>
                      {record.paged_out ? (
                        <p className="text-xs text-muted-foreground mt-1">Paged Out</p>
                      ) : null}
                    </td>
                    <td className="px-3 py-2">
                      {record.failure_count ?? 0}
                      <div className="text-xs text-muted-foreground">
                        {record.last_failure_ts ? formatTimestamp(record.last_failure_ts) : '—'}
                      </div>
                    </td>
                    <td className="px-3 py-2">
                      {record.last_seen_ts ? formatTimestamp(record.last_seen_ts) : '—'}
                    </td>
                    <td className="px-3 py-2 text-xs text-muted-foreground">
                      {record.status_reason ?? '—'}
                    </td>
                  </tr>
                ))
              )}
            </tbody>
          </table>
        </div>
      </CardContent>
    </Card>
  );
}
