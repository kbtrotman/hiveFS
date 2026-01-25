import { useState } from 'react';
import { Card, CardContent, CardHeader, CardTitle } from '../../../ui/card';
import { Input } from '../../../ui/input';
import { Label } from '../../../ui/label';
import { Button } from '../../../ui/button';
import { Separator } from '../../../ui/separator';
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '../../../ui/select';
import { Badge } from '../../../ui/badge';
import { Checkbox } from '../../../ui/checkbox';
import { Textarea } from '../../../ui/textarea';
import { RadioGroup, RadioGroupItem } from '../../../ui/radio-group';
import {
  Bell,
  AlertTriangle,
  Info,
  Send,
  Clock,
  Zap,
  Mail,
  Users,
  Radio as RadioIcon,
  FileText,
  Wrench,
  AlertCircle,
  MessageSquare,
  Smartphone,
  Phone,
  Link,
} from 'lucide-react';
interface NotificationEndpoint {
  id: string;
  name: string;
  type: 'email' | 'email-group' | 'snmp' | 'syslog' | 'servicenow' | 'pagerduty' | 'slack' | 'teams' | 'sms' | 'phone' | 'webhook';
  config: any;
  enabled: boolean;
}

export function NotificationsCreateTab() {
  const [notificationType, setNotificationType] = useState<'notification' | 'alert'>('alert');
  const [name, setName] = useState('');
  const [description, setDescription] = useState('');
  const [severity, setSeverity] = useState('critical');
  const [category, setCategory] = useState('maintenance');
  const [autoAckMinutes, setAutoAckMinutes] = useState('30');
  const [requiresResolution, setRequiresResolution] = useState(true);
  const [triggerType, setTriggerType] = useState('metric');
  const [selectedEndpoints, setSelectedEndpoints] = useState<string[]>(['1', '3', '4']);
  const [subject, setSubject] = useState('');
  const [message, setMessage] = useState('');

  // Mock endpoints - would come from the NotificationEndpointsScreen
  const [allEndpoints] = useState<NotificationEndpoint[]>([
    {
      id: '1',
      name: 'Operations Team',
      type: 'email-group',
      config: { emails: ['ops@hivefs.com', 'john.smith@hivefs.com'] },
      enabled: true,
    },
    {
      id: '2',
      name: 'Management Reports',
      type: 'email',
      config: { email: 'reports@hivefs.com' },
      enabled: true,
    },
    {
      id: '3',
      name: 'Slack #alerts',
      type: 'slack',
      config: { webhook: 'https://hooks.slack.com/...' },
      enabled: true,
    },
    {
      id: '4',
      name: 'PagerDuty - Critical',
      type: 'pagerduty',
      config: { integrationKey: 'R02XXX...' },
      enabled: true,
    },
    {
      id: '5',
      name: 'Primary SNMP Trap',
      type: 'snmp',
      config: { host: '192.168.1.100', port: 162 },
      enabled: true,
    },
    {
      id: '6',
      name: 'SMS - On-call Engineer',
      type: 'sms',
      config: { number: '+1234567890' },
      enabled: true,
    },
    {
      id: '7',
      name: 'Slack #status-updates',
      type: 'slack',
      config: { webhook: 'https://hooks.slack.com/...' },
      enabled: true,
    },
    {
      id: '8',
      name: 'Central Syslog',
      type: 'syslog',
      config: { host: 'syslog.hivefs.com' },
      enabled: true,
    },
  ]);

  const getEndpointIcon = (type: string) => {
    switch (type) {
      case 'email':
        return <Mail className="w-4 h-4" />;
      case 'email-group':
        return <Users className="w-4 h-4" />;
      case 'snmp':
        return <RadioIcon className="w-4 h-4" />;
      case 'syslog':
        return <FileText className="w-4 h-4" />;
      case 'servicenow':
        return <Wrench className="w-4 h-4" />;
      case 'pagerduty':
        return <AlertCircle className="w-4 h-4" />;
      case 'slack':
        return <MessageSquare className="w-4 h-4" />;
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

  // Filter endpoints based on notification type
  const getFilteredEndpoints = () => {
    if (notificationType === 'alert') {
      // Alerts: All endpoints are available
      return allEndpoints.map((endpoint) => {
        const isEmailOnly = endpoint.type === 'email' || endpoint.type === 'email-group';
        return {
          ...endpoint,
          warning: isEmailOnly ? 'Consider adding non-email endpoints for critical alerts' : undefined,
        };
      });
    } else {
      // Notifications: Filter out ops-specific endpoints
      const inappropriateTypes = ['snmp', 'pagerduty', 'sms', 'phone'];
      return allEndpoints
        .filter((endpoint) => !inappropriateTypes.includes(endpoint.type))
        .map((endpoint) => ({
          ...endpoint,
          warning: undefined,
        }));
    }
  };

  const filteredEndpoints = getFilteredEndpoints();

  const toggleEndpoint = (id: string) => {
    setSelectedEndpoints((prev) =>
      prev.includes(id) ? prev.filter((eid) => eid !== id) : [...prev, id]
    );
  };

  const hasOnlyEmailEndpoints = selectedEndpoints.every((id) => {
    const endpoint = allEndpoints.find((e) => e.id === id);
    return endpoint?.type === 'email' || endpoint?.type === 'email-group';
  });

  const handleSave = () => {
    console.log('Saving notification/alert:', {
      type: notificationType,
      name,
      description,
      severity: notificationType === 'alert' ? severity : undefined,
      category: notificationType === 'notification' ? category : undefined,
      endpoints: selectedEndpoints,
      subject,
      message,
    });
  };

  return (
    <div className="flex h-full w-full flex-col gap-6 overflow-auto bg-gradient-to-br from-background via-primary/5 to-background p-8">
      {/* Page Header */}
      <div>
        <h1 className="text-foreground/80">Create Notification / Alert</h1>
        <p className="text-muted-foreground text-sm mt-1">
          Configure automated notifications and operational alerts
        </p>
      </div>

      {/* Type & Basic Info Card */}
      <Card
        className={`border-primary/20 bg-gradient-to-b from-background/90 to-background shadow-lg transition-all ${
          notificationType === 'alert'
            ? 'border-orange-500/50 shadow-orange-500/10'
            : 'border-blue-500/30 shadow-blue-500/5'
        }`}
      >
        <CardHeader>
          <CardTitle className="text-foreground/90 flex items-center gap-2">
            {notificationType === 'alert' ? (
              <>
                <AlertTriangle className="w-5 h-5 text-orange-500" />
                Alert Configuration
              </>
            ) : (
              <>
                <Info className="w-5 h-5 text-blue-500" />
                Notification Configuration
              </>
            )}
          </CardTitle>
        </CardHeader>
        <CardContent className="space-y-4">
          {/* Type Selection */}
          <div>
            <Label className="text-sm mb-2 block">Type</Label>
            <RadioGroup
              value={notificationType}
              onValueChange={(value) => setNotificationType(value as 'notification' | 'alert')}
              className="flex gap-4"
            >
              <div className="flex items-center space-x-2 flex-1">
                <RadioGroupItem value="notification" id="type-notification" />
                <Label
                  htmlFor="type-notification"
                  className="cursor-pointer flex items-center gap-2 text-sm"
                >
                  <Info className="w-4 h-4 text-blue-500" />
                  Notification (Info/Status)
                </Label>
              </div>
              <div className="flex items-center space-x-2 flex-1">
                <RadioGroupItem value="alert" id="type-alert" />
                <Label
                  htmlFor="type-alert"
                  className="cursor-pointer flex items-center gap-2 text-sm"
                >
                  <AlertTriangle className="w-4 h-4 text-orange-500" />
                  Alert (Ops Event)
                </Label>
              </div>
            </RadioGroup>
            <p className="text-xs text-muted-foreground mt-2">
              {notificationType === 'alert'
                ? 'Alerts require acknowledgment and appear in the alert management dashboard'
                : 'Notifications are informational and appear in notification history only'}
            </p>
          </div>

          <Separator />

          {/* Basic Info */}
          <div className="grid grid-cols-2 gap-4">
            <div>
              <Label htmlFor="name" className="text-sm">
                Name
              </Label>
              <Input
                id="name"
                placeholder={
                  notificationType === 'alert'
                    ? 'e.g., High CPU Usage Alert'
                    : 'e.g., Weekly Maintenance Notice'
                }
                value={name}
                onChange={(e) => setName(e.target.value)}
                className="mt-1.5 text-sm"
              />
            </div>
            <div>
              <Label htmlFor="description" className="text-sm">
                Description
              </Label>
              <Input
                id="description"
                placeholder="Brief description"
                value={description}
                onChange={(e) => setDescription(e.target.value)}
                className="mt-1.5 text-sm"
              />
            </div>
          </div>

          {/* Alert-specific fields */}
          {notificationType === 'alert' && (
            <>
              <div className="grid grid-cols-3 gap-4">
                <div>
                  <Label htmlFor="severity" className="text-sm">
                    Severity
                  </Label>
                  <Select value={severity} onValueChange={setSeverity}>
                    <SelectTrigger id="severity" className="mt-1.5 text-sm">
                      <SelectValue />
                    </SelectTrigger>
                    <SelectContent>
                      <SelectItem value="critical">
                        <div className="flex items-center gap-2">
                          <div className="w-2 h-2 rounded-full bg-red-500" />
                          Critical
                        </div>
                      </SelectItem>
                      <SelectItem value="high">
                        <div className="flex items-center gap-2">
                          <div className="w-2 h-2 rounded-full bg-orange-500" />
                          High
                        </div>
                      </SelectItem>
                      <SelectItem value="medium">
                        <div className="flex items-center gap-2">
                          <div className="w-2 h-2 rounded-full bg-yellow-500" />
                          Medium
                        </div>
                      </SelectItem>
                      <SelectItem value="low">
                        <div className="flex items-center gap-2">
                          <div className="w-2 h-2 rounded-full bg-blue-500" />
                          Low
                        </div>
                      </SelectItem>
                    </SelectContent>
                  </Select>
                </div>
                <div>
                  <Label htmlFor="auto-ack" className="text-sm">
                    Auto-acknowledge after
                  </Label>
                  <div className="flex gap-2 mt-1.5">
                    <Input
                      id="auto-ack"
                      type="number"
                      value={autoAckMinutes}
                      onChange={(e) => setAutoAckMinutes(e.target.value)}
                      className="text-sm"
                    />
                    <span className="flex items-center text-sm text-muted-foreground">
                      minutes
                    </span>
                  </div>
                </div>
                <div className="flex items-end">
                  <div className="flex items-center space-x-2">
                    <Checkbox
                      id="requires-resolution"
                      checked={requiresResolution}
                      onCheckedChange={(checked) => setRequiresResolution(checked as boolean)}
                    />
                    <Label
                      htmlFor="requires-resolution"
                      className="text-sm cursor-pointer"
                    >
                      Requires manual resolution
                    </Label>
                  </div>
                </div>
              </div>
            </>
          )}

          {/* Notification-specific fields */}
          {notificationType === 'notification' && (
            <div>
              <Label htmlFor="category" className="text-sm">
                Category
              </Label>
              <Select value={category} onValueChange={setCategory}>
                <SelectTrigger id="category" className="mt-1.5 text-sm">
                  <SelectValue />
                </SelectTrigger>
                <SelectContent>
                  <SelectItem value="maintenance">
                    <div className="flex items-center gap-2">
                      <Wrench className="w-4 h-4" />
                      Maintenance
                    </div>
                  </SelectItem>
                  <SelectItem value="status">
                    <div className="flex items-center gap-2">
                      <Info className="w-4 h-4" />
                      Status Update
                    </div>
                  </SelectItem>
                  <SelectItem value="report">
                    <div className="flex items-center gap-2">
                      <FileText className="w-4 h-4" />
                      Report
                    </div>
                  </SelectItem>
                  <SelectItem value="general">
                    <div className="flex items-center gap-2">
                      <Bell className="w-4 h-4" />
                      General
                    </div>
                  </SelectItem>
                </SelectContent>
              </Select>
            </div>
          )}
        </CardContent>
      </Card>

      {/* Trigger Conditions Card */}
      <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
        <CardHeader>
          <CardTitle className="text-foreground/90 flex items-center gap-2">
            <Zap className="w-5 h-5" />
            Trigger Conditions
          </CardTitle>
        </CardHeader>
        <CardContent className="space-y-4">
          <div>
            <Label htmlFor="trigger-type" className="text-sm">
              Trigger When
            </Label>
            <Select value={triggerType} onValueChange={setTriggerType}>
              <SelectTrigger id="trigger-type" className="mt-1.5 text-sm">
                <SelectValue />
              </SelectTrigger>
              <SelectContent>
                <SelectItem value="metric">Metric Threshold Exceeded</SelectItem>
                <SelectItem value="time">Time-based Schedule</SelectItem>
                <SelectItem value="event">System Event Occurs</SelectItem>
                <SelectItem value="api">API Call / Manual Trigger</SelectItem>
              </SelectContent>
            </Select>
          </div>

          {/* Trigger-specific configuration */}
          {triggerType === 'metric' && (
            <div className="bg-muted/30 rounded-lg p-4 border border-border space-y-3">
              <p className="text-sm font-medium">Metric Threshold Configuration</p>
              <div className="grid grid-cols-3 gap-4">
                <div>
                  <Label htmlFor="metric" className="text-xs text-muted-foreground">
                    Metric
                  </Label>
                  <Select defaultValue="cpu">
                    <SelectTrigger id="metric" className="mt-1 text-sm h-8">
                      <SelectValue />
                    </SelectTrigger>
                    <SelectContent>
                      <SelectItem value="cpu">CPU Usage</SelectItem>
                      <SelectItem value="memory">Memory Usage</SelectItem>
                      <SelectItem value="disk">Disk Usage</SelectItem>
                      <SelectItem value="network">Network Traffic</SelectItem>
                      <SelectItem value="iops">IOPS</SelectItem>
                    </SelectContent>
                  </Select>
                </div>
                <div>
                  <Label htmlFor="condition" className="text-xs text-muted-foreground">
                    Condition
                  </Label>
                  <Select defaultValue="greater">
                    <SelectTrigger id="condition" className="mt-1 text-sm h-8">
                      <SelectValue />
                    </SelectTrigger>
                    <SelectContent>
                      <SelectItem value="greater">Greater than</SelectItem>
                      <SelectItem value="less">Less than</SelectItem>
                      <SelectItem value="equal">Equal to</SelectItem>
                    </SelectContent>
                  </Select>
                </div>
                <div>
                  <Label htmlFor="threshold" className="text-xs text-muted-foreground">
                    Threshold
                  </Label>
                  <div className="flex gap-1 mt-1">
                    <Input
                      id="threshold"
                      type="number"
                      placeholder="80"
                      className="text-sm h-8"
                    />
                    <span className="flex items-center text-xs text-muted-foreground px-2">
                      %
                    </span>
                  </div>
                </div>
              </div>
              <div>
                <Label htmlFor="duration" className="text-xs text-muted-foreground">
                  Duration (trigger if condition persists)
                </Label>
                <div className="flex gap-2 mt-1">
                  <Input
                    id="duration"
                    type="number"
                    placeholder="5"
                    className="text-sm h-8"
                  />
                  <span className="flex items-center text-xs text-muted-foreground">
                    minutes
                  </span>
                </div>
              </div>
            </div>
          )}

          {triggerType === 'time' && (
            <div className="bg-muted/30 rounded-lg p-4 border border-border space-y-3">
              <p className="text-sm font-medium">Schedule Configuration</p>
              <div className="grid grid-cols-2 gap-4">
                <div>
                  <Label htmlFor="frequency" className="text-xs text-muted-foreground">
                    Frequency
                  </Label>
                  <Select defaultValue="daily">
                    <SelectTrigger id="frequency" className="mt-1 text-sm h-8">
                      <SelectValue />
                    </SelectTrigger>
                    <SelectContent>
                      <SelectItem value="hourly">Hourly</SelectItem>
                      <SelectItem value="daily">Daily</SelectItem>
                      <SelectItem value="weekly">Weekly</SelectItem>
                      <SelectItem value="monthly">Monthly</SelectItem>
                    </SelectContent>
                  </Select>
                </div>
                <div>
                  <Label htmlFor="time" className="text-xs text-muted-foreground">
                    Time
                  </Label>
                  <Input
                    id="time"
                    type="time"
                    defaultValue="09:00"
                    className="mt-1 text-sm h-8"
                  />
                </div>
              </div>
            </div>
          )}

          {triggerType === 'event' && (
            <div className="bg-muted/30 rounded-lg p-4 border border-border space-y-3">
              <p className="text-sm font-medium">Event Configuration</p>
              <div>
                <Label htmlFor="event-type" className="text-xs text-muted-foreground">
                  Event Type
                </Label>
                <Select defaultValue="node-failure">
                  <SelectTrigger id="event-type" className="mt-1 text-sm h-8">
                    <SelectValue />
                  </SelectTrigger>
                  <SelectContent>
                    <SelectItem value="node-failure">Node Failure</SelectItem>
                    <SelectItem value="node-recovery">Node Recovery</SelectItem>
                    <SelectItem value="drive-failure">Drive Failure</SelectItem>
                    <SelectItem value="network-disconnect">Network Disconnect</SelectItem>
                    <SelectItem value="backup-complete">Backup Complete</SelectItem>
                    <SelectItem value="replication-sync">Replication Sync</SelectItem>
                  </SelectContent>
                </Select>
              </div>
            </div>
          )}

          {triggerType === 'api' && (
            <div className="bg-muted/30 rounded-lg p-4 border border-border">
              <p className="text-sm font-medium mb-2">Manual / API Trigger</p>
              <p className="text-xs text-muted-foreground">
                This notification/alert will only be sent via API call or manual trigger from
                the dashboard.
              </p>
              <div className="mt-3 bg-background rounded p-2 font-mono text-xs">
                POST /api/v1/notifications/trigger/{'<notification_id>'}
              </div>
            </div>
          )}
        </CardContent>
      </Card>

      {/* Notification Endpoints Card */}
      <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
        <CardHeader>
          <CardTitle className="text-foreground/90 flex items-center gap-2">
            <Send className="w-5 h-5" />
            Notification Endpoints
          </CardTitle>
        </CardHeader>
        <CardContent className="space-y-4">
          <div>
            <p className="text-sm text-muted-foreground mb-3">
              Select where to send this {notificationType}
              {notificationType === 'alert' && hasOnlyEmailEndpoints && selectedEndpoints.length > 0 && (
                <span className="ml-2 text-orange-500 font-medium">
                  ⚠ Consider adding non-email endpoints
                </span>
              )}
            </p>

            {filteredEndpoints.length === 0 && (
              <div className="text-center py-8 text-muted-foreground border border-dashed border-border rounded-lg">
                <Bell className="w-8 h-8 mx-auto mb-2 opacity-50" />
                <p className="text-sm">No appropriate endpoints configured</p>
                <p className="text-xs mt-1">
                  Configure endpoints in the Notification Endpoints page
                </p>
              </div>
            )}

            <div className="space-y-2">
              {filteredEndpoints.map((endpoint) => {
                const isSelected = selectedEndpoints.includes(endpoint.id);
                const hasWarning = endpoint.warning && isSelected;

                return (
                  <div
                    key={endpoint.id}
                    className={`flex items-center justify-between p-3 border rounded-lg transition-all ${
                      isSelected
                        ? 'bg-primary/5 border-primary/50'
                        : 'border-border hover:bg-muted/30'
                    }`}
                  >
                    <div className="flex items-center gap-3 flex-1">
                      <Checkbox
                        id={`endpoint-${endpoint.id}`}
                        checked={isSelected}
                        onCheckedChange={() => toggleEndpoint(endpoint.id)}
                      />
                      <div className="flex items-center justify-center w-8 h-8 rounded bg-muted">
                        {getEndpointIcon(endpoint.type)}
                      </div>
                      <label
                        htmlFor={`endpoint-${endpoint.id}`}
                        className="flex-1 cursor-pointer"
                      >
                        <p className="text-sm font-medium text-foreground/90">
                          {endpoint.name}
                        </p>
                        <p className="text-xs text-muted-foreground">
                          {endpoint.type
                            .split('-')
                            .map((word) => word.charAt(0).toUpperCase() + word.slice(1))
                            .join(' ')}
                        </p>
                      </label>
                    </div>
                    {hasWarning && (
                      <Badge variant="outline" className="text-xs text-orange-500 border-orange-500">
                        Warning
                      </Badge>
                    )}
                  </div>
                );
              })}
            </div>

            {notificationType === 'notification' && (
              <p className="text-xs text-muted-foreground mt-3 opacity-60">
                SNMP, PagerDuty, SMS, and Phone Push endpoints are reserved for critical alerts
              </p>
            )}
          </div>
        </CardContent>
      </Card>

      {/* Message Template Card */}
      <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
        <CardHeader>
          <CardTitle className="text-foreground/90 flex items-center gap-2">
            <FileText className="w-5 h-5" />
            Message Template
          </CardTitle>
        </CardHeader>
        <CardContent className="space-y-4">
          <div>
            <Label htmlFor="subject" className="text-sm">
              Subject / Title
            </Label>
            <Input
              id="subject"
              placeholder={
                notificationType === 'alert'
                  ? '{{severity}} Alert: {{name}}'
                  : '{{name}} - {{cluster_name}}'
              }
              value={subject}
              onChange={(e) => setSubject(e.target.value)}
              className="mt-1.5 text-sm"
            />
          </div>
          <div>
            <Label htmlFor="message" className="text-sm">
              Message Body
            </Label>
            <Textarea
              id="message"
              placeholder={
                notificationType === 'alert'
                  ? 'Alert triggered at {{timestamp}}\n\nMetric: {{metric_name}}\nCurrent value: {{metric_value}}\nThreshold: {{threshold}}\n\nPlease investigate immediately.'
                  : 'This is a notification from {{cluster_name}}\n\nTimestamp: {{timestamp}}\n\nDetails:\n{{details}}'
              }
              value={message}
              onChange={(e) => setMessage(e.target.value)}
              className="mt-1.5 text-sm min-h-32 font-mono"
            />
          </div>

          <div className="bg-muted/30 rounded-lg p-3 border border-border">
            <p className="text-xs font-medium mb-2">Available Variables:</p>
            <div className="grid grid-cols-2 gap-2 text-xs text-muted-foreground font-mono">
              <div>{'{{cluster_name}}'}</div>
              <div>{'{{timestamp}}'}</div>
              <div>{'{{name}}'}</div>
              <div>{'{{description}}'}</div>
              {notificationType === 'alert' && (
                <>
                  <div>{'{{severity}}'}</div>
                  <div>{'{{metric_name}}'}</div>
                  <div>{'{{metric_value}}'}</div>
                  <div>{'{{threshold}}'}</div>
                </>
              )}
            </div>
          </div>
        </CardContent>
      </Card>

      {/* Action Buttons */}
      <div className="flex justify-end gap-3 pb-4">
        <Button variant="outline" className="min-w-24">
          Cancel
        </Button>
        <Button variant="outline" className="min-w-24">
          <Send className="w-4 h-4 mr-2" />
          Test {notificationType === 'alert' ? 'Alert' : 'Notification'}
        </Button>
        <Button onClick={handleSave} className="min-w-24">
          Save
        </Button>
      </div>
    </div>
  );
}
