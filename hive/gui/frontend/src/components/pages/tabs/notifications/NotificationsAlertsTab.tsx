import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from '../../../ui/card';
import { Button } from '../../../ui/button';
import { Switch } from '../../../ui/switch';
import { Label } from '../../../ui/label';
import { Badge } from '../../../ui/badge';
import { Bell, Mail, MessageSquare, AlertCircle, CheckCircle2, Info } from 'lucide-react';
import { formatRelativeTime } from '../../../useDiskStats';

const ALERT_FOCUS_STORAGE_KEY = 'hive:pending-alert-focus';
const PENDING_ALERT_EVENT = 'hive:pending-alert-focus';
const API_BASE = import.meta?.env?.VITE_NODES_API_BASE_URL ?? 'http://localhost:8000/api/v1';
const DAY_MS = 24 * 60 * 60 * 1000;
const WEEK_MS = 7 * DAY_MS;
const TWELVE_HOURS_MS = 12 * 60 * 60 * 1000;
const ACTIVE_STATUSES = new Set(['new', 'triggered', 'acknowledged']);

type FocusedAlert = {
  alert_id?: number | string | null;
  title?: string | null;
  message?: string | null;
  severity?: string | null;
  triggered_at?: string | null;
};

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

export function NotificationsAlertsTab() {
  const [alerts, setAlerts] = useState<AlertRecord[]>([]);
  const [isLoading, setIsLoading] = useState(true);
  const [fetchError, setFetchError] = useState<string | null>(null);
  const [focusedAlert, setFocusedAlert] = useState<FocusedAlert | null>(() => {
    if (typeof window === 'undefined') return null;
    try {
      const raw = window.sessionStorage?.getItem(ALERT_FOCUS_STORAGE_KEY);
      if (raw) {
        window.sessionStorage?.removeItem(ALERT_FOCUS_STORAGE_KEY);
        return JSON.parse(raw);
      }
    } catch {
      return null;
    }
    return null;
  });
  const focusedRowRef = useRef<HTMLDivElement | null>(null);

  const getTimestampValue = useCallback((record?: AlertRecord | FocusedAlert | null) => {
    if (!record) return null;
    const source = record.triggered_at ?? (record as AlertRecord).updated_at ?? null;
    if (!source) return null;
    const value = new Date(source).getTime();
    return Number.isFinite(value) ? value : null;
  }, []);

  const fetchAlerts = useCallback(async () => {
    setIsLoading(true);
    setFetchError(null);
    try {
      const response = await fetch(`${API_BASE}/alerts?limit=200`);
      if (!response.ok) {
        throw new Error(`Failed to load alerts (${response.status})`);
      }
      const payload = await response.json();
      const rows: AlertRecord[] = Array.isArray(payload)
        ? payload
        : Array.isArray(payload?.results)
          ? payload.results
          : [];
      setAlerts(rows);
    } catch (err) {
      setFetchError(err instanceof Error ? err.message : 'Unable to load alerts');
      setAlerts([]);
    } finally {
      setIsLoading(false);
    }
  }, []);

  useEffect(() => {
    fetchAlerts();
    if (typeof window === 'undefined') {
      return;
    }
    const interval = window.setInterval(fetchAlerts, 60_000);
    return () => window.clearInterval(interval);
  }, [fetchAlerts]);

  useEffect(() => {
    const handler = (event: Event) => {
      const detail = (event as CustomEvent<FocusedAlert>).detail;
      if (!detail) return;
      if (typeof window !== 'undefined') {
        try {
          window.sessionStorage?.setItem(ALERT_FOCUS_STORAGE_KEY, JSON.stringify(detail));
        } catch {
          /* ignore storage failures */
        }
      }
      setFocusedAlert(detail);
    };
    window.addEventListener(PENDING_ALERT_EVENT, handler);
    return () => window.removeEventListener(PENDING_ALERT_EVENT, handler);
  }, []);

  useEffect(() => {
    if (focusedRowRef.current) {
      focusedRowRef.current.scrollIntoView({ behavior: 'smooth', block: 'center' });
    }
  }, [focusedAlert]);

  const focusedNotificationId =
    focusedAlert?.alert_id != null
      ? String(focusedAlert.alert_id)
      : focusedAlert
        ? `focus-${focusedAlert.title ?? 'alert'}`
        : null;

  const todayCount = useMemo(() => {
    const cutoff = Date.now() - DAY_MS;
    return alerts.filter((alert) => {
      const value = getTimestampValue(alert);
      return value !== null && value >= cutoff;
    }).length;
  }, [alerts, getTimestampValue]);

  const weekCount = useMemo(() => {
    const cutoff = Date.now() - WEEK_MS;
    return alerts.filter((alert) => {
      const value = getTimestampValue(alert);
      return value !== null && value >= cutoff;
    }).length;
  }, [alerts, getTimestampValue]);

  const newOrUnackCount = useMemo(
    () =>
      alerts.filter((alert) => {
        const status = (alert.status ?? '').toLowerCase();
        const wStatus = (alert.w_status ?? '').toLowerCase();
        return ACTIVE_STATUSES.has(status) || ACTIVE_STATUSES.has(wStatus);
      }).length,
    [alerts],
  );

  const pagedOutAlerts = useMemo(() => {
    const cutoff = Date.now() - TWELVE_HOURS_MS;
    return alerts
      .filter((alert) => {
        const severity = (alert.severity ?? '').toLowerCase();
        if (severity !== 'critical' && severity !== 'error') return false;
        const value = getTimestampValue(alert);
        return value !== null && value >= cutoff;
      })
      .sort((a, b) => (getTimestampValue(b) ?? 0) - (getTimestampValue(a) ?? 0));
  }, [alerts, getTimestampValue]);

  const normalizedAlerts = useMemo(
    () =>
      alerts
        .slice()
        .sort((a, b) => (getTimestampValue(b) ?? 0) - (getTimestampValue(a) ?? 0)),
    [alerts, getTimestampValue],
  );

  const displayNotifications = useMemo(() => {
    const rows = normalizedAlerts.slice(0, 8).map((alert, index) => {
      const severity = (alert.severity ?? '').toLowerCase();
      const status = (alert.status ?? '').toLowerCase();
      const wStatus = (alert.w_status ?? '').toLowerCase();
      const id =
        alert.alert_id != null ? String(alert.alert_id) : `${alert.title ?? 'alert'}-${index}`;
      const unread = ACTIVE_STATUSES.has(status) || ACTIVE_STATUSES.has(wStatus);
      const icon =
        severity === 'critical'
          ? AlertCircle
          : severity === 'warning'
            ? AlertCircle
            : status === 'resolved'
              ? CheckCircle2
              : Info;
      const color =
        severity === 'critical'
          ? 'text-red-500'
          : severity === 'warning'
            ? 'text-yellow-500'
            : status === 'resolved'
              ? 'text-green-500'
              : 'text-blue-500';

      return {
        id,
        icon,
        color,
        title: alert.title ?? `Alert ${alert.alert_id ?? ''}`,
        message: alert.message ?? 'No message provided',
        time: formatRelativeTime(alert.triggered_at ?? alert.updated_at),
        read: !unread,
      };
    });

    if (focusedAlert && focusedNotificationId) {
      const exists = rows.some((row) => row.id === focusedNotificationId);
      if (!exists) {
        const severity = (focusedAlert.severity ?? '').toLowerCase();
        const icon = severity === 'critical' ? AlertCircle : AlertCircle;
        const color =
          severity === 'critical'
            ? 'text-red-500'
            : severity === 'warning'
              ? 'text-yellow-500'
              : 'text-blue-500';
        rows.unshift({
          id: focusedNotificationId,
          icon,
          color,
          title: focusedAlert.title ?? `Alert ${focusedAlert.alert_id ?? ''}`,
          message: focusedAlert.message ?? 'Selected alert',
          time: focusedAlert.triggered_at
            ? formatRelativeTime(focusedAlert.triggered_at)
            : 'moments ago',
          read: false,
        });
      }
    }

    return rows;
  }, [normalizedAlerts, focusedAlert, focusedNotificationId]);

  return (
    <div className="space-y-6">
      <div className="flex justify-between items-center">
        <div>
          <h2>Alerts</h2>
          <p className="text-muted-foreground">Manage alert preferences and review alerts</p>
        </div>
        <Button variant="outline">Mark All as Read</Button>
      </div>

      <div className="grid grid-cols-1 md:grid-cols-4 gap-4">
        <Card>
          <CardContent className="pt-6">
            <div className="flex items-center justify-between">
              <div>
                <p className="text-sm text-muted-foreground">Today</p>
                <p className="mt-2 text-2xl font-semibold">{isLoading ? '—' : todayCount}</p>
                <p className="text-xs text-muted-foreground">Alerts in the last 24 hours</p>
              </div>
              <MessageSquare className="w-5 h-5 text-green-500" />
            </div>
          </CardContent>
        </Card>
        <Card>
          <CardContent className="pt-6">
            <div className="flex items-center justify-between">
              <div>
                <p className="text-sm text-muted-foreground">This Week</p>
                <p className="mt-2 text-2xl font-semibold">{isLoading ? '—' : weekCount}</p>
                <p className="text-xs text-muted-foreground">Alerts in the last 7 days</p>
              </div>
              <Mail className="w-5 h-5 text-purple-500" />
            </div>
          </CardContent>
        </Card>
        <Card>
          <CardContent className="pt-6">
            <div className="flex items-center justify-between">
              <div>
                <p className="text-sm text-muted-foreground">New or Un-Acknowledged</p>
                <p className="mt-2 text-2xl font-semibold">{isLoading ? '—' : newOrUnackCount}</p>
                <p className="text-xs text-muted-foreground">Status or workflow: new/triggered/ack</p>
              </div>
              <Bell className="w-5 h-5 text-blue-500" />
            </div>
          </CardContent>
        </Card>
        <Card>
          <CardContent className="pt-6">
            <div className="flex items-center justify-between">
              <div>
                <p className="text-sm text-muted-foreground">Alerts Paged Out</p>
                <p className="mt-2 text-2xl font-semibold">
                  {isLoading ? '—' : pagedOutAlerts.length}
                </p>
                <p className="text-xs text-muted-foreground">Critical/error alerts in last 12 hours</p>
              </div>
              <AlertCircle className="w-5 h-5 text-red-500" />
            </div>
            <div className="mt-4 max-h-24 overflow-y-auto space-y-2 pr-1">
              {isLoading ? (
                <p className="text-xs text-muted-foreground">Loading alerts…</p>
              ) : pagedOutAlerts.length === 0 ? (
                <p className="text-xs text-muted-foreground">No alerts paged out.</p>
              ) : (
                pagedOutAlerts.map((alert) => (
                  <p key={alert.alert_id ?? alert.title} className="text-xs text-muted-foreground">
                    {(alert.title ?? `Alert ${alert.alert_id ?? ''}`).slice(0, 50)}
                  </p>
                ))
              )}
            </div>
          </CardContent>
        </Card>
      </div>

      {fetchError && (
        <div className="rounded-md border border-destructive/40 bg-destructive/10 px-4 py-3 text-sm text-destructive">
          {fetchError}
        </div>
      )}

      <Card>
        <CardHeader>
          <CardTitle>Recent Alerts</CardTitle>
          <CardDescription>Live alert feed from the last few hours</CardDescription>
        </CardHeader>
        <CardContent>
          {isLoading ? (
            <p className="text-sm text-muted-foreground">Loading alerts…</p>
          ) : displayNotifications.length === 0 ? (
            <p className="text-sm text-muted-foreground">No alerts available.</p>
          ) : (
            <div className="space-y-4">
              {displayNotifications.map((notification) => {
                const Icon = notification.icon;
                return (
                  <div 
                    key={notification.id}
                    ref={notification.id === focusedNotificationId ? focusedRowRef : undefined}
                    className={`flex gap-4 p-4 rounded-lg border transition ${
                      notification.id === focusedNotificationId
                        ? 'border-primary bg-primary/10'
                        : notification.read
                          ? 'border-border'
                          : 'border-border bg-muted/50'
                    }`}
                  >
                    <Icon className={`w-5 h-5 mt-0.5 ${notification.color}`} />
                    <div className="flex-1">
                      <div className="flex items-start justify-between">
                        <div className="flex-1">
                          <p className={notification.read ? '' : 'font-medium'}>
                            {notification.title}
                          </p>
                          <p className="text-sm text-muted-foreground mt-1">
                            {notification.message}
                          </p>
                          <p className="text-xs text-muted-foreground mt-2">
                            {notification.time}
                          </p>
                        </div>
                        {!notification.read && (
                          <Badge variant="default" className="ml-2">New</Badge>
                        )}
                      </div>
                    </div>
                  </div>
                );
              })}
            </div>
          )}
        </CardContent>
      </Card>

      <Card>
        <CardHeader>
          <CardTitle>Notification Preferences</CardTitle>
          <CardDescription>Configure how you receive notifications</CardDescription>
        </CardHeader>
        <CardContent className="space-y-4">
          <div className="flex items-center justify-between">
            <div className="space-y-0.5">
              <Label>Email Notifications</Label>
              <p className="text-sm text-muted-foreground">Receive alerts via email</p>
            </div>
            <Switch defaultChecked />
          </div>
          <div className="flex items-center justify-between">
            <div className="space-y-0.5">
              <Label>Push Notifications</Label>
              <p className="text-sm text-muted-foreground">Browser push notifications</p>
            </div>
            <Switch defaultChecked />
          </div>
          <div className="flex items-center justify-between">
            <div className="space-y-0.5">
              <Label>SMS Alerts (Critical)</Label>
              <p className="text-sm text-muted-foreground">SMS for critical issues only</p>
            </div>
            <Switch />
          </div>
        </CardContent>
      </Card>

      <Card>
        <CardHeader>
          <CardTitle>Alert Rules</CardTitle>
          <CardDescription>Configure when to receive notifications</CardDescription>
        </CardHeader>
        <CardContent className="space-y-4">
          <div className="flex items-center justify-between">
            <div className="space-y-0.5">
              <Label>Disk Usage Threshold</Label>
              <p className="text-sm text-muted-foreground">Alert when disk usage exceeds 85%</p>
            </div>
            <Switch defaultChecked />
          </div>
          <div className="flex items-center justify-between">
            <div className="space-y-0.5">
              <Label>Failed Login Attempts</Label>
              <p className="text-sm text-muted-foreground">Alert after 3 consecutive failures</p>
            </div>
            <Switch defaultChecked />
          </div>
          <div className="flex items-center justify-between">
            <div className="space-y-0.5">
              <Label>System Updates</Label>
              <p className="text-sm text-muted-foreground">Notify when updates are available</p>
            </div>
            <Switch defaultChecked />
          </div>
          <div className="flex items-center justify-between">
            <div className="space-y-0.5">
              <Label>Backup Completion</Label>
              <p className="text-sm text-muted-foreground">Notify when backups complete</p>
            </div>
            <Switch defaultChecked />
          </div>
          <div className="flex items-center justify-between">
            <div className="space-y-0.5">
              <Label>Performance Degradation</Label>
              <p className="text-sm text-muted-foreground">Alert when performance drops below threshold</p>
            </div>
            <Switch defaultChecked />
          </div>
        </CardContent>
      </Card>
    </div>
  );
}
