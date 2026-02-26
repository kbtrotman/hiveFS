import { useEffect, useMemo, useState } from 'react';
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from '../../../ui/card';
import { Button } from '../../../ui/button';
import { Switch } from '../../../ui/switch';
import { Label } from '../../../ui/label';
import { Input } from '../../../ui/input';
import { Badge } from '../../../ui/badge';
import { Separator } from '../../../ui/separator';
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '../../../ui/select';
import {
  Shield,
  Lock,
  Key,
  AlertTriangle,
  Search,
  Download,
  ChevronDown,
  ChevronRight,
  Settings,
  Eye,
  User,
  FileText,
  LogIn,
  LogOut,
  XCircle,
  Info,
} from 'lucide-react';

const API_BASE = import.meta?.env?.VITE_NODES_API_BASE_URL ?? 'http://localhost:8000/api/v1';

const securitySettings = [
  { id: '2fa', label: 'Two-Factor Authentication', description: 'Require 2FA for all users', enabled: true },
  { id: 'ip-whitelist', label: 'IP Whitelist', description: 'Restrict access to approved IPs', enabled: true },
  { id: 'encryption', label: 'Encryption at Rest', description: 'Encrypt all stored data', enabled: true },
  { id: 'session-timeout', label: 'Session Timeout', description: 'Auto-logout after 30 minutes of inactivity', enabled: true },
  { id: 'audit-logging', label: 'Audit Logging', description: 'Log all security events', enabled: true },
  { id: 'api-hardening', label: 'API Hardening', description: 'Require signed requests for APIs', enabled: false },
];

interface AuditEntry {
  audit_entry_id?: number | string;
  id?: number | string;
  created_at?: string | null;
  updated_at?: string | null;
  page_name?: string | null;
  change_summary?: string | null;
  description?: string | null;
  metadata?: Record<string, unknown> | null;
  user_name?: string | null;
  username?: string | null;
  ip_address?: string | null;
}

interface NormalizedAuditEvent {
  id: string;
  timestamp: string;
  timestampMs: number;
  action: string;
  details: string;
  eventType: string;
  severity: string;
  user: string;
  ipAddress: string;
  success: boolean;
  resource?: string;
  sessionId?: string;
  userAgent?: string;
  oldValue?: string;
  newValue?: string;
  raw: AuditEntry;
}

const normalizeAuditEntries = (payload: unknown): AuditEntry[] => {
  if (Array.isArray(payload)) return payload;
  if (payload && Array.isArray((payload as any).results)) return (payload as any).results;
  return [];
};

const getTimestampValue = (entry: AuditEntry): string | null => entry.updated_at ?? entry.created_at ?? null;

const isFailedLoginEntry = (entry: AuditEntry): boolean => {
  const page = (entry.page_name ?? '').toLowerCase();
  const summary = (entry.change_summary ?? '').toLowerCase();
  return page === 'login' && summary.includes('fail');
};

const getAuditTitle = (entry: AuditEntry): string =>
  entry.change_summary ?? entry.description ?? entry.page_name ?? 'Audit Event';

const deriveEventType = (entry: AuditEntry): string => {
  const metadata = (entry.metadata && typeof entry.metadata === 'object' ? entry.metadata : {}) as Record<string, any>;
  const raw = (metadata.event_type ?? metadata.type ?? entry.page_name ?? 'general').toString();
  return raw.toLowerCase();
};

const deriveSeverity = (entry: AuditEntry): string => {
  const metadata = (entry.metadata && typeof entry.metadata === 'object' ? entry.metadata : {}) as Record<string, any>;
  const explicit = metadata.severity ?? metadata.level;
  if (explicit && typeof explicit === 'string') return explicit.toLowerCase();
  return isFailedLoginEntry(entry) ? 'high' : 'info';
};

const deriveSuccess = (entry: AuditEntry): boolean => {
  const metadata = (entry.metadata && typeof entry.metadata === 'object' ? entry.metadata : {}) as Record<string, any>;
  if (typeof metadata.success === 'boolean') return metadata.success;
  const summary = (entry.change_summary ?? '').toLowerCase();
  return !summary.includes('fail') && !summary.includes('error');
};

const getDateRangeMs = (value: string): number | null => {
  switch (value) {
    case '1d':
      return 24 * 60 * 60 * 1000;
    case '7d':
      return 7 * 24 * 60 * 60 * 1000;
    case '30d':
      return 30 * 24 * 60 * 60 * 1000;
    case '90d':
      return 90 * 24 * 60 * 60 * 1000;
    default:
      return null;
  }
};

const formatTimestamp = (timestamp: string) => {
  const date = new Date(timestamp);
  if (Number.isNaN(date.getTime())) return '—';
  const now = Date.now();
  const diff = now - date.getTime();
  const minutes = Math.floor(diff / 60000);
  if (minutes < 1) return 'Just now';
  if (minutes < 60) return `${minutes}m ago`;
  const hours = Math.floor(minutes / 60);
  if (hours < 24) return `${hours}h ago`;
  const days = Math.floor(hours / 24);
  if (days < 7) return `${days}d ago`;
  return date.toLocaleString();
};

const severityColors: Record<string, string> = {
  critical: 'bg-red-500',
  high: 'bg-orange-500',
  medium: 'bg-yellow-500',
  low: 'bg-blue-500',
  info: 'bg-gray-500',
};

const getSeverityBadge = (severity: string) => {
  const color = severityColors[severity] ?? 'bg-gray-500';
  const label = severity.charAt(0).toUpperCase() + severity.slice(1);
  return (
    <Badge variant="outline" className="text-xs">
      <span className={`w-2 h-2 rounded-full ${color} mr-1.5`} />
      {label}
    </Badge>
  );
};

const getEventIcon = (eventType: string, action: string) => {
  if (action.toLowerCase().includes('failed')) return <XCircle className="w-5 h-5 text-red-500" />;
  if (action.includes('Login')) return <LogIn className="w-5 h-5 text-green-500" />;
  if (action.includes('Logout')) return <LogOut className="w-5 h-5 text-gray-500" />;

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

export function SecurityAuditTab() {
  const [auditEvents, setAuditEvents] = useState<AuditEntry[]>([]);
  const [auditLoading, setAuditLoading] = useState(false);
  const [auditError, setAuditError] = useState<string | null>(null);

  const [filterEventType, setFilterEventType] = useState<string>('all');
  const [filterSeverity, setFilterSeverity] = useState<string>('all');
  const [filterUser, setFilterUser] = useState<string>('all');
  const [filterSuccess, setFilterSuccess] = useState<string>('all');
  const [dateRange, setDateRange] = useState<string>('7d');
  const [searchQuery, setSearchQuery] = useState('');
  const [expandedItems, setExpandedItems] = useState<string[]>([]);

  useEffect(() => {
    const abortController = new AbortController();
    const loadAudit = async () => {
      setAuditLoading(true);
      setAuditError(null);
      try {
        const response = await fetch(`${API_BASE}/audit?limit=200`, { signal: abortController.signal });
        if (!response.ok) throw new Error(`Failed to load audit events (${response.status})`);
        const payload = await response.json();
        setAuditEvents(normalizeAuditEntries(payload));
      } catch (error) {
        if (!abortController.signal.aborted) {
          setAuditError(error instanceof Error ? error.message : 'Unable to load audit events');
          setAuditEvents([]);
        }
      } finally {
        if (!abortController.signal.aborted) setAuditLoading(false);
      }
    };

    loadAudit();
    return () => abortController.abort();
  }, []);

  const normalizedEvents = useMemo<NormalizedAuditEvent[]>(() => {
    return auditEvents.map((entry, index) => {
      const metadata = (entry.metadata && typeof entry.metadata === 'object' ? entry.metadata : {}) as Record<string, any>;
      const timestampRaw = getTimestampValue(entry) ?? new Date().toISOString();
      const timestampMs = Date.parse(timestampRaw);
      const eventType = deriveEventType(entry);
      const severity = deriveSeverity(entry);
      const success = deriveSuccess(entry);
      const action = getAuditTitle(entry);
      const details = entry.description ?? metadata.details ?? action;
      const user = entry.user_name ?? entry.username ?? metadata.user ?? 'Unknown user';
      const ipAddress = entry.ip_address ?? metadata.ip ?? metadata.ip_address ?? 'Unknown IP';

      return {
        id: String(entry.audit_entry_id ?? entry.id ?? index),
        timestamp: timestampRaw,
        timestampMs: Number.isFinite(timestampMs) ? timestampMs : Date.now(),
        action,
        details,
        eventType,
        severity,
        user,
        ipAddress,
        success,
        resource: metadata.resource ?? metadata.target ?? undefined,
        sessionId: metadata.session_id ?? metadata.sessionId ?? undefined,
        userAgent: metadata.user_agent ?? metadata.userAgent ?? undefined,
        oldValue: metadata.old_value ?? metadata.oldValue ?? undefined,
        newValue: metadata.new_value ?? metadata.newValue ?? undefined,
        raw: entry,
      } satisfies NormalizedAuditEvent;
    });
  }, [auditEvents]);

  const failedLoginCount = useMemo(
    () => auditEvents.filter((entry) => isFailedLoginEntry(entry)).length,
    [auditEvents],
  );

  const uniqueUsers = useMemo(
    () => Array.from(new Set(normalizedEvents.map((event) => event.user).filter(Boolean))).sort(),
    [normalizedEvents],
  );

  const filteredEvents = useMemo(() => {
    const rangeMs = getDateRangeMs(dateRange);
    const now = Date.now();
    const query = searchQuery.trim().toLowerCase();

    return normalizedEvents.filter((event) => {
      if (filterEventType !== 'all' && event.eventType !== filterEventType) return false;
      if (filterSeverity !== 'all' && event.severity !== filterSeverity) return false;
      if (filterUser !== 'all' && event.user !== filterUser) return false;
      if (filterSuccess !== 'all') {
        const successTarget = filterSuccess === 'success';
        if (event.success !== successTarget) return false;
      }
      if (rangeMs !== null && now - event.timestampMs > rangeMs) return false;
      if (query) {
        const haystack = `${event.action} ${event.details} ${event.resource ?? ''}`.toLowerCase();
        if (!haystack.includes(query)) return false;
      }
      return true;
    });
  }, [normalizedEvents, filterEventType, filterSeverity, filterUser, filterSuccess, dateRange, searchQuery]);

  const toggleExpanded = (id: string) => {
    setExpandedItems((prev) => (prev.includes(id) ? prev.filter((itemId) => itemId !== id) : [...prev, id]));
  };

  const handleExport = (format: 'csv' | 'excel') => {
    const headers = [
      'Timestamp',
      'Action',
      'User',
      'IP Address',
      'Type',
      'Severity',
      'Status',
      'Details',
    ];
    const rows = filteredEvents.map((event) => [
      event.timestamp,
      event.action,
      event.user,
      event.ipAddress,
      event.eventType,
      event.severity,
      event.success ? 'Success' : 'Failed',
      event.details,
    ]);
    const csv = [headers, ...rows]
      .map((row) => row.map((value) => `"${String(value ?? '').replace(/"/g, '""')}"`).join(','))
      .join('\n');
    const blob = new Blob([csv], {
      type: format === 'excel' ? 'application/vnd.ms-excel' : 'text/csv;charset=utf-8;',
    });
    const url = URL.createObjectURL(blob);
    const link = document.createElement('a');
    link.href = url;
    link.setAttribute('download', `audit_events_${new Date().toISOString()}.${format === 'excel' ? 'xls' : 'csv'}`);
    document.body.appendChild(link);
    link.click();
    link.remove();
    URL.revokeObjectURL(url);
  };

  return (
    <div className="space-y-6">
      <div className="flex justify-between items-center">
        <div>
          <h2>Security Audit</h2>
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
                <p className="mt-2 text-yellow-500">{auditLoading ? '…' : failedLoginCount}</p>
                <p className="text-xs text-muted-foreground">Last 200 audit entries</p>
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
        <CardContent>
          <div className="flex flex-wrap gap-4">
            {securitySettings.map((setting) => (
              <div
                key={setting.id}
                className="flex min-w-[220px] flex-1 basis-full items-start justify-between rounded-lg border border-border/60 bg-muted/20 px-4 py-3 md:basis-[calc(50%-0.5rem)] xl:basis-[calc(33.333%-0.75rem)]"
              >
                <div className="pr-3">
                  <Label className="text-sm font-medium">{setting.label}</Label>
                  <p className="text-xs text-muted-foreground">{setting.description}</p>
                </div>
                <Switch defaultChecked={setting.enabled} aria-label={setting.label} />
              </div>
            ))}
          </div>
        </CardContent>
      </Card>

      <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
        <CardHeader>
          <CardTitle className="text-foreground/90 flex items-center gap-2">
            <Search className="w-5 h-5" />
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
                  <SelectItem value="authentication">Authentication</SelectItem>
                  <SelectItem value="configuration">Configuration</SelectItem>
                  <SelectItem value="access">Access</SelectItem>
                  <SelectItem value="user-management">User Management</SelectItem>
                  <SelectItem value="system">System</SelectItem>
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
                  <SelectItem value="success">Success</SelectItem>
                  <SelectItem value="failed">Failed</SelectItem>
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
              Showing {filteredEvents.length} of {normalizedEvents.length} events
            </p>
            <div className="flex gap-2">
              <Button size="sm" variant="outline" className="text-xs h-7" onClick={() => handleExport('csv')}>
                <Download className="w-3 h-3 mr-1" />
                Export CSV
              </Button>
              <Button size="sm" variant="outline" className="text-xs h-7" onClick={() => handleExport('excel')}>
                <Download className="w-3 h-3 mr-1" />
                Export Excel
              </Button>
            </div>
          </div>
        </CardContent>
      </Card>

      <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
        <CardHeader>
          <CardTitle className="text-foreground/90 flex items-center gap-2">
            <Shield className="w-5 h-5" />
            Recent Audit Events ({filteredEvents.length})
          </CardTitle>
        </CardHeader>
        <CardContent>
          {auditLoading && <p className="text-sm text-muted-foreground">Loading audit events…</p>}
          {!auditLoading && filteredEvents.length === 0 && !auditError && (
            <div className="text-center py-12 text-muted-foreground">
              <Shield className="w-12 h-12 mx-auto mb-3 opacity-50" />
              <p className="text-sm">No audit events found</p>
              <p className="text-xs mt-1">Try adjusting your filters</p>
            </div>
          )}

          <div className="space-y-2">
            {filteredEvents.map((event) => {
              const isExpanded = expandedItems.includes(event.id);

              return (
                <div
                  key={event.id}
                  className={`border rounded-lg transition-all ${event.success ? 'border-border bg-background' : 'border-red-500/30 bg-red-500/5'}`}
                >
                  <div className="p-3 flex items-center justify-between">
                    <div className="flex items-center gap-3 flex-1">
                      <Button size="sm" variant="ghost" className="h-6 w-6 p-0" onClick={() => toggleExpanded(event.id)}>
                        {isExpanded ? <ChevronDown className="w-4 h-4" /> : <ChevronRight className="w-4 h-4" />}
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

                      <div className="text-xs text-muted-foreground">{formatTimestamp(event.timestamp)}</div>

                      {getSeverityBadge(event.severity)}
                    </div>
                  </div>

                  {isExpanded && (
                    <>
                      <Separator />
                      <div className="p-4 space-y-4 bg-muted/20">
                        <div>
                          <p className="text-xs text-muted-foreground mb-1">Event Details</p>
                          <p className="text-sm bg-muted/50 rounded p-2 border border-border">{event.details}</p>
                        </div>

                        <div className="grid grid-cols-2 gap-4">
                          <div className="space-y-3">
                            <div>
                              <p className="text-xs text-muted-foreground mb-1">Timestamp</p>
                              <p className="text-sm font-mono">{new Date(event.timestamp).toLocaleString()}</p>
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
                                <p className="text-sm font-mono bg-muted/50 rounded px-2 py-1">{event.resource}</p>
                              </div>
                            )}
                            {event.sessionId && (
                              <div>
                                <p className="text-xs text-muted-foreground mb-1">Session ID</p>
                                <p className="text-xs font-mono text-muted-foreground">{event.sessionId}</p>
                              </div>
                            )}
                          </div>

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
                                <p className="text-xs font-mono text-muted-foreground break-all">{event.userAgent}</p>
                              </div>
                            )}
                          </div>
                        </div>

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

                        <p className="text-muted-foreground opacity-60 pt-2 border-t border-border" style={{ fontSize: '0.7rem', lineHeight: '1.3' }}>
                          Security events are retained for audit and compliance purposes and cannot be deleted
                        </p>
                      </div>
                    </>
                  )}
                </div>
              );
            })}
          </div>

          {auditError && <p className="mt-3 text-xs text-destructive">{auditError}</p>}
        </CardContent>
      </Card>
    </div>
  );
}
