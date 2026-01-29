import { useState } from 'react';
import { Card, CardContent, CardHeader, CardTitle } from '../../../ui/card';
import { Input } from '../../../ui/input';
import { Label } from '../../../ui/label';
import { Button } from '../../../ui/button';
import { Separator } from '../../../ui/separator';
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '../../../ui/select';
import { Badge } from '../../../ui/badge';
import {
  Shield,
  Search,
  Download,
  ChevronDown,
  ChevronRight,
  LogIn,
  LogOut,
  Settings,
  Eye,
  Lock,
  Unlock,
  UserPlus,
  UserMinus,
  Key,
  AlertTriangle,
  Info,
  CheckCircle,
  XCircle,
  FileText,
  Calendar,
  Filter,
  User,
} from 'lucide-react';

interface SecurityEvent {
  id: string;
  timestamp: string;
  eventType: 'authentication' | 'configuration' | 'access' | 'user-management' | 'data' | 'system';
  action: string;
  severity: 'critical' | 'high' | 'medium' | 'low' | 'info';
  user: string;
  ipAddress: string;
  resource?: string;
  details: string;
  oldValue?: string;
  newValue?: string;
  success: boolean;
  userAgent?: string;
  sessionId?: string;
}

export function SecurityAuditTab() {
  const [filterEventType, setFilterEventType] = useState<string>('all');
  const [filterSeverity, setFilterSeverity] = useState<string>('all');
  const [filterUser, setFilterUser] = useState<string>('all');
  const [filterSuccess, setFilterSuccess] = useState<string>('all');
  const [searchQuery, setSearchQuery] = useState('');
  const [expandedItems, setExpandedItems] = useState<string[]>([]);
  const [dateRange, setDateRange] = useState<string>('7d');

  // Mock security events data
  const [securityEvents] = useState<SecurityEvent[]>([
    {
      id: '1',
      timestamp: '2026-01-25T14:32:15Z',
      eventType: 'authentication',
      action: 'Failed Login Attempt',
      severity: 'high',
      user: 'unknown',
      ipAddress: '192.168.1.45',
      details: 'Failed login attempt for user "admin" - invalid password',
      success: false,
      userAgent: 'Mozilla/5.0 (Windows NT 10.0; Win64; x64)',
      sessionId: 'N/A',
    },
    {
      id: '2',
      timestamp: '2026-01-25T14:30:00Z',
      eventType: 'authentication',
      action: 'Successful Login',
      severity: 'info',
      user: 'john.smith@hivefs.com',
      ipAddress: '192.168.1.100',
      details: 'User logged in successfully',
      success: true,
      userAgent: 'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7)',
      sessionId: 'sess_abc123def456',
    },
    {
      id: '3',
      timestamp: '2026-01-25T13:45:00Z',
      eventType: 'configuration',
      action: 'Cluster Settings Modified',
      severity: 'medium',
      user: 'sarah.johnson@hivefs.com',
      ipAddress: '192.168.1.102',
      resource: 'cluster.replication_factor',
      details: 'Modified cluster replication factor setting',
      oldValue: '3',
      newValue: '5',
      success: true,
      sessionId: 'sess_xyz789ghi012',
    },
    {
      id: '4',
      timestamp: '2026-01-25T13:20:10Z',
      eventType: 'user-management',
      action: 'User Created',
      severity: 'medium',
      user: 'sarah.johnson@hivefs.com',
      ipAddress: '192.168.1.102',
      resource: 'emily.davis@hivefs.com',
      details: 'New user account created with role: Operator',
      success: true,
      sessionId: 'sess_xyz789ghi012',
    },
    {
      id: '5',
      timestamp: '2026-01-25T12:15:30Z',
      eventType: 'access',
      action: 'File Access',
      severity: 'low',
      user: 'michael.chen@hivefs.com',
      ipAddress: '192.168.1.105',
      resource: '/secure/financial-reports/Q4-2025.xlsx',
      details: 'User accessed sensitive file',
      success: true,
      sessionId: 'sess_mno345pqr678',
    },
    {
      id: '6',
      timestamp: '2026-01-25T11:30:00Z',
      eventType: 'configuration',
      action: 'Security Policy Updated',
      severity: 'high',
      user: 'john.smith@hivefs.com',
      ipAddress: '192.168.1.100',
      resource: 'security.password_policy',
      details: 'Updated password complexity requirements',
      oldValue: 'min_length: 8, require_special: false',
      newValue: 'min_length: 12, require_special: true',
      success: true,
      sessionId: 'sess_abc123def456',
    },
    {
      id: '7',
      timestamp: '2026-01-25T10:45:00Z',
      eventType: 'authentication',
      action: 'Password Changed',
      severity: 'medium',
      user: 'michael.chen@hivefs.com',
      ipAddress: '192.168.1.105',
      details: 'User changed their password',
      success: true,
      sessionId: 'sess_mno345pqr678',
    },
    {
      id: '8',
      timestamp: '2026-01-25T10:00:00Z',
      eventType: 'authentication',
      action: 'Logout',
      severity: 'info',
      user: 'sarah.johnson@hivefs.com',
      ipAddress: '192.168.1.102',
      details: 'User logged out',
      success: true,
      sessionId: 'sess_xyz789ghi012',
    },
    {
      id: '9',
      timestamp: '2026-01-25T09:30:00Z',
      eventType: 'system',
      action: 'API Key Generated',
      severity: 'high',
      user: 'john.smith@hivefs.com',
      ipAddress: '192.168.1.100',
      resource: 'api_key_prod_001',
      details: 'New API key generated for production access',
      success: true,
      sessionId: 'sess_abc123def456',
    },
    {
      id: '10',
      timestamp: '2026-01-25T09:00:00Z',
      eventType: 'user-management',
      action: 'Role Changed',
      severity: 'high',
      user: 'john.smith@hivefs.com',
      ipAddress: '192.168.1.100',
      resource: 'michael.chen@hivefs.com',
      details: 'User role modified',
      oldValue: 'Operator',
      newValue: 'Administrator',
      success: true,
      sessionId: 'sess_abc123def456',
    },
    {
      id: '11',
      timestamp: '2026-01-25T08:30:00Z',
      eventType: 'access',
      action: 'Page View: Cluster Management',
      severity: 'info',
      user: 'sarah.johnson@hivefs.com',
      ipAddress: '192.168.1.102',
      resource: '/cluster/management',
      details: 'User viewed Cluster Management page',
      success: true,
      sessionId: 'sess_xyz789ghi012',
    },
    {
      id: '12',
      timestamp: '2026-01-25T08:00:00Z',
      eventType: 'authentication',
      action: 'Failed Login Attempt',
      severity: 'critical',
      user: 'unknown',
      ipAddress: '203.0.113.42',
      details: 'Multiple failed login attempts detected - possible brute force attack',
      success: false,
      userAgent: 'python-requests/2.28.0',
      sessionId: 'N/A',
    },
  ]);

  const getSeverityColor = (severity: string) => {
    switch (severity) {
      case 'critical':
        return 'bg-red-500';
      case 'high':
        return 'bg-orange-500';
      case 'medium':
        return 'bg-yellow-500';
      case 'low':
        return 'bg-blue-500';
      case 'info':
        return 'bg-gray-500';
      default:
        return 'bg-gray-500';
    }
  };

  const getSeverityBadge = (severity: string) => {
    const colors = {
      critical: 'bg-red-500',
      high: 'bg-orange-500',
      medium: 'bg-yellow-500',
      low: 'bg-blue-500',
      info: 'bg-gray-500',
    };

    return (
      <Badge variant="outline" className="text-xs">
        <div className={`w-2 h-2 rounded-full ${colors[severity as keyof typeof colors]} mr-1.5`} />
        {severity.charAt(0).toUpperCase() + severity.slice(1)}
      </Badge>
    );
  };

  const getEventIcon = (eventType: string, action: string) => {
    if (action.includes('Login') && action.includes('Failed')) {
      return <XCircle className="w-5 h-5 text-red-500" />;
    }
    if (action.includes('Login')) {
      return <LogIn className="w-5 h-5 text-green-500" />;
    }
    if (action.includes('Logout')) {
      return <LogOut className="w-5 h-5 text-gray-500" />;
    }

    switch (eventType) {
      case 'authentication':
        return <Lock className="w-5 h-5 text-blue-500" />;
      case 'configuration':
        return <Settings className="w-5 h-5 text-purple-500" />;
      case 'access':
        return <Eye className="w-5 h-5 text-cyan-500" />;
      case 'user-management':
        return <User className="w-5 h-5 text-orange-500" />;
      case 'data':
        return <FileText className="w-5 h-5 text-green-500" />;
      case 'system':
        return <Shield className="w-5 h-5 text-indigo-500" />;
      default:
        return <Info className="w-5 h-5 text-gray-500" />;
    }
  };

  const toggleExpanded = (id: string) => {
    setExpandedItems((prev) =>
      prev.includes(id) ? prev.filter((itemId) => itemId !== id) : [...prev, id]
    );
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

  const handleExportCSV = () => {
    console.log('Exporting to CSV...');
    // Would generate CSV of filtered events
  };

  const handleExportExcel = () => {
    console.log('Exporting to Excel...');
    // Would generate Excel file of filtered events
  };

  // Get unique users for filter
  const uniqueUsers = Array.from(new Set(securityEvents.map((e) => e.user))).sort();

  // Filter events
  const filteredEvents = securityEvents.filter((event) => {
    if (filterEventType !== 'all' && event.eventType !== filterEventType) return false;
    if (filterSeverity !== 'all' && event.severity !== filterSeverity) return false;
    if (filterUser !== 'all' && event.user !== filterUser) return false;
    if (filterSuccess !== 'all') {
      const successFilter = filterSuccess === 'success';
      if (event.success !== successFilter) return false;
    }
    if (searchQuery && !event.action.toLowerCase().includes(searchQuery.toLowerCase()) &&
        !event.details.toLowerCase().includes(searchQuery.toLowerCase())) {
      return false;
    }
    return true;
  });

  // Calculate statistics
  const criticalCount = filteredEvents.filter((e) => e.severity === 'critical').length;
  const highCount = filteredEvents.filter((e) => e.severity === 'high').length;
  const failedCount = filteredEvents.filter((e) => !e.success).length;

  return (
    <div className="flex h-full w-full flex-col gap-6 overflow-auto bg-gradient-to-br from-background via-primary/5 to-background p-8">
      {/* Page Header */}
      <div>
        <h1 className="text-foreground/80">Security Audit Log</h1>
        <p className="text-muted-foreground text-sm mt-1">
          Comprehensive audit trail of security events and system access
        </p>
      </div>

      {/* Statistics Cards */}
      <div className="grid grid-cols-1 gap-4 md:grid-cols-4">
        <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
          <CardContent className="pt-4">
            <div className="flex items-center justify-between">
              <div>
                <p className="text-xs text-muted-foreground">Total Events</p>
                <p className="text-2xl font-semibold mt-1">{filteredEvents.length}</p>
              </div>
              <Shield className="w-8 h-8 text-muted-foreground opacity-50" />
            </div>
          </CardContent>
        </Card>

        <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
          <CardContent className="pt-4">
            <div className="flex items-center justify-between">
              <div>
                <p className="text-xs text-muted-foreground">Critical Events</p>
                <p className="text-2xl font-semibold mt-1 text-red-500">{criticalCount}</p>
              </div>
              <AlertTriangle className="w-8 h-8 text-red-500 opacity-50" />
            </div>
          </CardContent>
        </Card>

        <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
          <CardContent className="pt-4">
            <div className="flex items-center justify-between">
              <div>
                <p className="text-xs text-muted-foreground">High Severity</p>
                <p className="text-2xl font-semibold mt-1 text-orange-500">{highCount}</p>
              </div>
              <AlertTriangle className="w-8 h-8 text-orange-500 opacity-50" />
            </div>
          </CardContent>
        </Card>

        <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
          <CardContent className="pt-4">
            <div className="flex items-center justify-between">
              <div>
                <p className="text-xs text-muted-foreground">Failed Actions</p>
                <p className="text-2xl font-semibold mt-1 text-red-500">{failedCount}</p>
              </div>
              <XCircle className="w-8 h-8 text-red-500 opacity-50" />
            </div>
          </CardContent>
        </Card>
      </div>

      {/* Filters Card */}
      <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
        <CardHeader>
          <CardTitle className="text-foreground/90 flex items-center gap-2">
            <Filter className="w-5 h-5" />
            Filters & Search
          </CardTitle>
        </CardHeader>
        <CardContent>
          <div className="flex flex-wrap items-end gap-4 xl:flex-nowrap">
            <div className="flex min-w-[160px] flex-1 flex-col">
              <Label htmlFor="filter-event-type" className="text-xs text-muted-foreground">
                Event Type
              </Label>
              <Select value={filterEventType} onValueChange={setFilterEventType}>
                <SelectTrigger id="filter-event-type" className="mt-1 text-sm h-8">
                  <SelectValue />
                </SelectTrigger>
                <SelectContent>
                  <SelectItem value="all">All Types</SelectItem>
                  <SelectItem value="authentication">
                    <div className="flex items-center gap-2">
                      <Lock className="w-4 h-4" />
                      Authentication
                    </div>
                  </SelectItem>
                  <SelectItem value="configuration">
                    <div className="flex items-center gap-2">
                      <Settings className="w-4 h-4" />
                      Configuration
                    </div>
                  </SelectItem>
                  <SelectItem value="access">
                    <div className="flex items-center gap-2">
                      <Eye className="w-4 h-4" />
                      Access
                    </div>
                  </SelectItem>
                  <SelectItem value="user-management">
                    <div className="flex items-center gap-2">
                      <User className="w-4 h-4" />
                      User Management
                    </div>
                  </SelectItem>
                  <SelectItem value="system">
                    <div className="flex items-center gap-2">
                      <Shield className="w-4 h-4" />
                      System
                    </div>
                  </SelectItem>
                </SelectContent>
              </Select>
            </div>

            <div className="flex min-w-[140px] flex-1 flex-col">
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
                  <SelectItem value="info">Info</SelectItem>
                </SelectContent>
              </Select>
            </div>

            <div className="flex min-w-[140px] flex-1 flex-col">
              <Label htmlFor="filter-user" className="text-xs text-muted-foreground">
                User
              </Label>
              <Select value={filterUser} onValueChange={setFilterUser}>
                <SelectTrigger id="filter-user" className="mt-1 text-sm h-8">
                  <SelectValue />
                </SelectTrigger>
                <SelectContent>
                  <SelectItem value="all">All Users</SelectItem>
                  {uniqueUsers.map((user) => (
                    <SelectItem key={user} value={user}>
                      {user}
                    </SelectItem>
                  ))}
                </SelectContent>
              </Select>
            </div>

            <div className="flex min-w-[140px] flex-1 flex-col">
              <Label htmlFor="filter-success" className="text-xs text-muted-foreground">
                Status
              </Label>
              <Select value={filterSuccess} onValueChange={setFilterSuccess}>
                <SelectTrigger id="filter-success" className="mt-1 text-sm h-8">
                  <SelectValue />
                </SelectTrigger>
                <SelectContent>
                  <SelectItem value="all">All</SelectItem>
                  <SelectItem value="success">
                    <div className="flex items-center gap-2">
                      <CheckCircle className="w-4 h-4 text-green-500" />
                      Success
                    </div>
                  </SelectItem>
                  <SelectItem value="failed">
                    <div className="flex items-center gap-2">
                      <XCircle className="w-4 h-4 text-red-500" />
                      Failed
                    </div>
                  </SelectItem>
                </SelectContent>
              </Select>
            </div>

            <div className="flex min-w-[140px] flex-1 flex-col">
              <Label htmlFor="date-range" className="text-xs text-muted-foreground">
                Date Range
              </Label>
              <Select value={dateRange} onValueChange={setDateRange}>
                <SelectTrigger id="date-range" className="mt-1 text-sm h-8">
                  <SelectValue />
                </SelectTrigger>
                <SelectContent>
                  <SelectItem value="1d">Last 24 Hours</SelectItem>
                  <SelectItem value="7d">Last 7 Days</SelectItem>
                  <SelectItem value="30d">Last 30 Days</SelectItem>
                  <SelectItem value="90d">Last 90 Days</SelectItem>
                  <SelectItem value="all">All Time</SelectItem>
                </SelectContent>
              </Select>
            </div>

            <div className="flex min-w-[220px] flex-1 flex-col">
              <Label htmlFor="search" className="text-xs text-muted-foreground">
                Search
              </Label>
              <div className="relative mt-1">
                <Search className="absolute left-2 top-1/2 -translate-y-1/2 w-4 h-4 text-muted-foreground" />
                <Input
                  id="search"
                  placeholder="Search events..."
                  value={searchQuery}
                  onChange={(e) => setSearchQuery(e.target.value)}
                  className="pl-8 text-sm h-8"
                />
              </div>
            </div>
          </div>

          <div className="flex items-center justify-between mt-4">
            <p className="text-xs text-muted-foreground">
              Showing {filteredEvents.length} of {securityEvents.length} events
            </p>
            <div className="flex gap-2">
              <Button size="sm" variant="outline" className="text-xs h-7" onClick={handleExportCSV}>
                <Download className="w-3 h-3 mr-1" />
                Export CSV
              </Button>
              <Button size="sm" variant="outline" className="text-xs h-7" onClick={handleExportExcel}>
                <Download className="w-3 h-3 mr-1" />
                Export Excel
              </Button>
            </div>
          </div>
        </CardContent>
      </Card>

      {/* Security Events */}
      <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
        <CardHeader>
          <CardTitle className="text-foreground/90 flex items-center gap-2">
            <Shield className="w-5 h-5" />
            Security Events ({filteredEvents.length})
          </CardTitle>
        </CardHeader>
        <CardContent>
          <div className="space-y-2">
            {filteredEvents.map((event) => {
              const isExpanded = expandedItems.includes(event.id);

              return (
                <div
                  key={event.id}
                  className={`border rounded-lg transition-all ${
                    event.success
                      ? 'border-border bg-background'
                      : 'border-red-500/30 bg-red-500/5'
                  }`}
                >
                  {/* Summary Row */}
                  <div className="p-3 flex items-center justify-between">
                    <div className="flex items-center gap-3 flex-1">
                      <Button
                        size="sm"
                        variant="ghost"
                        className="h-6 w-6 p-0"
                        onClick={() => toggleExpanded(event.id)}
                      >
                        {isExpanded ? (
                          <ChevronDown className="w-4 h-4" />
                        ) : (
                          <ChevronRight className="w-4 h-4" />
                        )}
                      </Button>

                      {getEventIcon(event.eventType, event.action)}

                      <div className="flex-1">
                        <div className="flex items-center gap-2">
                          <p className="text-sm font-medium text-foreground/90">{event.action}</p>
                          {!event.success && (
                            <Badge variant="destructive" className="text-xs">
                              Failed
                            </Badge>
                          )}
                        </div>
                        <p className="text-xs text-muted-foreground">
                          {event.user} • {event.ipAddress}
                        </p>
                      </div>

                      <div className="text-xs text-muted-foreground">
                        {formatTimestamp(event.timestamp)}
                      </div>

                      {getSeverityBadge(event.severity)}
                    </div>
                  </div>

                  {/* Expanded Details */}
                  {isExpanded && (
                    <>
                      <Separator />
                      <div className="p-4 space-y-4 bg-muted/20">
                        {/* Details */}
                        <div>
                          <p className="text-xs text-muted-foreground mb-1">Event Details</p>
                          <p className="text-sm bg-muted/50 rounded p-2 border border-border">
                            {event.details}
                          </p>
                        </div>

                        <div className="grid grid-cols-2 gap-4">
                          {/* Event Information */}
                          <div className="space-y-3">
                            <div>
                              <p className="text-xs text-muted-foreground mb-1">Timestamp</p>
                              <p className="text-sm font-mono">
                                {new Date(event.timestamp).toLocaleString()}
                              </p>
                            </div>

                            <div>
                              <p className="text-xs text-muted-foreground mb-1">Event Type</p>
                              <Badge variant="outline" className="text-xs">
                                {event.eventType
                                  .split('-')
                                  .map((word) => word.charAt(0).toUpperCase() + word.slice(1))
                                  .join(' ')}
                              </Badge>
                            </div>

                            {event.resource && (
                              <div>
                                <p className="text-xs text-muted-foreground mb-1">Resource</p>
                                <p className="text-sm font-mono bg-muted/50 rounded px-2 py-1">
                                  {event.resource}
                                </p>
                              </div>
                            )}

                            {event.sessionId && (
                              <div>
                                <p className="text-xs text-muted-foreground mb-1">Session ID</p>
                                <p className="text-xs font-mono text-muted-foreground">
                                  {event.sessionId}
                                </p>
                              </div>
                            )}
                          </div>

                          {/* User & Network Information */}
                          <div className="space-y-3">
                            <div>
                              <p className="text-xs text-muted-foreground mb-1">User</p>
                              <p className="text-sm">{event.user}</p>
                            </div>

                            <div>
                              <p className="text-xs text-muted-foreground mb-1">IP Address</p>
                              <p className="text-sm font-mono">{event.ipAddress}</p>
                            </div>

                            {event.userAgent && (
                              <div>
                                <p className="text-xs text-muted-foreground mb-1">User Agent</p>
                                <p className="text-xs font-mono text-muted-foreground break-all">
                                  {event.userAgent}
                                </p>
                              </div>
                            )}
                          </div>
                        </div>

                        {/* Value Changes */}
                        {(event.oldValue || event.newValue) && (
                          <div>
                            <p className="text-xs text-muted-foreground mb-2">Value Changes</p>
                            <div className="grid grid-cols-2 gap-4">
                              {event.oldValue && (
                                <div>
                                  <p className="text-xs text-muted-foreground mb-1">Old Value</p>
                                  <p className="text-sm bg-red-500/10 border border-red-500/30 rounded p-2 font-mono">
                                    {event.oldValue}
                                  </p>
                                </div>
                              )}
                              {event.newValue && (
                                <div>
                                  <p className="text-xs text-muted-foreground mb-1">New Value</p>
                                  <p className="text-sm bg-green-500/10 border border-green-500/30 rounded p-2 font-mono">
                                    {event.newValue}
                                  </p>
                                </div>
                              )}
                            </div>
                          </div>
                        )}

                        {/* Audit Note */}
                        <p
                          className="text-muted-foreground opacity-60 pt-2 border-t border-border"
                          style={{ fontSize: '0.7rem', lineHeight: '1.3' }}
                        >
                          Security events are retained for audit and compliance purposes and cannot
                          be deleted
                        </p>
                      </div>
                    </>
                  )}
                </div>
              );
            })}

            {filteredEvents.length === 0 && (
              <div className="text-center py-12 text-muted-foreground">
                <Shield className="w-12 h-12 mx-auto mb-3 opacity-50" />
                <p className="text-sm">No security events found</p>
                <p className="text-xs mt-1">Try adjusting your filters</p>
              </div>
            )}
          </div>
        </CardContent>
      </Card>
    </div>
  );
}
