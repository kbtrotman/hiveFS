import { useEffect, useMemo, useState } from 'react';
import { Card, CardContent, CardHeader, CardTitle } from '../../../ui/card';
import { Input } from '../../../ui/input';
import { Label } from '../../../ui/label';
import { Button } from '../../../ui/button';
import { Separator } from '../../../ui/separator';
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '../../../ui/select';
import { Badge } from '../../../ui/badge';
import { Checkbox } from '../../../ui/checkbox';
import { Textarea } from '../../../ui/textarea';
import {
  Bell,
  Mail,
  Plus,
  Trash2,
  Users,
  Radio,
  FileText,
  Wrench,
  AlertCircle,
  MessageSquare,
  Smartphone,
  Phone,
  Edit,
  Link,
  RefreshCw,
} from 'lucide-react';
import { useApiResource } from '../../../useApiResource';

type EndpointKind =
  | 'email'
  | 'email-group'
  | 'snmp'
  | 'syslog'
  | 'servicenow'
  | 'pagerduty'
  | 'slack'
  | 'teams'
  | 'sms'
  | 'phone'
  | 'webhook';

interface NotificationEndpoint {
  target_id: number;
  target_name?: string | null;
  user_id?: number | null;
  destination_email?: string | null;
  delivery_channel?: EndpointKind | string | null;
  is_primary?: boolean;
  schedule_id?: number | null;
  updated_at?: string | null;
}

interface SystemUser {
  id: number;
  email: string;
  username: string;
  first_name: string;
  last_name: string;
}

interface Schedule {
  schedule_id: number;
  schedule_name: string;
  frequency: string;
  is_active: boolean;
}

const endpointTypeLabels: Record<string, string> = {
  email: 'Email',
  'email-group': 'Email Group',
  snmp: 'SNMP',
  syslog: 'Syslog',
  servicenow: 'ServiceNow',
  pagerduty: 'PagerDuty',
  slack: 'Slack',
  teams: 'Microsoft Teams',
  sms: 'SMS',
  phone: 'Phone Push',
  webhook: 'Webhook',
};

const getEndpointIcon = (type: string) => {
  switch (type) {
    case 'email':
      return <Mail className="w-4 h-4" />;
    case 'email-group':
      return <Users className="w-4 h-4" />;
    case 'snmp':
      return <Radio className="w-4 h-4" />;
    case 'syslog':
      return <FileText className="w-4 h-4" />;
    case 'servicenow':
      return <Wrench className="w-4 h-4" />;
    case 'pagerduty':
      return <AlertCircle className="w-4 h-4" />;
    case 'slack':
    case 'teams':
      return <MessageSquare className="w-4 h-4" />;
    case 'sms':
      return <Smartphone className="w-4 h-4" />;
    case 'phone':
      return <Phone className="w-4 h-4" />;
    case 'webhook':
      return <Link className="w-4 h-4" />;
    default:
      return <Bell className="w-4 h-4" />;
  }
};

export function NotificationsEndpointsTab() {
  const [endpointType, setEndpointType] = useState<EndpointKind>('email');
  const [endpointName, setEndpointName] = useState('');
  const [endpointConfig, setEndpointConfig] = useState('');
  const [isAddingEndpoint, setIsAddingEndpoint] = useState(false);
  const [selectedUsers, setSelectedUsers] = useState<number[]>([]);

  const {
    data: apiEndpoints,
    isLoading: isLoadingEndpoints,
    error: endpointsError,
    refresh: refreshEndpoints,
  } = useApiResource<NotificationEndpoint[]>('notification-endpoints', {
    initialData: [],
    transform: (payload) => (Array.isArray(payload) ? payload : []),
  });
  const {
    data: systemUsers,
    isLoading: isLoadingUsers,
    error: usersError,
  } = useApiResource<SystemUser[]>('accounts', {
    initialData: [],
    transform: (payload) => (Array.isArray(payload) ? payload : []),
  });
  const {
    data: schedules,
    isLoading: isLoadingSchedules,
  } = useApiResource<Schedule[]>('notification-schedules', {
    initialData: [],
    transform: (payload) => (Array.isArray(payload) ? payload : []),
  });

  const [localEndpoints, setLocalEndpoints] = useState<NotificationEndpoint[]>([]);
  useEffect(() => {
    setLocalEndpoints(apiEndpoints);
  }, [apiEndpoints]);

  const userLookup = useMemo(() => {
    const map = new Map<number, SystemUser>();
    systemUsers.forEach((user) => map.set(user.id, user));
    return map;
  }, [systemUsers]);

  const scheduleLookup = useMemo(() => {
    const map = new Map<number, Schedule>();
    schedules.forEach((schedule) => map.set(schedule.schedule_id, schedule));
    return map;
  }, [schedules]);

  const toggleUserSelection = (userId: number) => {
    setSelectedUsers((prev) =>
      prev.includes(userId) ? prev.filter((id) => id !== userId) : [...prev, userId],
    );
  };

  const addEndpoint = () => {
    if (!endpointName.trim()) return;
    const nextEndpoint: NotificationEndpoint = {
      target_id: Date.now(),
      target_name: endpointName.trim(),
      delivery_channel: endpointType,
      destination_email: endpointConfig || undefined,
      is_primary: true,
      user_id: selectedUsers[0] ?? null,
      schedule_id: schedules[0]?.schedule_id ?? null,
      updated_at: new Date().toISOString(),
    };
    setLocalEndpoints((prev) => [nextEndpoint, ...prev]);
    setEndpointName('');
    setEndpointConfig('');
    setSelectedUsers([]);
    setIsAddingEndpoint(false);
  };

  const deleteEndpoint = (id: number) => {
    setLocalEndpoints((prev) => prev.filter((endpoint) => endpoint.target_id !== id));
  };

  const toggleEndpoint = (id: number) => {
    setLocalEndpoints((prev) =>
      prev.map((endpoint) =>
        endpoint.target_id === id ? { ...endpoint, is_primary: !endpoint.is_primary } : endpoint,
      ),
    );
  };

  const combinedError = endpointsError ?? usersError;

  return (
    <div className="flex h-full w-full flex-col gap-6 overflow-auto bg-gradient-to-br from-background via-primary/5 to-background p-8">
      <div className="flex items-center justify-between">
        <div>
          <h1 className="text-foreground/80">Notification Endpoints</h1>
          <p className="text-muted-foreground text-sm mt-1">
            Configure destinations for notifications, alerts, and system messages
          </p>
        </div>
        <Button variant="outline" size="sm" onClick={refreshEndpoints}>
          <RefreshCw className="w-4 h-4 mr-2" />
          Refresh
        </Button>
      </div>

      {combinedError && (
        <div className="rounded-md border border-destructive/40 bg-destructive/10 px-4 py-2 text-sm text-destructive">
          {combinedError}
        </div>
      )}

      <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
        <CardHeader>
          <div className="flex items-center justify-between">
            <CardTitle className="text-foreground/90 flex items-center gap-2">
              <Plus className="w-5 h-5" />
              Add Notification Endpoint
            </CardTitle>
            {!isAddingEndpoint && (
              <Button size="sm" onClick={() => setIsAddingEndpoint(true)} className="text-xs h-8">
                <Plus className="w-3 h-3 mr-1" />
                New Endpoint
              </Button>
            )}
          </div>
        </CardHeader>
        {isAddingEndpoint && (
          <CardContent className="space-y-4">
            <div className="grid grid-cols-3 gap-4">
              <div>
                <Label htmlFor="endpoint-name" className="text-sm">
                  Endpoint Name
                </Label>
                <Input
                  id="endpoint-name"
                  placeholder="Operations Team Email"
                  value={endpointName}
                  onChange={(event) => setEndpointName(event.target.value)}
                />
              </div>
              <div>
                <Label htmlFor="endpoint-type" className="text-sm">
                  Type
                </Label>
                <Select value={endpointType} onValueChange={(value: EndpointKind) => setEndpointType(value)}>
                  <SelectTrigger id="endpoint-type" className="mt-1.5 text-sm">
                    <SelectValue />
                  </SelectTrigger>
                  <SelectContent>
                    {Object.entries(endpointTypeLabels).map(([value, label]) => (
                      <SelectItem key={value} value={value}>
                        {label}
                      </SelectItem>
                    ))}
                  </SelectContent>
                </Select>
              </div>
              <div>
                <Label className="text-sm">Primary Recipient</Label>
                <Select
                  value={selectedUsers[0]?.toString() ?? ''}
                  onValueChange={(value) => setSelectedUsers(value ? [Number(value)] : [])}
                  disabled={isLoadingUsers || systemUsers.length === 0}
                >
                  <SelectTrigger className="mt-1.5 text-sm">
                    <SelectValue placeholder={isLoadingUsers ? 'Loading users…' : 'Select user'} />
                  </SelectTrigger>
                  <SelectContent>
                    {systemUsers.map((user) => (
                      <SelectItem key={user.id} value={user.id.toString()}>
                        {user.first_name || user.last_name
                          ? `${user.first_name} ${user.last_name}`.trim()
                          : user.email}
                      </SelectItem>
                    ))}
                  </SelectContent>
                </Select>
              </div>
            </div>

            <div>
              <Label className="text-sm">Destination / Webhook</Label>
              <Textarea
                placeholder="ops@hivefs.com"
                value={endpointConfig}
                onChange={(event) => setEndpointConfig(event.target.value)}
              />
            </div>

            <div className="flex justify-end gap-2">
              <Button variant="outline" onClick={() => setIsAddingEndpoint(false)}>
                Cancel
              </Button>
              <Button onClick={addEndpoint}>Save Endpoint</Button>
            </div>
          </CardContent>
        )}
      </Card>

      <Card className="border-primary/20 bg-gradient-to-b from-background/90 to-background shadow-lg shadow-primary/10">
        <CardHeader>
          <CardTitle className="text-foreground/90 flex items-center gap-2">
            <Users className="w-5 h-5" />
            System Users
          </CardTitle>
        </CardHeader>
        <CardContent className="space-y-2">
          {isLoadingUsers && <p className="text-sm text-muted-foreground">Loading accounts…</p>}
          <div className="border border-border rounded-lg max-h-48 overflow-y-auto">
            {systemUsers.map((user) => (
              <div
                key={user.id}
                className="flex items-center gap-3 p-2.5 hover:bg-muted/50 border-b border-border last:border-b-0"
              >
                <Checkbox
                  id={`user-${user.id}`}
                  checked={selectedUsers.includes(user.id)}
                  onCheckedChange={() => toggleUserSelection(user.id)}
                />
                <label htmlFor={`user-${user.id}`} className="flex-1 cursor-pointer text-sm">
                  <p className="text-foreground/90">
                    {user.first_name || user.last_name
                      ? `${user.first_name} ${user.last_name}`.trim()
                      : user.username}
                  </p>
                  <p className="text-xs text-muted-foreground">{user.email}</p>
                </label>
              </div>
            ))}
            {!isLoadingUsers && systemUsers.length === 0 && (
              <p className="p-3 text-sm text-muted-foreground">No users available.</p>
            )}
          </div>
        </CardContent>
      </Card>

      <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
        <CardHeader>
          <CardTitle className="text-foreground/90 flex items-center gap-2">
            <Bell className="w-5 h-5" />
            Configured Endpoints
          </CardTitle>
        </CardHeader>
        <CardContent className="space-y-4">
          {isLoadingEndpoints && <p className="text-sm text-muted-foreground">Loading endpoints…</p>}
          {!isLoadingEndpoints && localEndpoints.length === 0 && (
            <p className="text-sm text-muted-foreground">No endpoints have been configured.</p>
          )}
          {localEndpoints.map((endpoint) => {
            const type = endpoint.delivery_channel ?? 'email';
            const targetUser = endpoint.user_id ? userLookup.get(endpoint.user_id) : undefined;
            const schedule = endpoint.schedule_id ? scheduleLookup.get(endpoint.schedule_id) : undefined;
            return (
              <div
                key={endpoint.target_id}
                className="flex flex-col gap-2 rounded-lg border border-border/60 p-4"
              >
                <div className="flex items-center justify-between">
                  <div className="flex items-center gap-3">
                    <div className="rounded-full bg-muted/80 p-2">{getEndpointIcon(type)}</div>
                    <div>
                      <p className="font-medium">
                        {endpoint.target_name || targetUser?.email || 'Unnamed Endpoint'}
                      </p>
                      <p className="text-xs text-muted-foreground">
                        {endpointTypeLabels[type] ?? type} •{' '}
                        {schedule ? schedule.schedule_name : 'unscheduled'}
                      </p>
                    </div>
                  </div>
                  <div className="flex items-center gap-2">
                    <Badge variant={endpoint.is_primary ? 'default' : 'secondary'}>
                      {endpoint.is_primary ? 'Primary' : 'Secondary'}
                    </Badge>
                    <Button variant="ghost" size="icon" onClick={() => toggleEndpoint(endpoint.target_id)}>
                      <Edit className="w-4 h-4" />
                    </Button>
                    <Button variant="ghost" size="icon" onClick={() => deleteEndpoint(endpoint.target_id)}>
                      <Trash2 className="w-4 h-4" />
                    </Button>
                  </div>
                </div>
                <Separator />
                <div className="grid grid-cols-1 md:grid-cols-2 gap-3 text-sm text-muted-foreground">
                  <div>
                    <p className="text-xs uppercase tracking-wide">Destination</p>
                    <p>{endpoint.destination_email || '—'}</p>
                  </div>
                  <div>
                    <p className="text-xs uppercase tracking-wide">Assigned User</p>
                    <p>{targetUser?.email || '—'}</p>
                  </div>
                </div>
              </div>
            );
          })}
        </CardContent>
      </Card>
    </div>
  );
}
