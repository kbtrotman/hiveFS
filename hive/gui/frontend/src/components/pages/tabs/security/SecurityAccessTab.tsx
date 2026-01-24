import { Card, CardContent, CardDescription, CardHeader, CardTitle } from '../../../ui/card';
import { Button } from '../../../ui/button';
import { Badge } from '../../../ui/badge';
import { Switch } from '../../../ui/switch';
import { Label } from '../../../ui/label';
import { Shield, Lock, Key, AlertTriangle, CheckCircle2 } from 'lucide-react';

const securityEvents = [
  { time: '10 min ago', type: 'success', message: 'User admin logged in from 192.168.1.100' },
  { time: '1 hour ago', type: 'warning', message: 'Failed login attempt from 203.45.12.88' },
  { time: '2 hours ago', type: 'success', message: 'API key rotated successfully' },
  { time: '3 hours ago', type: 'warning', message: '3 failed login attempts from 198.23.45.67' },
  { time: '5 hours ago', type: 'success', message: 'Firewall rules updated' },
];

const permissions = [
  { user: 'admin', role: 'Administrator', read: true, write: true, delete: true, admin: true },
  { user: 'operator1', role: 'Operator', read: true, write: true, delete: false, admin: false },
  { user: 'viewer1', role: 'Viewer', read: true, write: false, delete: false, admin: false },
  { user: 'backup-service', role: 'Service Account', read: true, write: true, delete: false, admin: false },
];

export function SecurityAccessTab() {
  return (
    <div className="space-y-6">
      <div className="flex justify-between items-center">
        <div>
          <h2>Security Management</h2>
          <p className="text-muted-foreground">Configure security settings and monitor access</p>
        </div>
        <Button>Security Audit</Button>
      </div>

      <div className="grid grid-cols-1 md:grid-cols-4 gap-4">
        <Card>
          <CardContent className="pt-6">
            <div className="flex items-center justify-between">
              <div>
                <p className="text-sm text-muted-foreground">Security Score</p>
                <p className="mt-2 text-green-500">98/100</p>
              </div>
              <Shield className="w-5 h-5 text-green-500" />
            </div>
          </CardContent>
        </Card>
        <Card>
          <CardContent className="pt-6">
            <div className="flex items-center justify-between">
              <div>
                <p className="text-sm text-muted-foreground">Active Sessions</p>
                <p className="mt-2">12</p>
              </div>
              <Lock className="w-5 h-5 text-blue-500" />
            </div>
          </CardContent>
        </Card>
        <Card>
          <CardContent className="pt-6">
            <div className="flex items-center justify-between">
              <div>
                <p className="text-sm text-muted-foreground">Failed Logins</p>
                <p className="mt-2 text-yellow-500">4</p>
              </div>
              <AlertTriangle className="w-5 h-5 text-yellow-500" />
            </div>
          </CardContent>
        </Card>
        <Card>
          <CardContent className="pt-6">
            <div className="flex items-center justify-between">
              <div>
                <p className="text-sm text-muted-foreground">API Keys</p>
                <p className="mt-2">8</p>
              </div>
              <Key className="w-5 h-5 text-purple-500" />
            </div>
          </CardContent>
        </Card>
      </div>

      <Card>
        <CardHeader>
          <CardTitle>Security Settings</CardTitle>
          <CardDescription>Configure authentication and access control</CardDescription>
        </CardHeader>
        <CardContent className="space-y-4">
          <div className="flex items-center justify-between">
            <div className="space-y-0.5">
              <Label>Two-Factor Authentication</Label>
              <p className="text-sm text-muted-foreground">Require 2FA for all users</p>
            </div>
            <Switch defaultChecked />
          </div>
          <div className="flex items-center justify-between">
            <div className="space-y-0.5">
              <Label>IP Whitelist</Label>
              <p className="text-sm text-muted-foreground">Restrict access to approved IPs</p>
            </div>
            <Switch defaultChecked />
          </div>
          <div className="flex items-center justify-between">
            <div className="space-y-0.5">
              <Label>Encryption at Rest</Label>
              <p className="text-sm text-muted-foreground">Encrypt all stored data</p>
            </div>
            <Switch defaultChecked />
          </div>
          <div className="flex items-center justify-between">
            <div className="space-y-0.5">
              <Label>Session Timeout</Label>
              <p className="text-sm text-muted-foreground">Auto-logout after 30 minutes of inactivity</p>
            </div>
            <Switch defaultChecked />
          </div>
          <div className="flex items-center justify-between">
            <div className="space-y-0.5">
              <Label>Audit Logging</Label>
              <p className="text-sm text-muted-foreground">Log all security events</p>
            </div>
            <Switch defaultChecked />
          </div>
        </CardContent>
      </Card>

      <div className="grid grid-cols-1 lg:grid-cols-2 gap-6">
        <Card>
          <CardHeader>
            <CardTitle>Recent Security Events</CardTitle>
            <CardDescription>Security activity in the last 24 hours</CardDescription>
          </CardHeader>
          <CardContent>
            <div className="space-y-4">
              {securityEvents.map((event, index) => (
                <div key={index} className="flex gap-3 py-2">
                  {event.type === 'success' ? (
                    <CheckCircle2 className="w-5 h-5 text-green-500 mt-0.5" />
                  ) : (
                    <AlertTriangle className="w-5 h-5 text-yellow-500 mt-0.5" />
                  )}
                  <div className="flex-1">
                    <p className="text-sm">{event.message}</p>
                    <p className="text-xs text-muted-foreground">{event.time}</p>
                  </div>
                </div>
              ))}
            </div>
          </CardContent>
        </Card>

        <Card>
          <CardHeader>
            <CardTitle>User Permissions</CardTitle>
            <CardDescription>Access control for users and services</CardDescription>
          </CardHeader>
          <CardContent>
            <div className="space-y-4">
              {permissions.map((perm, index) => (
                <div key={index} className="flex items-center justify-between py-2 border-b border-border last:border-0">
                  <div>
                    <p className="text-sm">{perm.user}</p>
                    <p className="text-xs text-muted-foreground">{perm.role}</p>
                  </div>
                  <div className="flex gap-1">
                    {perm.read && <Badge variant="outline" className="text-xs">R</Badge>}
                    {perm.write && <Badge variant="outline" className="text-xs">W</Badge>}
                    {perm.delete && <Badge variant="outline" className="text-xs">D</Badge>}
                    {perm.admin && <Badge variant="default" className="text-xs">Admin</Badge>}
                  </div>
                </div>
              ))}
            </div>
          </CardContent>
        </Card>
      </div>
    </div>
  );
}
