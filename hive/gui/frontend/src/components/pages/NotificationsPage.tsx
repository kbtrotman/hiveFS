import { Card, CardContent, CardDescription, CardHeader, CardTitle } from '../ui/card';
import { Button } from '../ui/button';
import { Switch } from '../ui/switch';
import { Label } from '../ui/label';
import { Badge } from '../ui/badge';
import { Bell, Mail, MessageSquare, AlertCircle, CheckCircle2, Info } from 'lucide-react';

const notifications = [
  { type: 'warning', icon: AlertCircle, color: 'text-yellow-500', title: 'High disk usage on storage-01', message: 'Disk usage has reached 89% on storage-01', time: '10 min ago', read: false },
  { type: 'success', icon: CheckCircle2, color: 'text-green-500', title: 'Backup completed successfully', message: 'Weekly backup completed for all volumes', time: '1 hour ago', read: false },
  { type: 'info', icon: Info, color: 'text-blue-500', title: 'System update available', message: 'HiveFS v3.2.1 is now available', time: '2 hours ago', read: true },
  { type: 'error', icon: AlertCircle, color: 'text-red-500', title: 'Connection timeout', message: 'Lost connection to backup-node-01', time: '3 hours ago', read: true },
  { type: 'info', icon: Info, color: 'text-blue-500', title: 'New user registered', message: 'User operator2 has been added to the system', time: '5 hours ago', read: true },
];

export function NotificationsPage() {
  return (
    <div className="space-y-6">
      <div className="flex justify-between items-center">
        <div>
          <h2>Notifications</h2>
          <p className="text-muted-foreground">Manage notification preferences and view alerts</p>
        </div>
        <Button variant="outline">Mark All as Read</Button>
      </div>

      <div className="grid grid-cols-1 md:grid-cols-4 gap-4">
        <Card>
          <CardContent className="pt-6">
            <div className="flex items-center justify-between">
              <div>
                <p className="text-sm text-muted-foreground">Unread</p>
                <p className="mt-2">2</p>
              </div>
              <Bell className="w-5 h-5 text-blue-500" />
            </div>
          </CardContent>
        </Card>
        <Card>
          <CardContent className="pt-6">
            <div className="flex items-center justify-between">
              <div>
                <p className="text-sm text-muted-foreground">Today</p>
                <p className="mt-2">5</p>
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
                <p className="mt-2">28</p>
              </div>
              <Mail className="w-5 h-5 text-purple-500" />
            </div>
          </CardContent>
        </Card>
        <Card>
          <CardContent className="pt-6">
            <p className="text-sm text-muted-foreground">Email Alerts</p>
            <p className="mt-2 text-green-500">Enabled</p>
          </CardContent>
        </Card>
      </div>

      <Card>
        <CardHeader>
          <CardTitle>Recent Notifications</CardTitle>
          <CardDescription>System alerts and messages</CardDescription>
        </CardHeader>
        <CardContent>
          <div className="space-y-4">
            {notifications.map((notification, index) => {
              const Icon = notification.icon;
              return (
                <div 
                  key={index} 
                  className={`flex gap-4 p-4 rounded-lg border ${
                    notification.read ? 'border-border' : 'border-border bg-muted/50'
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
