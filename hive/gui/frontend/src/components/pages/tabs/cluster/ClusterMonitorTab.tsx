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
} from '../../../useDiskStats';

type ComponentState = {
  name: string;
  status: 'healthy' | 'warning' | 'error';
  message?: string | null;
  timestamp?: string | null;
};

export function ClusterMonitorTab() {
  const { stats, isLoading, error, lastUpdated } = useDiskStats();
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

  const componentStatus: ComponentState[] = useMemo(
    () =>
      aggregated.map((stat) => ({
        name: getNodeLabel(stat),
        status: resolveStatHealth(stat),
        message: stat.message,
        timestamp: stat.s_ts,
      })),
    [aggregated],
  );

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

      <Card>
        <CardHeader>
          <CardTitle>Component Status</CardTitle>
          <CardDescription>Status of all system components</CardDescription>
        </CardHeader>
        <CardContent>
          <div className="space-y-4">
            {componentStatus.map((item, index) => {
              const Icon =
                item.status === 'error'
                  ? AlertCircle
                  : item.status === 'warning'
                    ? AlertTriangle
                    : CheckCircle2;
              const badgeVariant =
                item.status === 'error'
                  ? 'destructive'
                  : item.status === 'warning'
                    ? 'secondary'
                    : 'default';
              return (
                <div key={index} className="flex items-center justify-between py-3 border-b border-border last:border-0">
                  <div className="flex items-center gap-3">
                    <Icon
                      className={`w-5 h-5 ${
                        item.status === 'error'
                          ? 'text-red-500'
                          : item.status === 'warning'
                            ? 'text-yellow-500'
                            : 'text-green-500'
                      }`}
                    />
                    <div>
                      <p>{item.name}</p>
                      <p className="text-sm text-muted-foreground">
                        {item.timestamp ? formatTimestamp(item.timestamp) : 'No timestamp'}
                      </p>
                      {item.message && (
                        <p className="text-xs text-muted-foreground">{item.message}</p>
                      )}
                    </div>
                  </div>
                  <Badge variant={badgeVariant as 'default' | 'secondary' | 'destructive'}>
                    {item.status}
                  </Badge>
                </div>
              );
            })}
          </div>
        </CardContent>
      </Card>

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
