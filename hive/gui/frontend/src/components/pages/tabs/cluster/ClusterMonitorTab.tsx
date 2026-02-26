import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from '../../../ui/card';
import { Badge } from '../../../ui/badge';
import { Activity, ShieldAlert, ShieldCheck } from 'lucide-react';
import { Button } from '../../../ui/button';
import { Checkbox } from '../../../ui/checkbox';
import { Table, TableBody, TableCell, TableHead, TableHeader, TableRow } from '../../../ui/table';
import {
  formatRelativeTime,
  formatTimestamp,
  getNodeLabel,
  resolveStatHealth,
  aggregateStatsByNode,
  useDiskStats,
  useHardwareStatus,
} from '../../../useDiskStats';

const API_BASE = import.meta?.env?.VITE_NODES_API_BASE_URL ?? 'http://localhost:8000/api/v1';
const FORTY_EIGHT_HOURS_MS = 48 * 60 * 60 * 1000;
const ALERT_FOCUS_STORAGE_KEY = 'hive:pending-alert-focus';
const PENDING_ALERT_EVENT = 'hive:pending-alert-focus';

type AlertRecord = {
  alert_id?: number | string | null;
  title?: string | null;
  message?: string | null;
  severity?: string | null;
  status?: string | null;
  w_status?: string | null;
  triggered_at?: string | null;
  updated_at?: string | null;
};

export function ClusterMonitorTab() {
  const { stats, isLoading, error, lastUpdated } = useDiskStats();
  const { hardwareStatus, isLoading: isHwLoading, error: hardwareError } = useHardwareStatus();
  const aggregated = useMemo(() => aggregateStatsByNode(stats), [stats]);
  const [activeAlerts, setActiveAlerts] = useState<AlertRecord[]>([]);
  const [recentSevereAlerts, setRecentSevereAlerts] = useState<AlertRecord[]>([]);
  const [alertsError, setAlertsError] = useState<string | null>(null);
  const [alertsLoading, setAlertsLoading] = useState(true);
  const dismissedComponentsRef = useRef<Set<string>>(new Set());
  const [selectedComponents, setSelectedComponents] = useState<Set<string>>(new Set());
  const [dismissedVersion, setDismissedVersion] = useState(0);

  const fetchAlerts = useCallback(async () => {
    const cutoff = Date.now() - FORTY_EIGHT_HOURS_MS;
    const [activeRes, severeRes] = await Promise.all([
      fetch(`${API_BASE}/alerts/recent?active_only=true&limit=25`),
      fetch(`${API_BASE}/alerts/severity?severity=critical,error&limit=100`),
    ]);

    if (!activeRes.ok) {
      throw new Error(`Failed to load active alerts (${activeRes.status})`);
    }
    if (!severeRes.ok) {
      throw new Error(`Failed to load severe alerts (${severeRes.status})`);
    }

    const activePayload = await activeRes.json();
    const severePayload = await severeRes.json();

    const activeList: AlertRecord[] = Array.isArray(activePayload?.results)
      ? activePayload.results
          .filter((entry: any) => entry?.type === 'alert')
          .map((entry: any) => ({
            alert_id: entry.alert_id,
            title: entry.title,
            message: entry.message,
            severity: entry.severity,
            status: entry.status,
            w_status: entry.w_status,
            triggered_at: entry.triggered_at,
            updated_at: entry.updated_at,
          }))
      : [];

    const severeList: AlertRecord[] = Array.isArray(severePayload?.results)
      ? severePayload.results.map((entry: any) => ({
          alert_id: entry.alert_id,
          title: entry.title,
          message: entry.message,
          severity: entry.severity,
          status: entry.status,
          w_status: entry.w_status,
          triggered_at: entry.triggered_at,
          updated_at: entry.updated_at,
        }))
      : [];

    return {
      active: activeList,
      severe: severeList.filter((alert) => {
        const ts = alert.triggered_at ?? alert.updated_at;
        if (!ts) return false;
        const value = new Date(ts).getTime();
        return Number.isFinite(value) && value >= cutoff;
      }),
    };
  }, []);

  useEffect(() => {
    let isMounted = true;
    const load = async () => {
      setAlertsLoading(true);
      setAlertsError(null);
      try {
        const { active, severe } = await fetchAlerts();
        if (!isMounted) return;
        setActiveAlerts(active);
        setRecentSevereAlerts(severe);
      } catch (err) {
        if (!isMounted) return;
        setAlertsError(err instanceof Error ? err.message : 'Unable to load alerts');
      } finally {
        if (isMounted) {
          setAlertsLoading(false);
        }
      }
    };

    load();
    const interval = window.setInterval(load, 60_000);
    return () => {
      isMounted = false;
      window.clearInterval(interval);
    };
  }, [fetchAlerts]);

  const summary = useMemo(() => {
    const statuses = aggregated.map((stat) => resolveStatHealth(stat));
    const errors = statuses.filter((status) => status === 'error').length;
    const warnings = statuses.filter((status) => status === 'warning').length;
    const overall =
      errors > 0 ? 'Attention required' : warnings > 0 ? 'Warnings detected' : 'Healthy';

    return {
      overall,
      alerts: activeAlerts.length,
      reporting: recentSevereAlerts.length,
      lastSample: lastUpdated,
    };
  }, [aggregated, lastUpdated, activeAlerts, recentSevereAlerts]);

  const componentsWithErrors = useMemo(() => {
    const cutoffMs = Date.now() - FORTY_EIGHT_HOURS_MS;
    const dismissed = dismissedComponentsRef.current;
    return hardwareStatus
      .map((record, index) => ({ record, key: `${record.node_id ?? 'na'}-${record.component_type ?? 'unknown'}-${record.component_slot ?? index}` }))
      .filter(({ record, key }) => {
        if (dismissed.has(key)) return false;
        const health = (record.health_state ?? '').toLowerCase();
        const isBad =
          health.includes('crit') ||
          health.includes('fail') ||
          health.includes('error') ||
          health.includes('offline');
        if (!isBad) return false;
        const tsRaw = record.last_error_ts ?? record.last_seen_ts;
        if (!tsRaw) return false;
        const ts = new Date(tsRaw).getTime();
        if (!Number.isFinite(ts)) return false;
        return ts >= cutoffMs;
      });
  }, [hardwareStatus, dismissedVersion]);

  const toggleComponentSelection = (key: string) => {
    setSelectedComponents((prev) => {
      const next = new Set(prev);
      if (next.has(key)) {
        next.delete(key);
      } else {
        next.add(key);
      }
      return next;
    });
  };

  const handleResetComponents = () => {
    if (selectedComponents.size === 0) return;
    const dismissed = dismissedComponentsRef.current;
    selectedComponents.forEach((key) => dismissed.add(key));
    setSelectedComponents(new Set());
    setDismissedVersion((value) => value + 1);
  };

  const errorCriticalCount = recentSevereAlerts.length;
  const recentAlerts = recentSevereAlerts.slice(0, 6);
  const selectAllState =
    componentsWithErrors.length > 0 && selectedComponents.size === componentsWithErrors.length
      ? true
      : selectedComponents.size > 0
        ? 'indeterminate'
        : false;

  const handleNavigateToAlert = (alert: AlertRecord) => {
    if (!alert || !alert.alert_id || typeof window === 'undefined') return;
    const payload = {
      alert_id: alert.alert_id,
      title: alert.title ?? `Alert ${alert.alert_id}`,
      message: alert.message ?? '',
      severity: alert.severity ?? '',
      triggered_at: alert.triggered_at ?? alert.updated_at ?? '',
    };
    try {
      window.sessionStorage?.setItem(ALERT_FOCUS_STORAGE_KEY, JSON.stringify(payload));
    } catch {
      /* ignore storage errors */
    }
    const focusEvent = new CustomEvent(PENDING_ALERT_EVENT, {
      detail: payload,
    });
    window.dispatchEvent(focusEvent);
    const navigateEvent = new CustomEvent('hive:navigate', {
      detail: {
        sidebar: 'notifications',
        tab: 'alerts',
        focusAlertPayload: payload,
      },
    });
    window.dispatchEvent(navigateEvent);
  };

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
            <div className="flex items-center justify-between">
              <p className="text-sm text-muted-foreground">Active Alerts</p>
              <Badge variant="secondary">{alertsLoading ? '…' : summary.alerts}</Badge>
            </div>
            <div className="mt-4 space-y-3 max-h-48 overflow-y-auto pr-1">
              {alertsLoading ? (
                <p className="text-sm text-muted-foreground">Loading alerts…</p>
              ) : activeAlerts.length === 0 ? (
                <p className="text-sm text-muted-foreground">No active alerts 🎉</p>
              ) : (
                activeAlerts.slice(0, 4).map((alert) => (
                  <div key={alert.alert_id ?? alert.title} className="rounded border border-border px-3 py-2">
                    <p className="text-sm font-medium">{alert.title ?? `Alert #${alert.alert_id}`}</p>
                    <p className="text-xs text-muted-foreground">
                      {(alert.severity ?? 'unknown').toUpperCase()} •{' '}
                      {formatRelativeTime(alert.triggered_at ?? alert.updated_at)}
                    </p>
                  </div>
                ))
              )}
            </div>
            {alertsError && (
              <p className="mt-3 text-xs text-destructive">
                {alertsError}
              </p>
            )}
          </CardContent>
        </Card>
        <Card>
          <CardContent className="pt-6">
            <p className="text-sm text-muted-foreground">Number of Error &amp; Critical Alerts (48h)</p>
            <p className="mt-2 text-2xl font-semibold">{alertsLoading ? '—' : errorCriticalCount}</p>
            <p className="text-xs text-muted-foreground">Rolling 48-hour window</p>
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
        <CardHeader className="flex flex-col gap-4 md:flex-row md:items-center md:justify-between">
          <div>
            <CardTitle>Components with Errors</CardTitle>
            <CardDescription>Hardware components failing in the last 48 hours</CardDescription>
          </div>
          <Button
            variant="outline"
            size="sm"
            onClick={handleResetComponents}
            disabled={selectedComponents.size === 0}
          >
            Reset Component
          </Button>
        </CardHeader>
        <CardContent>
          {hardwareError && (
            <p className="mb-3 text-sm text-destructive">{hardwareError}</p>
          )}
          <div className="overflow-x-auto">
            <Table>
              <TableHeader>
                <TableRow>
                  <TableHead className="w-10">
                    <Checkbox
                      aria-label="Select all"
                      checked={selectAllState}
                      onCheckedChange={(value) => {
                        if (value === true) {
                          setSelectedComponents(new Set(componentsWithErrors.map(({ key }) => key)));
                        } else {
                          setSelectedComponents(new Set());
                        }
                      }}
                    />
                  </TableHead>
                  <TableHead>Node</TableHead>
                  <TableHead>Component</TableHead>
                  <TableHead>Reason</TableHead>
                  <TableHead>Last Error</TableHead>
                </TableRow>
              </TableHeader>
              <TableBody>
                {isHwLoading ? (
                  <TableRow>
                    <TableCell colSpan={5} className="text-center text-muted-foreground">
                      Loading components…
                    </TableCell>
                  </TableRow>
                ) : componentsWithErrors.length === 0 ? (
                  <TableRow>
                    <TableCell colSpan={5} className="text-center text-muted-foreground">
                      No component errors detected in the last 48 hours.
                    </TableCell>
                  </TableRow>
                ) : (
                  componentsWithErrors.map(({ record, key }) => (
                    <TableRow key={key} className={selectedComponents.has(key) ? 'bg-muted/40' : undefined}>
                      <TableCell>
                        <Checkbox
                          checked={selectedComponents.has(key)}
                          onCheckedChange={() => toggleComponentSelection(key)}
                          aria-label={`Select component ${key}`}
                        />
                      </TableCell>
                      <TableCell>{record.node_id ?? '—'}</TableCell>
                      <TableCell>
                        <div className="flex flex-col">
                          <span className="font-medium capitalize">
                            {record.component_type ?? 'Unknown'}
                          </span>
                          <span className="text-xs text-muted-foreground">
                            Slot {record.component_slot ?? '—'} • {record.component_model ?? '—'}
                          </span>
                        </div>
                      </TableCell>
                      <TableCell>
                        <p className="text-sm">{record.health_reason ?? 'Error reported'}</p>
                        {record.status_flags && (
                          <p className="text-xs text-muted-foreground">{record.status_flags}</p>
                        )}
                      </TableCell>
                      <TableCell className="text-sm text-muted-foreground">
                        {formatTimestamp(record.last_error_ts ?? record.last_seen_ts)}
                      </TableCell>
                    </TableRow>
                  ))
                )}
              </TableBody>
            </Table>
          </div>
        </CardContent>
      </Card>

      <Card>
        <CardHeader>
          <CardTitle>Recent Alerts</CardTitle>
          <CardDescription>Error &amp; critical alerts from the last 48 hours</CardDescription>
        </CardHeader>
        <CardContent>
          {alertsLoading ? (
            <p className="text-sm text-muted-foreground">Loading recent alerts…</p>
          ) : recentAlerts.length === 0 ? (
            <p className="text-sm text-muted-foreground">No error or critical alerts in the last 48 hours.</p>
          ) : (
            <div className="space-y-3">
              {recentAlerts.map((alert) => (
                <button
                  key={alert.alert_id ?? alert.title}
                  type="button"
                  onClick={() => handleNavigateToAlert(alert)}
                  className="flex w-full items-start gap-3 rounded-lg border border-border px-3 py-2 text-left transition hover:border-primary hover:bg-muted/60"
                >
                  {(alert.severity ?? '').toLowerCase() === 'critical' ? (
                    <ShieldAlert className="mt-0.5 h-5 w-5 text-destructive" />
                  ) : (
                    <ShieldCheck className="mt-0.5 h-5 w-5 text-yellow-500" />
                  )}
                  <div className="flex-1">
                    <p className="font-medium">{alert.title ?? `Alert #${alert.alert_id}`}</p>
                    {alert.message && (
                      <p className="text-sm text-muted-foreground">{alert.message}</p>
                    )}
                    <p className="text-xs text-muted-foreground">
                      {(alert.severity ?? 'unknown').toUpperCase()} •{' '}
                      {formatRelativeTime(alert.triggered_at ?? alert.updated_at)}
                    </p>
                  </div>
                </button>
              ))}
            </div>
          )}
        </CardContent>
      </Card>
    </div>
  );
}
