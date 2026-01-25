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
} from 'lucide-react';

interface NotificationEndpoint {
  id: string;
  name: string;
  type: 'email' | 'email-group' | 'snmp' | 'syslog' | 'servicenow' | 'pagerduty' | 'slack' | 'teams' | 'sms' | 'phone' | 'webhook';
  config: any;
  enabled: boolean;
}

interface SystemUser {
  id: string;
  name: string;
  email: string;
}

export function NotificationsEndpointsTab() {
  const [endpointType, setEndpointType] = useState<string>('email');
  const [endpointName, setEndpointName] = useState('');
  const [isAddingEndpoint, setIsAddingEndpoint] = useState(false);

  // Mock system users - would come from localhost:8000/api/v1/accounts
  const [systemUsers] = useState<SystemUser[]>([
    { id: '1', name: 'John Smith', email: 'john.smith@hivefs.com' },
    { id: '2', name: 'Sarah Johnson', email: 'sarah.johnson@hivefs.com' },
    { id: '3', name: 'Michael Chen', email: 'michael.chen@hivefs.com' },
    { id: '4', name: 'Emily Davis', email: 'emily.davis@hivefs.com' },
  ]);

  const [endpoints, setEndpoints] = useState<NotificationEndpoint[]>([
    {
      id: '1',
      name: 'Operations Team',
      type: 'email-group',
      config: { emails: ['ops@hivefs.com', 'john.smith@hivefs.com', 'sarah.johnson@hivefs.com'] },
      enabled: true,
    },
    {
      id: '2',
      name: 'Primary SNMP Trap',
      type: 'snmp',
      config: { host: '192.168.1.100', port: 162, community: 'public' },
      enabled: true,
    },
    {
      id: '3',
      name: 'Slack #alerts Channel',
      type: 'slack',
      config: { webhook: 'https://hooks.slack.com/services/T00000000/B00000000/XXXXXXXXXXXX' },
      enabled: true,
    },
    {
      id: '4',
      name: 'PagerDuty - Critical',
      type: 'pagerduty',
      config: { integrationKey: 'R02XXXXXXXXXXXXXXXXXXXXXX' },
      enabled: true,
    },
  ]);

  const [selectedUsers, setSelectedUsers] = useState<string[]>([]);

  const toggleUserSelection = (userId: string) => {
    setSelectedUsers((prev) =>
      prev.includes(userId) ? prev.filter((id) => id !== userId) : [...prev, userId]
    );
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

  const getEndpointTypeLabel = (type: string) => {
    const labels: Record<string, string> = {
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
    return labels[type] || type;
  };

  const deleteEndpoint = (id: string) => {
    setEndpoints(endpoints.filter((e) => e.id !== id));
  };

  const toggleEndpoint = (id: string) => {
    setEndpoints(
      endpoints.map((e) => (e.id === id ? { ...e, enabled: !e.enabled } : e))
    );
  };

  const addEndpoint = () => {
    if (!endpointName.trim()) return;

    const newEndpoint: NotificationEndpoint = {
      id: Date.now().toString(),
      name: endpointName,
      type: endpointType as any,
      config: {}, // Would be filled from the configuration form
      enabled: true,
    };

    setEndpoints([...endpoints, newEndpoint]);
    setEndpointName('');
    setIsAddingEndpoint(false);
  };

  return (
    <div className="flex h-full w-full flex-col gap-6 overflow-auto bg-gradient-to-br from-background via-primary/5 to-background p-8">
      {/* Page Header */}
      <div>
        <h1 className="text-foreground/80">Notification Endpoints</h1>
        <p className="text-muted-foreground text-sm mt-1">
          Configure destinations for notifications, alerts, and system messages
        </p>
      </div>

      {/* Add Endpoint Card */}
      <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
        <CardHeader>
          <div className="flex items-center justify-between">
            <CardTitle className="text-foreground/90 flex items-center gap-2">
              <Plus className="w-5 h-5" />
              Add Notification Endpoint
            </CardTitle>
            {!isAddingEndpoint && (
              <Button
                size="sm"
                onClick={() => setIsAddingEndpoint(true)}
                className="text-xs h-8"
              >
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
                  placeholder="e.g., Operations Team"
                  value={endpointName}
                  onChange={(e) => setEndpointName(e.target.value)}
                  className="mt-1.5 text-sm"
                />
              </div>
              <div>
                <Label htmlFor="endpoint-type" className="text-sm">
                  Endpoint Type
                </Label>
                <Select value={endpointType} onValueChange={setEndpointType}>
                  <SelectTrigger id="endpoint-type" className="mt-1.5 text-sm">
                    <SelectValue />
                  </SelectTrigger>
                  <SelectContent>
                    <SelectItem value="email">
                      <div className="flex items-center gap-2">
                        <Mail className="w-4 h-4" />
                        Email
                      </div>
                    </SelectItem>
                    <SelectItem value="email-group">
                      <div className="flex items-center gap-2">
                        <Users className="w-4 h-4" />
                        Email Group / Alias
                      </div>
                    </SelectItem>
                    <SelectItem value="snmp">
                      <div className="flex items-center gap-2">
                        <Radio className="w-4 h-4" />
                        SNMP
                      </div>
                    </SelectItem>
                    <SelectItem value="syslog">
                      <div className="flex items-center gap-2">
                        <FileText className="w-4 h-4" />
                        Syslog
                      </div>
                    </SelectItem>
                    <SelectItem value="servicenow">
                      <div className="flex items-center gap-2">
                        <Wrench className="w-4 h-4" />
                        ServiceNow
                      </div>
                    </SelectItem>
                    <SelectItem value="pagerduty">
                      <div className="flex items-center gap-2">
                        <AlertCircle className="w-4 h-4" />
                        PagerDuty
                      </div>
                    </SelectItem>
                    <SelectItem value="slack">
                      <div className="flex items-center gap-2">
                        <MessageSquare className="w-4 h-4" />
                        Slack
                      </div>
                    </SelectItem>
                    <SelectItem value="teams">
                      <div className="flex items-center gap-2">
                        <MessageSquare className="w-4 h-4" />
                        Microsoft Teams
                      </div>
                    </SelectItem>
                    <SelectItem value="sms">
                      <div className="flex items-center gap-2">
                        <Smartphone className="w-4 h-4" />
                        SMS
                      </div>
                    </SelectItem>
                    <SelectItem value="phone">
                      <div className="flex items-center gap-2">
                        <Phone className="w-4 h-4" />
                        Phone Push
                      </div>
                    </SelectItem>
                    <SelectItem value="webhook">
                      <div className="flex items-center gap-2">
                        <Link className="w-4 h-4" />
                        Webhook
                      </div>
                    </SelectItem>
                  </SelectContent>
                </Select>
              </div>
              <div className="flex items-end gap-2">
                <Button
                  variant="outline"
                  className="flex-1"
                  onClick={() => {
                    setIsAddingEndpoint(false);
                    setEndpointName('');
                  }}
                >
                  Cancel
                </Button>
                <Button
                  className="flex-1"
                  onClick={addEndpoint}
                  disabled={!endpointName.trim()}
                >
                  Add
                </Button>
              </div>
            </div>

            <Separator />

            {/* Endpoint Configuration */}
            <div className="bg-muted/30 rounded-lg p-4 border border-border">
              <p className="text-sm font-medium mb-3">
                {getEndpointTypeLabel(endpointType)} Configuration
              </p>

              {endpointType === 'email' && (
                <div>
                  <Label htmlFor="email-address" className="text-xs text-muted-foreground">
                    Email Address
                  </Label>
                  <Input
                    id="email-address"
                    type="email"
                    placeholder="user@example.com"
                    className="mt-1 text-sm h-8"
                  />
                  <p className="text-xs text-muted-foreground mt-2">
                    Or select from system users below
                  </p>
                  <div className="border border-border rounded-lg mt-2 max-h-32 overflow-y-auto">
                    {systemUsers.map((user) => (
                      <div
                        key={user.id}
                        className="flex items-center gap-3 p-2 hover:bg-muted/50 border-b border-border last:border-b-0"
                      >
                        <Checkbox
                          id={`user-${user.id}`}
                          checked={selectedUsers.includes(user.id)}
                          onCheckedChange={() => toggleUserSelection(user.id)}
                        />
                        <label
                          htmlFor={`user-${user.id}`}
                          className="flex-1 cursor-pointer text-xs"
                        >
                          <p className="text-foreground/90">{user.name}</p>
                          <p className="text-muted-foreground">{user.email}</p>
                        </label>
                      </div>
                    ))}
                  </div>
                </div>
              )}

              {endpointType === 'email-group' && (
                <div>
                  <Label className="text-xs text-muted-foreground mb-2 block">
                    Select System Users
                  </Label>
                  <div className="border border-border rounded-lg max-h-40 overflow-y-auto">
                    {systemUsers.map((user) => (
                      <div
                        key={user.id}
                        className="flex items-center gap-3 p-2 hover:bg-muted/50 border-b border-border last:border-b-0"
                      >
                        <Checkbox
                          id={`group-user-${user.id}`}
                          checked={selectedUsers.includes(user.id)}
                          onCheckedChange={() => toggleUserSelection(user.id)}
                        />
                        <label
                          htmlFor={`group-user-${user.id}`}
                          className="flex-1 cursor-pointer text-xs"
                        >
                          <p className="text-foreground/90">{user.name}</p>
                          <p className="text-muted-foreground">{user.email}</p>
                        </label>
                      </div>
                    ))}
                  </div>
                  <Separator className="my-3" />
                  <Label htmlFor="additional-emails" className="text-xs text-muted-foreground">
                    Additional Email Addresses (comma-separated)
                  </Label>
                  <Input
                    id="additional-emails"
                    placeholder="ops@example.com, alerts@example.com"
                    className="mt-1 text-sm h-8"
                  />
                </div>
              )}

              {endpointType === 'snmp' && (
                <div className="grid grid-cols-3 gap-4">
                  <div>
                    <Label htmlFor="snmp-host" className="text-xs text-muted-foreground">
                      Host / IP Address
                    </Label>
                    <Input
                      id="snmp-host"
                      placeholder="192.168.1.100"
                      className="mt-1 text-sm h-8"
                    />
                  </div>
                  <div>
                    <Label htmlFor="snmp-port" className="text-xs text-muted-foreground">
                      Port
                    </Label>
                    <Input
                      id="snmp-port"
                      placeholder="162"
                      type="number"
                      className="mt-1 text-sm h-8"
                    />
                  </div>
                  <div>
                    <Label htmlFor="snmp-community" className="text-xs text-muted-foreground">
                      Community String
                    </Label>
                    <Input
                      id="snmp-community"
                      placeholder="public"
                      className="mt-1 text-sm h-8"
                    />
                  </div>
                  <div>
                    <Label htmlFor="snmp-version" className="text-xs text-muted-foreground">
                      SNMP Version
                    </Label>
                    <Select defaultValue="v2c">
                      <SelectTrigger id="snmp-version" className="mt-1 text-sm h-8">
                        <SelectValue />
                      </SelectTrigger>
                      <SelectContent>
                        <SelectItem value="v1">SNMPv1</SelectItem>
                        <SelectItem value="v2c">SNMPv2c</SelectItem>
                        <SelectItem value="v3">SNMPv3</SelectItem>
                      </SelectContent>
                    </Select>
                  </div>
                </div>
              )}

              {endpointType === 'syslog' && (
                <div className="grid grid-cols-3 gap-4">
                  <div>
                    <Label htmlFor="syslog-host" className="text-xs text-muted-foreground">
                      Syslog Server
                    </Label>
                    <Input
                      id="syslog-host"
                      placeholder="syslog.example.com"
                      className="mt-1 text-sm h-8"
                    />
                  </div>
                  <div>
                    <Label htmlFor="syslog-port" className="text-xs text-muted-foreground">
                      Port
                    </Label>
                    <Input
                      id="syslog-port"
                      placeholder="514"
                      type="number"
                      className="mt-1 text-sm h-8"
                    />
                  </div>
                  <div>
                    <Label htmlFor="syslog-protocol" className="text-xs text-muted-foreground">
                      Protocol
                    </Label>
                    <Select defaultValue="udp">
                      <SelectTrigger id="syslog-protocol" className="mt-1 text-sm h-8">
                        <SelectValue />
                      </SelectTrigger>
                      <SelectContent>
                        <SelectItem value="udp">UDP</SelectItem>
                        <SelectItem value="tcp">TCP</SelectItem>
                        <SelectItem value="tls">TLS</SelectItem>
                      </SelectContent>
                    </Select>
                  </div>
                </div>
              )}

              {endpointType === 'servicenow' && (
                <div className="grid grid-cols-2 gap-4">
                  <div>
                    <Label htmlFor="snow-instance" className="text-xs text-muted-foreground">
                      Instance URL
                    </Label>
                    <Input
                      id="snow-instance"
                      placeholder="https://instance.service-now.com"
                      className="mt-1 text-sm h-8"
                    />
                  </div>
                  <div>
                    <Label htmlFor="snow-username" className="text-xs text-muted-foreground">
                      Username
                    </Label>
                    <Input
                      id="snow-username"
                      placeholder="api_user"
                      className="mt-1 text-sm h-8"
                    />
                  </div>
                  <div>
                    <Label htmlFor="snow-password" className="text-xs text-muted-foreground">
                      Password
                    </Label>
                    <Input
                      id="snow-password"
                      type="password"
                      placeholder="••••••••"
                      className="mt-1 text-sm h-8"
                    />
                  </div>
                  <div>
                    <Label htmlFor="snow-table" className="text-xs text-muted-foreground">
                      Table Name
                    </Label>
                    <Input
                      id="snow-table"
                      placeholder="incident"
                      className="mt-1 text-sm h-8"
                    />
                  </div>
                </div>
              )}

              {endpointType === 'pagerduty' && (
                <div className="grid grid-cols-2 gap-4">
                  <div>
                    <Label htmlFor="pd-key" className="text-xs text-muted-foreground">
                      Integration Key
                    </Label>
                    <Input
                      id="pd-key"
                      placeholder="R02XXXXXXXXXXXXXXXXXXXXXX"
                      className="mt-1 text-sm h-8"
                    />
                  </div>
                  <div>
                    <Label htmlFor="pd-severity" className="text-xs text-muted-foreground">
                      Default Severity
                    </Label>
                    <Select defaultValue="error">
                      <SelectTrigger id="pd-severity" className="mt-1 text-sm h-8">
                        <SelectValue />
                      </SelectTrigger>
                      <SelectContent>
                        <SelectItem value="critical">Critical</SelectItem>
                        <SelectItem value="error">Error</SelectItem>
                        <SelectItem value="warning">Warning</SelectItem>
                        <SelectItem value="info">Info</SelectItem>
                      </SelectContent>
                    </Select>
                  </div>
                </div>
              )}

              {endpointType === 'slack' && (
                <div>
                  <Label htmlFor="slack-webhook" className="text-xs text-muted-foreground">
                    Webhook URL
                  </Label>
                  <Input
                    id="slack-webhook"
                    placeholder="https://hooks.slack.com/services/..."
                    className="mt-1 text-sm h-8"
                  />
                  <p className="text-xs text-muted-foreground mt-2">
                    Create an incoming webhook in your Slack workspace settings
                  </p>
                </div>
              )}

              {endpointType === 'teams' && (
                <div>
                  <Label htmlFor="teams-webhook" className="text-xs text-muted-foreground">
                    Webhook URL
                  </Label>
                  <Input
                    id="teams-webhook"
                    placeholder="https://outlook.office.com/webhook/..."
                    className="mt-1 text-sm h-8"
                  />
                  <p className="text-xs text-muted-foreground mt-2">
                    Configure an incoming webhook connector in your Teams channel
                  </p>
                </div>
              )}

              {endpointType === 'sms' && (
                <div className="grid grid-cols-2 gap-4">
                  <div>
                    <Label htmlFor="sms-provider" className="text-xs text-muted-foreground">
                      SMS Provider
                    </Label>
                    <Select defaultValue="twilio">
                      <SelectTrigger id="sms-provider" className="mt-1 text-sm h-8">
                        <SelectValue />
                      </SelectTrigger>
                      <SelectContent>
                        <SelectItem value="twilio">Twilio</SelectItem>
                        <SelectItem value="aws-sns">AWS SNS</SelectItem>
                        <SelectItem value="nexmo">Nexmo</SelectItem>
                      </SelectContent>
                    </Select>
                  </div>
                  <div>
                    <Label htmlFor="sms-number" className="text-xs text-muted-foreground">
                      Phone Number
                    </Label>
                    <Input
                      id="sms-number"
                      placeholder="+1234567890"
                      className="mt-1 text-sm h-8"
                    />
                  </div>
                  <div>
                    <Label htmlFor="sms-api-key" className="text-xs text-muted-foreground">
                      API Key / Account SID
                    </Label>
                    <Input
                      id="sms-api-key"
                      placeholder="ACXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
                      className="mt-1 text-sm h-8"
                    />
                  </div>
                  <div>
                    <Label htmlFor="sms-api-secret" className="text-xs text-muted-foreground">
                      API Secret / Auth Token
                    </Label>
                    <Input
                      id="sms-api-secret"
                      type="password"
                      placeholder="••••••••"
                      className="mt-1 text-sm h-8"
                    />
                  </div>
                </div>
              )}

              {endpointType === 'phone' && (
                <div className="grid grid-cols-2 gap-4">
                  <div>
                    <Label htmlFor="phone-service" className="text-xs text-muted-foreground">
                      Push Service
                    </Label>
                    <Select defaultValue="apns">
                      <SelectTrigger id="phone-service" className="mt-1 text-sm h-8">
                        <SelectValue />
                      </SelectTrigger>
                      <SelectContent>
                        <SelectItem value="apns">Apple Push (APNS)</SelectItem>
                        <SelectItem value="fcm">Firebase (FCM)</SelectItem>
                        <SelectItem value="both">Both</SelectItem>
                      </SelectContent>
                    </Select>
                  </div>
                  <div>
                    <Label htmlFor="phone-token" className="text-xs text-muted-foreground">
                      Device Token
                    </Label>
                    <Input
                      id="phone-token"
                      placeholder="Device registration token"
                      className="mt-1 text-sm h-8"
                    />
                  </div>
                  <div className="col-span-2">
                    <Label htmlFor="phone-api-key" className="text-xs text-muted-foreground">
                      API Key / Certificate
                    </Label>
                    <Input
                      id="phone-api-key"
                      placeholder="API key or path to certificate"
                      className="mt-1 text-sm h-8"
                    />
                  </div>
                </div>
              )}

              {endpointType === 'webhook' && (
                <div className="space-y-3">
                  <div>
                    <Label htmlFor="webhook-url" className="text-xs text-muted-foreground">
                      Webhook URL
                    </Label>
                    <Input
                      id="webhook-url"
                      placeholder="https://example.com/webhook"
                      className="mt-1 text-sm h-8"
                    />
                  </div>
                  <div className="grid grid-cols-2 gap-4">
                    <div>
                      <Label htmlFor="webhook-method" className="text-xs text-muted-foreground">
                        HTTP Method
                      </Label>
                      <Select defaultValue="post">
                        <SelectTrigger id="webhook-method" className="mt-1 text-sm h-8">
                          <SelectValue />
                        </SelectTrigger>
                        <SelectContent>
                          <SelectItem value="post">POST</SelectItem>
                          <SelectItem value="put">PUT</SelectItem>
                          <SelectItem value="patch">PATCH</SelectItem>
                        </SelectContent>
                      </Select>
                    </div>
                    <div>
                      <Label htmlFor="webhook-auth" className="text-xs text-muted-foreground">
                        Authentication
                      </Label>
                      <Select defaultValue="none">
                        <SelectTrigger id="webhook-auth" className="mt-1 text-sm h-8">
                          <SelectValue />
                        </SelectTrigger>
                        <SelectContent>
                          <SelectItem value="none">None</SelectItem>
                          <SelectItem value="basic">Basic Auth</SelectItem>
                          <SelectItem value="bearer">Bearer Token</SelectItem>
                          <SelectItem value="apikey">API Key</SelectItem>
                        </SelectContent>
                      </Select>
                    </div>
                  </div>
                  <div>
                    <Label htmlFor="webhook-headers" className="text-xs text-muted-foreground">
                      Custom Headers (JSON format, optional)
                    </Label>
                    <Textarea
                      id="webhook-headers"
                      placeholder='{"Content-Type": "application/json", "X-Custom-Header": "value"}'
                      className="mt-1 text-sm min-h-16 font-mono"
                    />
                  </div>
                  <p className="text-xs text-muted-foreground">
                    Supports Slack, Teams, Discord, Zapier, and any webhook-enabled application
                  </p>
                </div>
              )}
            </div>
          </CardContent>
        )}
      </Card>

      {/* Configured Endpoints */}
      <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
        <CardHeader>
          <CardTitle className="text-foreground/90 flex items-center gap-2">
            <Bell className="w-5 h-5" />
            Configured Endpoints ({endpoints.length})
          </CardTitle>
        </CardHeader>
        <CardContent>
          <div className="space-y-2">
            {endpoints.map((endpoint) => (
              <div
                key={endpoint.id}
                className="flex items-center justify-between p-3 border border-border rounded-lg hover:bg-muted/30"
              >
                <div className="flex items-center gap-3 flex-1">
                  <div className="flex items-center justify-center w-8 h-8 rounded bg-muted">
                    {getEndpointIcon(endpoint.type)}
                  </div>
                  <div className="flex-1">
                    <div className="flex items-center gap-2">
                      <p className="text-sm font-medium text-foreground/90">
                        {endpoint.name}
                      </p>
                      {!endpoint.enabled && (
                        <Badge variant="secondary" className="text-xs">
                          Disabled
                        </Badge>
                      )}
                    </div>
                    <p className="text-xs text-muted-foreground">
                      {getEndpointTypeLabel(endpoint.type)}
                      {endpoint.type === 'email-group' &&
                        endpoint.config.emails &&
                        ` • ${endpoint.config.emails.length} recipients`}
                      {endpoint.type === 'snmp' &&
                        endpoint.config.host &&
                        ` • ${endpoint.config.host}:${endpoint.config.port}`}
                    </p>
                  </div>
                </div>
                <div className="flex items-center gap-2">
                  <Button
                    size="sm"
                    variant="ghost"
                    className="h-8 w-8 p-0"
                    onClick={() => toggleEndpoint(endpoint.id)}
                  >
                    {endpoint.enabled ? (
                      <Badge variant="default" className="text-xs px-2">
                        On
                      </Badge>
                    ) : (
                      <Badge variant="outline" className="text-xs px-2">
                        Off
                      </Badge>
                    )}
                  </Button>
                  <Button size="sm" variant="ghost" className="h-8 w-8 p-0">
                    <Edit className="w-4 h-4 text-muted-foreground" />
                  </Button>
                  <Button
                    size="sm"
                    variant="ghost"
                    className="h-8 w-8 p-0"
                    onClick={() => deleteEndpoint(endpoint.id)}
                  >
                    <Trash2 className="w-4 h-4 text-destructive" />
                  </Button>
                </div>
              </div>
            ))}

            {endpoints.length === 0 && (
              <div className="text-center py-12 text-muted-foreground">
                <Bell className="w-12 h-12 mx-auto mb-3 opacity-50" />
                <p className="text-sm">No notification endpoints configured</p>
                <p className="text-xs mt-1">
                  Add an endpoint above to start receiving notifications
                </p>
              </div>
            )}
          </div>

          <p
            className="text-muted-foreground opacity-60 mt-4"
            style={{ fontSize: '0.7rem', lineHeight: '1.3' }}
          >
            Endpoints can be used for alerts, reports, maintenance notices, and system status
            updates. Alerts are a subset of notifications for critical operational events.
          </p>
        </CardContent>
      </Card>
    </div>
  );
}
