import { useEffect, useState } from 'react';
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from '../../../ui/card';
import { Label } from '../../../ui/label';
import { Input } from '../../../ui/input';
import { Button } from '../../../ui/button';
import { Switch } from '../../../ui/switch';
import { Badge } from '../../../ui/badge';
import { useApiResource } from '../../../useApiResource';

interface SettingsPayload {
  cluster_notification_forwarder?: string | null;
  cluster_allow_gui_management?: boolean;
  cluster_allow_cli_access?: boolean;
  cluster_rollup_initial_minutes?: number | null;
  net_domain_name?: string | null;
}

export function HomeDashSetupTab() {
  const {
    data: settings,
    isLoading,
    error,
  } = useApiResource<SettingsPayload | null>('settings', { initialData: null });

  const [dashboardName, setDashboardName] = useState('My HiveFS Dashboard');
  const [autoRefresh, setAutoRefresh] = useState(true);
  const [compactMode, setCompactMode] = useState(false);
  const [showNotifications, setShowNotifications] = useState(true);

  useEffect(() => {
    if (!settings) return;
    setDashboardName(settings.cluster_notification_forwarder || 'My HiveFS Dashboard');
    setAutoRefresh((settings.cluster_rollup_initial_minutes ?? 30) <= 30);
    setCompactMode(!(settings.cluster_allow_gui_management ?? true));
    setShowNotifications(settings.cluster_allow_cli_access ?? true);
  }, [settings]);

  return (
    <div className="max-w-4xl space-y-6">
      <div>
        <h2>Dashboard Setup</h2>
        <p className="text-muted-foreground">Configure your dashboard preferences and display options</p>
      </div>

      {error && (
        <div className="rounded-md border border-destructive/40 bg-destructive/10 px-4 py-2 text-sm text-destructive">
          {error}
        </div>
      )}

      {settings && (
        <Card>
          <CardHeader>
            <CardTitle>Cluster Context</CardTitle>
            <CardDescription>Live defaults pulled from /api/v1/settings</CardDescription>
          </CardHeader>
          <CardContent className="flex flex-wrap gap-4 text-sm">
            <div className="flex flex-col">
              <span className="text-muted-foreground text-xs uppercase tracking-wide">Forwarder</span>
              <span>{settings.cluster_notification_forwarder || 'not configured'}</span>
            </div>
            <div className="flex flex-col">
              <span className="text-muted-foreground text-xs uppercase tracking-wide">Domain</span>
              <span>{settings.net_domain_name || 'unset'}</span>
            </div>
            <div className="flex items-center gap-2">
              <Badge variant={settings.cluster_allow_gui_management ? 'default' : 'secondary'}>
                GUI Mgmt {settings.cluster_allow_gui_management ? 'enabled' : 'disabled'}
              </Badge>
              <Badge variant={settings.cluster_allow_cli_access ? 'default' : 'secondary'}>
                CLI Access {settings.cluster_allow_cli_access ? 'enabled' : 'disabled'}
              </Badge>
            </div>
          </CardContent>
        </Card>
      )}

      <Card>
        <CardHeader>
          <CardTitle>General Settings</CardTitle>
          <CardDescription>Customize your dashboard appearance and behavior</CardDescription>
        </CardHeader>
        <CardContent className="space-y-4">
          <div className="space-y-2">
            <Label htmlFor="dashboard-name">Dashboard Name</Label>
            <Input
              id="dashboard-name"
              placeholder="My HiveFS Dashboard"
              value={dashboardName}
              onChange={(event) => setDashboardName(event.target.value)}
            />
          </div>
          
          <div className="flex items-center justify-between">
            <div className="space-y-0.5">
              <Label>Auto-refresh</Label>
              <p className="text-sm text-muted-foreground">Automatically update metrics every 30 seconds</p>
            </div>
            <Switch checked={autoRefresh} onCheckedChange={(value) => setAutoRefresh(Boolean(value))} disabled={isLoading} />
          </div>

          <div className="flex items-center justify-between">
            <div className="space-y-0.5">
              <Label>Compact Mode</Label>
              <p className="text-sm text-muted-foreground">Display more information in less space</p>
            </div>
            <Switch checked={compactMode} onCheckedChange={(value) => setCompactMode(Boolean(value))} disabled={isLoading} />
          </div>

          <div className="flex items-center justify-between">
            <div className="space-y-0.5">
              <Label>Show Notifications</Label>
              <p className="text-sm text-muted-foreground">Display system alerts and warnings</p>
            </div>
            <Switch checked={showNotifications} onCheckedChange={(value) => setShowNotifications(Boolean(value))} disabled={isLoading} />
          </div>
        </CardContent>
      </Card>

      <Card>
        <CardHeader>
          <CardTitle>Widget Configuration</CardTitle>
          <CardDescription>Choose which metrics to display on your dashboard</CardDescription>
        </CardHeader>
        <CardContent className="space-y-3">
          <div className="flex items-center justify-between">
            <Label>IOPS Chart</Label>
            <Switch defaultChecked />
          </div>
          <div className="flex items-center justify-between">
            <Label>Read/Write Operations</Label>
            <Switch defaultChecked />
          </div>
          <div className="flex items-center justify-between">
            <Label>Throughput Metrics</Label>
            <Switch defaultChecked />
          </div>
          <div className="flex items-center justify-between">
            <Label>Latency Distribution</Label>
            <Switch defaultChecked />
          </div>
          <div className="flex items-center justify-between">
            <Label>Cache Performance</Label>
            <Switch defaultChecked />
          </div>
        </CardContent>
      </Card>

      <div className="flex justify-end gap-2">
        <Button variant="outline">Reset to Default</Button>
        <Button disabled={isLoading || !settings}>Save Changes</Button>
      </div>
    </div>
  );
}
