import { useState } from 'react';
import { Card, CardContent, CardHeader, CardTitle } from '../../../ui/card';
import { Input } from '../../../ui/input';
import { Label } from '../../../ui/label';
import { Button } from '../../../ui/button';
import { Separator } from '../../../ui/separator';
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '../../../ui/select';
import { Badge } from '../../../ui/badge';
import {
  History,
  Bell,
  AlertTriangle,
  Search,
  Download,
  ChevronDown,
  ChevronRight,
  Check,
  Clock,
  XCircle,
  Mail,
  Users,
  Radio as RadioIcon,
  MessageSquare,
  Smartphone,
  AlertCircle,
  CheckCircle,
  Info,
  Filter,
} from 'lucide-react';

const pastelPalette = ['#FFE4F3', '#FFF6E3', '#E7F5FF', '#E8FFFB', '#F2E8FF', '#FFF0EE', '#E6FFF2', '#F8F5FF'];

const hashString = (value: string) =>
  value.split('').reduce((acc, char) => acc + char.charCodeAt(0), 0);

const getPastelColor = (key: string) => pastelPalette[hashString(key) % pastelPalette.length];

interface NotificationHistoryItem {
  id: string;
  type: 'notification' | 'alert';
  name: string;
  description: string;
  severity?: 'critical' | 'high' | 'medium' | 'low';
  category?: string;
  status: 'new' | 'acknowledged' | 'in-progress' | 'resolved' | 'closed' | 'sent';
  timestamp: string;
  triggeredBy: string;
  endpoints: {
    name: string;
    type: string;
    status: 'sent' | 'failed' | 'pending';
  }[];
  message: string;
  acknowledgedBy?: string;
  acknowledgedAt?: string;
  resolvedBy?: string;
  resolvedAt?: string;
  closedBy?: string;
  closedAt?: string;
}

export function NotificationsHistoryTab() {
  const [filterType, setFilterType] = useState<string>('all');
  const [filterStatus, setFilterStatus] = useState<string>('all');
  const [filterSeverity, setFilterSeverity] = useState<string>('all');
  const [searchQuery, setSearchQuery] = useState('');
  const [expandedItems, setExpandedItems] = useState<string[]>([]);

  // Mock history data
  const [historyItems] = useState<NotificationHistoryItem[]>([
    {
      id: '1',
      type: 'alert',
      name: 'High CPU Usage Alert',
      description: 'CPU usage exceeded 85% threshold',
      severity: 'critical',
      status: 'acknowledged',
      timestamp: '2026-01-25T14:32:15Z',
      triggeredBy: 'Metric Threshold Monitor',
      endpoints: [
        { name: 'Operations Team', type: 'email-group', status: 'sent' },
        { name: 'PagerDuty - Critical', type: 'pagerduty', status: 'sent' },
        { name: 'Slack #alerts', type: 'slack', status: 'sent' },
      ],
      message: 'CPU usage on node-03 has reached 87% and sustained for 5 minutes.',
      acknowledgedBy: 'john.smith@hivefs.com',
      acknowledgedAt: '2026-01-25T14:35:00Z',
    },
    {
      id: '2',
      type: 'notification',
      name: 'Weekly Maintenance Notice',
      description: 'Scheduled maintenance window',
      category: 'maintenance',
      status: 'sent',
      timestamp: '2026-01-25T09:00:00Z',
      triggeredBy: 'Scheduled Task',
      endpoints: [
        { name: 'Operations Team', type: 'email-group', status: 'sent' },
        { name: 'Slack #status-updates', type: 'slack', status: 'sent' },
      ],
      message: 'Maintenance window scheduled for Saturday 2AM-4AM EST.',
    },
    {
      id: '3',
      type: 'alert',
      name: 'Drive Failure Detected',
      description: 'Drive /dev/sdb showing errors',
      severity: 'high',
      status: 'resolved',
      timestamp: '2026-01-25T08:15:30Z',
      triggeredBy: 'Hardware Monitor',
      endpoints: [
        { name: 'Operations Team', type: 'email-group', status: 'sent' },
        { name: 'Primary SNMP Trap', type: 'snmp', status: 'sent' },
        { name: 'SMS - On-call Engineer', type: 'sms', status: 'sent' },
      ],
      message: 'Drive /dev/sdb on node-07 reporting SMART errors. Replacement recommended.',
      acknowledgedBy: 'sarah.johnson@hivefs.com',
      acknowledgedAt: '2026-01-25T08:18:00Z',
      resolvedBy: 'sarah.johnson@hivefs.com',
      resolvedAt: '2026-01-25T10:45:00Z',
    },
    {
      id: '4',
      type: 'alert',
      name: 'Network Connectivity Issue',
      description: 'Node-05 network disconnected',
      severity: 'critical',
      status: 'in-progress',
      timestamp: '2026-01-25T13:20:10Z',
      triggeredBy: 'Network Monitor',
      endpoints: [
        { name: 'Operations Team', type: 'email-group', status: 'sent' },
        { name: 'PagerDuty - Critical', type: 'pagerduty', status: 'sent' },
        { name: 'Slack #alerts', type: 'slack', status: 'sent' },
      ],
      message: 'Node-05 lost network connectivity. Investigating root cause.',
      acknowledgedBy: 'michael.chen@hivefs.com',
      acknowledgedAt: '2026-01-25T13:22:00Z',
    },
    {
      id: '5',
      type: 'notification',
      name: 'Backup Completed Successfully',
      description: 'Daily backup job finished',
      category: 'status',
      status: 'sent',
      timestamp: '2026-01-25T02:30:00Z',
      triggeredBy: 'Backup Job',
      endpoints: [
        { name: 'Management Reports', type: 'email', status: 'sent' },
      ],
      message: 'Daily backup completed successfully. 2.3TB backed up to remote site.',
    },
    {
      id: '6',
      type: 'alert',
      name: 'Memory Usage Warning',
      description: 'Memory usage at 78%',
      severity: 'medium',
      status: 'closed',
      timestamp: '2026-01-24T16:45:00Z',
      triggeredBy: 'Metric Threshold Monitor',
      endpoints: [
        { name: 'Operations Team', type: 'email-group', status: 'sent' },
        { name: 'Slack #alerts', type: 'slack', status: 'sent' },
      ],
      message: 'Memory usage warning on node-02.',
      acknowledgedBy: 'john.smith@hivefs.com',
      acknowledgedAt: '2026-01-24T16:50:00Z',
      resolvedBy: 'john.smith@hivefs.com',
      resolvedAt: '2026-01-24T17:30:00Z',
      closedBy: 'john.smith@hivefs.com',
      closedAt: '2026-01-24T17:35:00Z',
    },
  ]);

  const getSeverityColor = (severity?: string) => {
    switch (severity) {
      case 'critical':
        return 'bg-red-400';
      case 'high':
        return 'bg-orange-300';
      case 'medium':
        return 'bg-yellow-200';
      case 'low':
        return 'bg-emerald-200';
      default:
        return 'bg-slate-200';
    }
  };

  const getStatusBadge = (item: NotificationHistoryItem) => {
    if (item.type === 'notification') {
      return (
        <Badge variant="outline" className="text-xs">
          <CheckCircle className="w-3 h-3 mr-1" />
          Sent
        </Badge>
      );
    }

    // Alert statuses
    switch (item.status) {
      case 'new':
        return (
          <Badge variant="destructive" className="text-xs">
            <AlertCircle className="w-3 h-3 mr-1" />
            New
          </Badge>
        );
      case 'acknowledged':
        return (
          <Badge variant="default" className="text-xs bg-blue-500">
            <Check className="w-3 h-3 mr-1" />
            Acknowledged
          </Badge>
        );
      case 'in-progress':
        return (
          <Badge variant="default" className="text-xs bg-purple-500">
            <Clock className="w-3 h-3 mr-1" />
            In Progress
          </Badge>
        );
      case 'resolved':
        return (
          <Badge variant="default" className="text-xs bg-green-500">
            <CheckCircle className="w-3 h-3 mr-1" />
            Resolved
          </Badge>
        );
      case 'closed':
        return (
          <Badge variant="outline" className="text-xs">
            <XCircle className="w-3 h-3 mr-1" />
            Closed
          </Badge>
        );
      default:
        return null;
    }
  };

  const getEndpointIcon = (type: string) => {
    switch (type) {
      case 'email':
        return <Mail className="w-3 h-3" />;
      case 'email-group':
        return <Users className="w-3 h-3" />;
      case 'snmp':
        return <RadioIcon className="w-3 h-3" />;
      case 'pagerduty':
        return <AlertCircle className="w-3 h-3" />;
      case 'slack':
      case 'teams':
        return <MessageSquare className="w-3 h-3" />;
      case 'sms':
        return <Smartphone className="w-3 h-3" />;
      default:
        return <Bell className="w-3 h-3" />;
    }
  };

  const toggleExpanded = (id: string) => {
    setExpandedItems((prev) =>
      prev.includes(id) ? prev.filter((itemId) => itemId !== id) : [...prev, id]
    );
  };

  const handleAcknowledge = (id: string) => {
    console.log('Acknowledging alert:', id);
    // Would update the alert status to acknowledged
  };

  const handleResolve = (id: string) => {
    console.log('Resolving alert:', id);
    // Would update the alert status to resolved
  };

  const handleClose = (id: string) => {
    console.log('Closing alert:', id);
    // Would update the alert status to closed
  };

  const handleSetInProgress = (id: string) => {
    console.log('Setting alert to in-progress:', id);
    // Would update the alert status to in-progress
  };

  const formatTimestamp = (timestamp: string) => {
    const date = new Date(timestamp);
    const now = new Date();
    const diff = now.getTime() - date.getTime();
    const minutes = Math.floor(diff / 60000);
    const hours = Math.floor(diff / 3600000);
    const days = Math.floor(diff / 86400000);

    if (minutes < 1) return 'Just now';
    if (minutes < 60) return `${minutes}m ago`;
    if (hours < 24) return `${hours}h ago`;
    if (days < 7) return `${days}d ago`;

    return date.toLocaleString();
  };

  // Filter items
  const filteredItems = historyItems.filter((item) => {
    if (filterType !== 'all' && item.type !== filterType) return false;
    if (filterStatus !== 'all' && item.status !== filterStatus) return false;
    if (filterSeverity !== 'all' && item.severity !== filterSeverity) return false;
    if (searchQuery && !item.name.toLowerCase().includes(searchQuery.toLowerCase())) return false;
    return true;
  });

  return (
    <div className="flex h-full w-full flex-col gap-6 overflow-auto bg-gradient-to-br from-background via-primary/5 to-background p-8">
      {/* Page Header */}
      <div>
        <h1 className="text-foreground/80">Notification History</h1>
        <p className="text-muted-foreground text-sm mt-1">
          Audit trail of all notifications and alerts (minimum 7-day retention)
        </p>
      </div>

      {/* Filters Card */}
      <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
        <CardHeader>
          <CardTitle className="text-foreground/90 flex items-center gap-2">
            <Filter className="w-5 h-5" />
            Filters
          </CardTitle>
        </CardHeader>
        <CardContent>
          <div className="grid grid-cols-5 gap-4">
            <div>
              <Label htmlFor="filter-type" className="text-xs text-muted-foreground">
                Type
              </Label>
              <Select value={filterType} onValueChange={setFilterType}>
                <SelectTrigger id="filter-type" className="mt-1 text-sm h-8">
                  <SelectValue />
                </SelectTrigger>
                <SelectContent>
                  <SelectItem value="all">All Types</SelectItem>
                  <SelectItem value="alert">
                    <div className="flex items-center gap-2">
                      <AlertTriangle className="w-4 h-4" />
                      Alerts Only
                    </div>
                  </SelectItem>
                  <SelectItem value="notification">
                    <div className="flex items-center gap-2">
                      <Bell className="w-4 h-4" />
                      Notifications Only
                    </div>
                  </SelectItem>
                </SelectContent>
              </Select>
            </div>

            <div>
              <Label htmlFor="filter-status" className="text-xs text-muted-foreground">
                Status
              </Label>
              <Select value={filterStatus} onValueChange={setFilterStatus}>
                <SelectTrigger id="filter-status" className="mt-1 text-sm h-8">
                  <SelectValue />
                </SelectTrigger>
                <SelectContent>
                  <SelectItem value="all">All Statuses</SelectItem>
                  <SelectItem value="new">New</SelectItem>
                  <SelectItem value="acknowledged">Acknowledged</SelectItem>
                  <SelectItem value="in-progress">In Progress</SelectItem>
                  <SelectItem value="resolved">Resolved</SelectItem>
                  <SelectItem value="closed">Closed</SelectItem>
                  <SelectItem value="sent">Sent (Notifications)</SelectItem>
                </SelectContent>
              </Select>
            </div>

            <div>
              <Label htmlFor="filter-severity" className="text-xs text-muted-foreground">
                Severity
              </Label>
              <Select value={filterSeverity} onValueChange={setFilterSeverity}>
                <SelectTrigger id="filter-severity" className="mt-1 text-sm h-8">
                  <SelectValue />
                </SelectTrigger>
                <SelectContent>
                  <SelectItem value="all">All Severities</SelectItem>
                  <SelectItem value="critical">Critical</SelectItem>
                  <SelectItem value="high">High</SelectItem>
                  <SelectItem value="medium">Medium</SelectItem>
                  <SelectItem value="low">Low</SelectItem>
                </SelectContent>
              </Select>
            </div>

            <div className="col-span-2">
              <Label htmlFor="search" className="text-xs text-muted-foreground">
                Search
              </Label>
              <div className="relative mt-1">
                <Search className="absolute left-2 top-1/2 -translate-y-1/2 w-4 h-4 text-muted-foreground" />
                <Input
                  id="search"
                  placeholder="Search by name..."
                  value={searchQuery}
                  onChange={(e) => setSearchQuery(e.target.value)}
                  className="pl-8 text-sm h-8"
                />
              </div>
            </div>
          </div>

          <div className="flex items-center justify-between mt-4">
            <p className="text-xs text-muted-foreground">
              Showing {filteredItems.length} of {historyItems.length} items
            </p>
            <Button size="sm" variant="outline" className="text-xs h-7">
              <Download className="w-3 h-3 mr-1" />
              Export to CSV
            </Button>
          </div>
        </CardContent>
      </Card>

      {/* History Items */}
      <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
        <CardHeader>
          <CardTitle className="text-foreground/90 flex items-center gap-2">
            <History className="w-5 h-5" />
            History ({filteredItems.length})
          </CardTitle>
        </CardHeader>
        <CardContent>
          <div className="space-y-2">
            {filteredItems.map((item) => {
              const isExpanded = expandedItems.includes(item.id);
              const pastelColor = getPastelColor(item.id);

              return (
                <div
                  key={item.id}
                  className="border rounded-lg transition-all shadow-sm"
                  style={{
                    backgroundColor: pastelColor,
                    borderColor: pastelColor,
                    color: '#0f172a',
                  }}
                >
                  {/* Summary Row */}
                  <div className="p-3 flex items-center justify-between">
                    <div className="flex items-center gap-3 flex-1">
                      <Button
                        size="sm"
                        variant="ghost"
                        className="h-6 w-6 p-0"
                        onClick={() => toggleExpanded(item.id)}
                      >
                        {isExpanded ? (
                          <ChevronDown className="w-4 h-4 text-slate-800" />
                        ) : (
                          <ChevronRight className="w-4 h-4 text-slate-800" />
                        )}
                      </Button>

                      {item.type === 'alert' ? (
                        <AlertTriangle className="w-5 h-5 text-orange-600" />
                      ) : (
                        <Bell className="w-5 h-5 text-sky-600" />
                      )}

                      <div className="flex-1">
                        <div className="flex items-center gap-2">
                          <p className="text-sm font-medium text-foreground/90">{item.name}</p>
                          {item.severity && (
                            <div
                              className={`w-2 h-2 rounded-full ${getSeverityColor(
                                item.severity
                              )}`}
                              title={item.severity}
                            />
                          )}
                        </div>
                        <p className="text-xs text-muted-foreground">{item.description}</p>
                      </div>

                      <div className="text-xs text-muted-foreground">
                        {formatTimestamp(item.timestamp)}
                      </div>

                      {getStatusBadge(item)}
                    </div>
                  </div>

                  {/* Expanded Details */}
                  {isExpanded && (
                    <>
                      <Separator />
                      <div className="p-4 space-y-4 bg-background/50">
                        {/* Message */}
                        <div>
                          <p className="text-xs text-muted-foreground mb-1">Message</p>
                          <p className="text-sm bg-muted/50 rounded p-2 border border-border">
                            {item.message}
                          </p>
                        </div>

                        <div className="grid grid-cols-2 gap-4">
                          {/* Trigger Info */}
                          <div>
                            <p className="text-xs text-muted-foreground mb-1">Triggered By</p>
                            <p className="text-sm">{item.triggeredBy}</p>
                            <p className="text-xs text-muted-foreground mt-1">
                              {new Date(item.timestamp).toLocaleString()}
                            </p>
                          </div>

                          {/* Endpoints */}
                          <div>
                            <p className="text-xs text-muted-foreground mb-1">
                              Endpoints ({item.endpoints.length})
                            </p>
                            <div className="space-y-1">
                              {item.endpoints.map((endpoint, idx) => (
                                <div
                                  key={idx}
                                  className="flex items-center gap-2 text-xs bg-muted/30 rounded px-2 py-1"
                                >
                                  {getEndpointIcon(endpoint.type)}
                                  <span className="flex-1">{endpoint.name}</span>
                                  {endpoint.status === 'sent' ? (
                                    <Check className="w-3 h-3 text-green-500" />
                                  ) : endpoint.status === 'failed' ? (
                                    <XCircle className="w-3 h-3 text-red-500" />
                                  ) : (
                                    <Clock className="w-3 h-3 text-yellow-500" />
                                  )}
                                </div>
                              ))}
                            </div>
                          </div>
                        </div>

                        {/* Alert-specific timeline */}
                        {item.type === 'alert' && (
                          <div>
                            <p className="text-xs text-muted-foreground mb-2">Alert Timeline</p>
                            <div className="space-y-2">
                              <div className="flex items-start gap-2 text-xs">
                                <div className="w-2 h-2 rounded-full bg-red-500 mt-1" />
                                <div className="flex-1">
                                  <p className="text-foreground/90">Alert triggered</p>
                                  <p className="text-muted-foreground">
                                    {new Date(item.timestamp).toLocaleString()}
                                  </p>
                                </div>
                              </div>

                              {item.acknowledgedAt && (
                                <div className="flex items-start gap-2 text-xs">
                                  <div className="w-2 h-2 rounded-full bg-blue-500 mt-1" />
                                  <div className="flex-1">
                                    <p className="text-foreground/90">
                                      Acknowledged by {item.acknowledgedBy}
                                    </p>
                                    <p className="text-muted-foreground">
                                      {new Date(item.acknowledgedAt).toLocaleString()}
                                    </p>
                                  </div>
                                </div>
                              )}

                              {item.resolvedAt && (
                                <div className="flex items-start gap-2 text-xs">
                                  <div className="w-2 h-2 rounded-full bg-green-500 mt-1" />
                                  <div className="flex-1">
                                    <p className="text-foreground/90">
                                      Resolved by {item.resolvedBy}
                                    </p>
                                    <p className="text-muted-foreground">
                                      {new Date(item.resolvedAt).toLocaleString()}
                                    </p>
                                  </div>
                                </div>
                              )}

                              {item.closedAt && (
                                <div className="flex items-start gap-2 text-xs">
                                  <div className="w-2 h-2 rounded-full bg-gray-500 mt-1" />
                                  <div className="flex-1">
                                    <p className="text-foreground/90">
                                      Closed by {item.closedBy}
                                    </p>
                                    <p className="text-muted-foreground">
                                      {new Date(item.closedAt).toLocaleString()}
                                    </p>
                                  </div>
                                </div>
                              )}
                            </div>
                          </div>
                        )}

                        {/* Alert Actions */}
                        {item.type === 'alert' && item.status !== 'closed' && (
                          <>
                            <Separator />
                            <div className="flex gap-2">
                              {item.status === 'new' && (
                                <>
                                  <Button
                                    size="sm"
                                    variant="outline"
                                    className="text-xs h-7"
                                    onClick={() => handleAcknowledge(item.id)}
                                  >
                                    <Check className="w-3 h-3 mr-1" />
                                    Acknowledge
                                  </Button>
                                  <Button
                                    size="sm"
                                    variant="outline"
                                    className="text-xs h-7"
                                    onClick={() => handleSetInProgress(item.id)}
                                  >
                                    <Clock className="w-3 h-3 mr-1" />
                                    Set In Progress
                                  </Button>
                                </>
                              )}

                              {(item.status === 'acknowledged' || item.status === 'in-progress') && (
                                <Button
                                  size="sm"
                                  variant="outline"
                                  className="text-xs h-7"
                                  onClick={() => handleResolve(item.id)}
                                >
                                  <CheckCircle className="w-3 h-3 mr-1" />
                                  Resolve
                                </Button>
                              )}

                              {item.status === 'resolved' && (
                                <Button
                                  size="sm"
                                  variant="outline"
                                  className="text-xs h-7"
                                  onClick={() => handleClose(item.id)}
                                >
                                  <XCircle className="w-3 h-3 mr-1" />
                                  Close
                                </Button>
                              )}
                            </div>
                          </>
                        )}

                        {/* Audit Note */}
                        {item.type === 'notification' && (
                          <p
                            className="text-muted-foreground opacity-60"
                            style={{ fontSize: '0.7rem', lineHeight: '1.3' }}
                          >
                            Notifications are retained for audit purposes and cannot be deleted
                          </p>
                        )}
                      </div>
                    </>
                  )}
                </div>
              );
            })}

            {filteredItems.length === 0 && (
              <div className="text-center py-12 text-muted-foreground">
                <History className="w-12 h-12 mx-auto mb-3 opacity-50" />
                <p className="text-sm">No notifications or alerts found</p>
                <p className="text-xs mt-1">Try adjusting your filters</p>
              </div>
            )}
          </div>
        </CardContent>
      </Card>
    </div>
  );
}
