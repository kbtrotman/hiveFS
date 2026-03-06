import { useEffect, useMemo, useState } from 'react';
import { Card, CardContent, CardHeader, CardTitle } from '../../../ui/card';
import { Input } from '../../../ui/input';
import { Label } from '../../../ui/label';
import { Button } from '../../../ui/button';
import { Separator } from '../../../ui/separator';
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '../../../ui/select';
import { Badge } from '../../../ui/badge';
import { Checkbox } from '../../../ui/checkbox';
import {
  FileText,
  Search,
  ChevronDown,
  ChevronRight,
  AlertTriangle,
  Info,
  AlertCircle,
  XCircle,
  Bug,
  Filter,
  Mail,
  Bell,
  CheckSquare,
  Square,
  Minus,
  X,
  Send,
  Package,
  Activity,
} from 'lucide-react';
import { API_BASE, useApiResource } from '../../../useApiResource';
interface LogEntry {
  id: string;
  timestamp: string;
  level: 'DEBUG' | 'INFO' | 'WARN' | 'ERROR' | 'CRITICAL';
  source: string;
  node: string;
  message: string;
  details?: string;
  stackTrace?: string;
  metadata?: Record<string, any>;
}

const LOG_SOURCES = [
  { label: 'Hive Guard (legacy)', key: 'hive_guard_log' },
  { label: 'Hive Bootstrap (legacy)', key: 'hive_bootstrap_log' },
  { label: 'Hive Guard SQL', key: 'hive_guard_sql_log' },
  { label: 'Hive Guard', key: 'hive_guard.log' },
  { label: 'Hive Bootstrap', key: 'hive_bootstrap.log' },
];
const LOG_SOURCE_KEYS = new Set(LOG_SOURCES.map((log) => log.key));

interface User {
  id: string;
  name: string;
  email: string;
  role: string;
}

interface AuditEntryRecord {
  audit_entry_id: number;
  page_name: string;
  setting_name: string;
  change_summary?: string | null;
  change_context?: Record<string, any> | null;
  request_ip?: string | null;
  username?: string | null;
  created_at?: string | null;
}

interface AccountRecord {
  id: number;
  email: string;
  username: string;
  first_name: string;
  last_name: string;
  auth_source?: string | null;
}

export function SecurityLogsTab() {
  const [filterLevel, setFilterLevel] = useState<string>('all');
  const [selectedLogSource, setSelectedLogSource] = useState<string>('all');
  const [filterNode, setFilterNode] = useState<string>('all');
  const [searchQuery, setSearchQuery] = useState('');
  const [expandedLogs, setExpandedLogs] = useState<string[]>([]);
  const [selectedLogs, setSelectedLogs] = useState<string[]>([]);
  const [isLiveMode, setIsLiveMode] = useState(false);
  const [showExportDialog, setShowExportDialog] = useState(false);
  const [showNotificationDialog, setShowNotificationDialog] = useState(false);
  const [exportRecipients, setExportRecipients] = useState<string[]>([]);
  const [exportIncludeSupport, setExportIncludeSupport] = useState(false);
  const [logContent, setLogContent] = useState<Record<string, string>>({});
  const [logMeta, setLogMeta] = useState<Record<string, { truncated: boolean }>>({});
  const [logLoading, setLogLoading] = useState(false);
  const [logError, setLogError] = useState<string | null>(null);

  const { data: auditEntries } = useApiResource<AuditEntryRecord[]>('audit?limit=200', {
    initialData: [],
    transform: (payload) => (Array.isArray(payload) ? payload : []),
  });
  const { data: accountRecords } = useApiResource<AccountRecord[]>('accounts', {
    initialData: [],
    transform: (payload) => (Array.isArray(payload) ? payload : []),
  });

  const users: User[] = useMemo(
    () =>
      accountRecords.map((account) => ({
        id: account.id.toString(),
        name:
          account.first_name || account.last_name
            ? `${account.first_name} ${account.last_name}`.trim()
            : account.username,
        email: account.email,
        role: account.auth_source || 'user',
      })),
    [accountRecords],
  );

  const isLogViewerActive = selectedLogSource === 'all' || LOG_SOURCE_KEYS.has(selectedLogSource);

  useEffect(() => {
    let cancelled = false;

    const loadLogs = async () => {
      if (!isLogViewerActive) {
        setLogLoading(false);
        setLogError(null);
        return;
      }

      const keysToFetch =
        selectedLogSource === 'all'
          ? LOG_SOURCES.map((source) => source.key)
          : [selectedLogSource];
      const missingKeys = keysToFetch.filter((key) => !logContent[key]);
      if (!missingKeys.length) {
        setLogLoading(false);
        setLogError(null);
        return;
      }

      setLogLoading(true);
      setLogError(null);

      try {
        const token = localStorage.getItem('authToken');
        const authHeaders: Record<string, string> = token ? { Authorization: `Token ${token}` } : {};

        const results = await Promise.all(
          missingKeys.map(async (key) => {
            const response = await fetch(
              `${API_BASE}/audit/logs?log=${encodeURIComponent(key)}&max_bytes=1048576`,
              { headers: authHeaders },
            );
            if (!response.ok) {
              throw new Error(`Failed to load ${key} (${response.status})`);
            }
            const payload = await response.json();
            return {
              key,
              content: payload?.content ?? '',
              truncated: Boolean(payload?.truncated),
            };
          }),
        );

        if (cancelled) {
          return;
        }

        setLogContent((prev) => {
          const next = { ...prev };
          for (const item of results) {
            next[item.key] = item.content;
          }
          return next;
        });
        setLogMeta((prev) => {
          const next = { ...prev };
          for (const item of results) {
            next[item.key] = { truncated: item.truncated };
          }
          return next;
        });
      } catch (error) {
        if (!cancelled) {
          setLogError(error instanceof Error ? error.message : 'Unable to load log content');
        }
      } finally {
        if (!cancelled) {
          setLogLoading(false);
        }
      }
    };

    void loadLogs();

    return () => {
      cancelled = true;
    };
  }, [isLogViewerActive, logContent, selectedLogSource]);

  const logViewerText = useMemo(() => {
    if (!isLogViewerActive) {
      return '';
    }
    if (selectedLogSource === 'all') {
      const segments = LOG_SOURCES.map(({ label, key }) => {
        const content = logContent[key];
        if (!content) {
          return null;
        }
        return `===== ${label} (${key}) =====\n${content.trim()}`;
      }).filter(Boolean);
      return segments.join('\n\n');
    }

    return logContent[selectedLogSource] ?? '';
  }, [isLogViewerActive, logContent, selectedLogSource]);

  const logsTruncated =
    selectedLogSource === 'all'
      ? LOG_SOURCES.some(({ key }) => logMeta[key]?.truncated)
      : Boolean(logMeta[selectedLogSource]?.truncated);
  const selectedLogLabel =
    selectedLogSource === 'all'
      ? 'All Logs'
      : LOG_SOURCES.find((source) => source.key === selectedLogSource)?.label ?? selectedLogSource;

  const logEntries: LogEntry[] = useMemo(() => {
    if (!auditEntries.length) return [];
    return auditEntries.map((entry) => {
      const severityRaw =
        (entry.change_context && entry.change_context.severity) ||
        (entry.change_context && entry.change_context.level) ||
        'INFO';
      const normalized = String(severityRaw).toUpperCase();
      const mapped: LogEntry['level'] =
        normalized === 'CRITICAL' || normalized === 'ERROR' || normalized === 'WARN' || normalized === 'DEBUG'
          ? (normalized as LogEntry['level'])
          : 'INFO';
      return {
        id: String(entry.audit_entry_id),
        timestamp: entry.created_at || new Date().toISOString(),
        level: mapped,
        source: entry.page_name || 'system',
        node: entry.change_context?.node_id ? `node-${entry.change_context.node_id}` : 'cluster',
        message: entry.change_summary || entry.setting_name,
        details: entry.change_context?.details || entry.setting_name,
        metadata: entry.change_context || undefined,
      };
    });
  }, [auditEntries]);


  const getLevelColor = (level: string) => {
    switch (level) {
      case 'CRITICAL':
        return 'text-red-600 dark:text-red-400';
      case 'ERROR':
        return 'text-red-500 dark:text-red-400';
      case 'WARN':
        return 'text-yellow-600 dark:text-yellow-500';
      case 'INFO':
        return 'text-blue-500 dark:text-blue-400';
      case 'DEBUG':
        return 'text-gray-500 dark:text-gray-400';
      default:
        return 'text-gray-500';
    }
  };

  const getLevelBadge = (level: string) => {
    const colors = {
      CRITICAL: 'bg-red-600',
      ERROR: 'bg-red-500',
      WARN: 'bg-yellow-500',
      INFO: 'bg-blue-500',
      DEBUG: 'bg-gray-500',
    };

    const icons = {
      CRITICAL: XCircle,
      ERROR: AlertCircle,
      WARN: AlertTriangle,
      INFO: Info,
      DEBUG: Bug,
    };

    const Icon = icons[level as keyof typeof icons];

    return (
      <Badge variant="outline" className={`text-xs ${getLevelColor(level)} border-current`}>
        <Icon className="w-3 h-3 mr-1" />
        {level}
      </Badge>
    );
  };

  const toggleExpanded = (id: string) => {
    setExpandedLogs((prev) =>
      prev.includes(id) ? prev.filter((logId) => logId !== id) : [...prev, id]
    );
  };

  const toggleSelected = (id: string) => {
    setSelectedLogs((prev) =>
      prev.includes(id) ? prev.filter((logId) => logId !== id) : [...prev, id]
    );
  };

  const toggleSelectAll = () => {
    if (selectedLogs.length === filteredLogs.length) {
      setSelectedLogs([]);
    } else {
      setSelectedLogs(filteredLogs.map((log) => log.id));
    }
  };

  const formatTimestamp = (timestamp: string) => {
    const date = new Date(timestamp);
    return date.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' }) +
      '.' + date.getMilliseconds().toString().padStart(3, '0');
  };

  const formatFullTimestamp = (timestamp: string) => {
    const date = new Date(timestamp);
    return date.toLocaleString();
  };

  const uniqueNodes = Array.from(new Set(logEntries.map((e) => e.node))).sort();

  // Filter logs
  const filteredLogs = logEntries.filter((log) => {
    if (filterLevel !== 'all' && log.level !== filterLevel) return false;
    if (filterNode !== 'all' && log.node !== filterNode) return false;
    if (
      searchQuery &&
      !log.message.toLowerCase().includes(searchQuery.toLowerCase()) &&
      !log.details?.toLowerCase().includes(searchQuery.toLowerCase())
    ) {
      return false;
    }
    return true;
  });

  // Calculate statistics
  const criticalCount = filteredLogs.filter((l) => l.level === 'CRITICAL').length;
  const errorCount = filteredLogs.filter((l) => l.level === 'ERROR').length;
  const warnCount = filteredLogs.filter((l) => l.level === 'WARN').length;

  const handleExportLogs = () => {
    if (selectedLogs.length === 0) {
      console.log('No logs selected for export');
      return;
    }
    setShowExportDialog(true);
  };

  const handleSendExport = () => {
    const recipients = exportRecipients.map((id) => users.find((u) => u.id === id)?.email).filter(Boolean);
    if (exportIncludeSupport) {
      recipients.push('support@hivefs.com');
    }

    console.log('Exporting logs to:', recipients);
    console.log('Selected logs:', selectedLogs);
    console.log('Would create ZIP file and email to recipients');

    setShowExportDialog(false);
    setExportRecipients([]);
    setExportIncludeSupport(false);
  };

  const handleCreateNotification = () => {
    if (selectedLogs.length === 0) {
      console.log('No logs selected for notification');
      return;
    }
    setShowNotificationDialog(true);
  };

  const toggleRecipient = (userId: string) => {
    setExportRecipients((prev) =>
      prev.includes(userId) ? prev.filter((id) => id !== userId) : [...prev, userId]
    );
  };

  const getSelectedLogsData = () => {
    return logEntries.filter((log) => selectedLogs.includes(log.id));
  };

  return (
    <div className="flex h-full w-full flex-col gap-6 overflow-auto bg-gradient-to-br from-background via-primary/5 to-background p-8">
      {/* Page Header */}
      <div>
        <h1 className="text-foreground/80">Application Logs</h1>
        <p className="text-muted-foreground text-sm mt-1">
          View and manage logs from HiveFS cluster daemons and services
        </p>
      </div>

      {/* Statistics Cards */}
      <div className="grid grid-cols-1 gap-4 sm:grid-cols-2 lg:grid-cols-4">
        <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
          <CardContent className="pt-4">
            <div className="flex items-center justify-between">
              <div>
                <p className="text-xs text-muted-foreground">Total Logs</p>
                <p className="text-2xl font-semibold mt-1">{filteredLogs.length}</p>
              </div>
              <FileText className="w-8 h-8 text-muted-foreground opacity-50" />
            </div>
          </CardContent>
        </Card>

        <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
          <CardContent className="pt-4">
            <div className="flex items-center justify-between">
              <div>
                <p className="text-xs text-muted-foreground">Critical</p>
                <p className="text-2xl font-semibold mt-1 text-red-600">{criticalCount}</p>
              </div>
              <XCircle className="w-8 h-8 text-red-600 opacity-50" />
            </div>
          </CardContent>
        </Card>

        <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
          <CardContent className="pt-4">
            <div className="flex items-center justify-between">
              <div>
                <p className="text-xs text-muted-foreground">Errors</p>
                <p className="text-2xl font-semibold mt-1 text-red-500">{errorCount}</p>
              </div>
              <AlertCircle className="w-8 h-8 text-red-500 opacity-50" />
            </div>
          </CardContent>
        </Card>

        <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
          <CardContent className="pt-4">
            <div className="flex items-center justify-between">
              <div>
                <p className="text-xs text-muted-foreground">Warnings</p>
                <p className="text-2xl font-semibold mt-1 text-yellow-600">{warnCount}</p>
              </div>
              <AlertTriangle className="w-8 h-8 text-yellow-600 opacity-50" />
            </div>
          </CardContent>
        </Card>
      </div>

      {/* Filters and Actions */}
      <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
        <CardHeader>
          <CardTitle className="text-foreground/90 flex items-center gap-2">
            <Filter className="w-5 h-5" />
            Filters & Actions
          </CardTitle>
        </CardHeader>
        <CardContent>
          <div className="grid gap-4 sm:grid-cols-2 lg:grid-cols-4">
            <div className="flex flex-col">
              <Label htmlFor="log-source" className="text-xs text-muted-foreground">
                Log Source
              </Label>
              <Select value={selectedLogSource} onValueChange={setSelectedLogSource}>
                <SelectTrigger id="log-source" className="mt-1 text-sm h-8">
                  <SelectValue placeholder="Select log" />
                </SelectTrigger>
                <SelectContent>
                  <SelectItem value="all">All Logs (concatenated)</SelectItem>
                  {LOG_SOURCES.map((source) => (
                    <SelectItem key={source.key} value={source.key}>
                      {source.label}
                    </SelectItem>
                  ))}
                </SelectContent>
              </Select>
            </div>

            <div className="flex flex-col">
              <Label htmlFor="filter-level" className="text-xs text-muted-foreground">
                Log Level
              </Label>
              <Select value={filterLevel} onValueChange={setFilterLevel}>
                <SelectTrigger id="filter-level" className="mt-1 text-sm h-8">
                  <SelectValue />
                </SelectTrigger>
                <SelectContent>
                  <SelectItem value="all">All Levels</SelectItem>
                  <SelectItem value="CRITICAL">Critical</SelectItem>
                  <SelectItem value="ERROR">Error</SelectItem>
                  <SelectItem value="WARN">Warning</SelectItem>
                  <SelectItem value="INFO">Info</SelectItem>
                  <SelectItem value="DEBUG">Debug</SelectItem>
                </SelectContent>
              </Select>
            </div>

            <div className="flex flex-col">
              <Label htmlFor="filter-node" className="text-xs text-muted-foreground">
                Node
              </Label>
              <Select value={filterNode} onValueChange={setFilterNode}>
                <SelectTrigger id="filter-node" className="mt-1 text-sm h-8">
                  <SelectValue />
                </SelectTrigger>
                <SelectContent>
                  <SelectItem value="all">All Nodes</SelectItem>
                  {uniqueNodes.map((node) => (
                    <SelectItem key={node} value={node}>
                      {node}
                    </SelectItem>
                  ))}
                </SelectContent>
              </Select>
            </div>

            <div className="flex flex-col sm:col-span-2 lg:col-span-1">
              <Label htmlFor="search" className="text-xs text-muted-foreground">
                Search
              </Label>
              <div className="relative mt-1">
                <Search className="absolute left-2 top-1/2 -translate-y-1/2 w-4 h-4 text-muted-foreground" />
                <Input
                  id="search"
                  placeholder="Search log messages..."
                  value={searchQuery}
                  onChange={(e) => setSearchQuery(e.target.value)}
                  className="pl-8 text-sm h-8 w-full min-w-[10ch]"
                />
              </div>
            </div>
          </div>

          <div className="mt-6 flex flex-wrap items-center gap-3 border-t border-border/50 pt-4">
            <div className="flex items-center gap-2">
              <Checkbox
                id="live-mode"
                checked={isLiveMode}
                onCheckedChange={(checked) => setIsLiveMode(checked as boolean)}
              />
              <Label htmlFor="live-mode" className="text-xs cursor-pointer flex items-center gap-1">
                <Activity className="w-3 h-3" />
                Live Mode
              </Label>
            </div>
            <p className="text-xs text-muted-foreground">
              {selectedLogs.length > 0 && `${selectedLogs.length} log(s) selected`}
            </p>

            <div className="ml-auto flex flex-wrap gap-2">
              <Button
                size="sm"
                variant="outline"
                className="text-xs h-7"
                onClick={handleCreateNotification}
                disabled={selectedLogs.length === 0}
              >
                <Bell className="w-3 h-3 mr-1" />
                Create Notification
              </Button>
              <Button
                size="sm"
                variant="outline"
                className="text-xs h-7"
                onClick={handleExportLogs}
                disabled={selectedLogs.length === 0}
              >
                <Mail className="w-3 h-3 mr-1" />
                Export & Email
              </Button>
            </div>
          </div>
        </CardContent>
      </Card>

      {/* Raw Log Viewer */}
      <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
        <CardHeader>
          <div className="flex flex-col sm:flex-row sm:items-center sm:justify-between gap-2">
            <CardTitle className="text-foreground/90 flex items-center gap-2">
              <FileText className="w-5 h-5" />
              Raw Log Viewer
            </CardTitle>
            {isLogViewerActive && (
              <Badge variant="outline" className="text-xs w-max">
                {selectedLogLabel}
              </Badge>
            )}
          </div>
          <p className="text-xs text-muted-foreground">
            Displays log content from the selected HiveFS daemon using <code>api/v1/audit/logs</code>.
          </p>
        </CardHeader>
        <CardContent className="space-y-2">
          {logError && (
            <div className="rounded border border-destructive/40 bg-destructive/10 px-3 py-2 text-xs text-destructive">
              {logError}
            </div>
          )}

          {!isLogViewerActive && (
            <p className="text-xs text-muted-foreground">
              Choose a log source above to fetch and display raw log output, or keep <strong>All Logs</strong> to
              preview every file concatenated together.
            </p>
          )}

          {isLogViewerActive && (
            <div className="rounded border border-border bg-muted/20 p-3">
              {logLoading && (
                <p className="text-xs text-muted-foreground mb-2">Loading log data…</p>
              )}
              <pre className="max-h-80 overflow-auto whitespace-pre-wrap text-xs font-mono text-foreground">
                {logViewerText || 'No log data available.'}
              </pre>
              {logsTruncated && (
                <p className="text-[11px] text-amber-600 mt-2">
                  Output truncated to 1MB per file for performance. Download full logs from the CLI if needed.
                </p>
              )}
            </div>
          )}
        </CardContent>
      </Card>


      {/* Export Dialog */}
      {showExportDialog && (
        <div className="fixed inset-0 bg-black/50 flex items-center justify-center z-50">
          <Card className="w-[600px] shadow-2xl">
            <CardHeader>
              <div className="flex items-center justify-between">
                <CardTitle className="text-foreground/90 flex items-center gap-2">
                  <Package className="w-5 h-5" />
                  Export & Email Logs
                </CardTitle>
                <Button
                  size="sm"
                  variant="ghost"
                  className="h-6 w-6 p-0"
                  onClick={() => setShowExportDialog(false)}
                >
                  <X className="w-4 h-4" />
                </Button>
              </div>
            </CardHeader>
            <CardContent className="space-y-4">
              <div className="bg-muted/30 rounded p-3 border border-border">
                <p className="text-sm font-medium mb-1">
                  Exporting {selectedLogs.length} log {selectedLogs.length === 1 ? 'entry' : 'entries'}
                </p>
                <p className="text-xs text-muted-foreground">
                  Logs will be packaged as a ZIP file and emailed to selected recipients
                </p>
              </div>

              <Separator />

              <div>
                <Label className="text-sm mb-2 block">Select Recipients</Label>
                <div className="space-y-2">
                  {users.map((user) => (
                    <div
                      key={user.id}
                      className="flex items-center space-x-2 bg-muted/20 rounded p-2 border border-border"
                    >
                      <Checkbox
                        id={`recipient-${user.id}`}
                        checked={exportRecipients.includes(user.id)}
                        onCheckedChange={() => toggleRecipient(user.id)}
                      />
                      <Label
                        htmlFor={`recipient-${user.id}`}
                        className="text-sm cursor-pointer flex-1"
                      >
                        <div className="flex items-center justify-between">
                          <div>
                            <p className="font-medium">{user.name}</p>
                            <p className="text-xs text-muted-foreground">{user.email}</p>
                          </div>
                          <Badge variant="outline" className="text-xs">
                            {user.role}
                          </Badge>
                        </div>
                      </Label>
                    </div>
                  ))}

                  <div className="flex items-center space-x-2 bg-blue-500/10 rounded p-2 border border-blue-500/30">
                    <Checkbox
                      id="recipient-support"
                      checked={exportIncludeSupport}
                      onCheckedChange={(checked) => setExportIncludeSupport(checked as boolean)}
                    />
                    <Label
                      htmlFor="recipient-support"
                      className="text-sm cursor-pointer flex-1"
                    >
                      <div className="flex items-center justify-between">
                        <div>
                          <p className="font-medium">HiveFS Support</p>
                          <p className="text-xs text-muted-foreground">support@hivefs.com</p>
                        </div>
                        <Badge variant="outline" className="text-xs border-blue-500 text-blue-500">
                          Support
                        </Badge>
                      </div>
                    </Label>
                  </div>
                </div>
              </div>

              <p
                className="text-muted-foreground opacity-60 pt-2 border-t border-border"
                style={{ fontSize: '0.7rem', lineHeight: '1.3' }}
              >
                For security, logs can only be emailed to users in your organization or to HiveFS support.
                External email addresses are not permitted.
              </p>

              <div className="flex gap-2 justify-end">
                <Button variant="outline" onClick={() => setShowExportDialog(false)}>
                  Cancel
                </Button>
                <Button
                  onClick={handleSendExport}
                  disabled={exportRecipients.length === 0 && !exportIncludeSupport}
                >
                  <Send className="w-4 h-4 mr-2" />
                  Send Logs
                </Button>
              </div>
            </CardContent>
          </Card>
        </div>
      )}

      {/* Notification Dialog */}
      {showNotificationDialog && (
        <div className="fixed inset-0 bg-black/50 flex items-center justify-center z-50">
          <Card className="w-[700px] max-h-[80vh] overflow-auto shadow-2xl">
            <CardHeader>
              <div className="flex items-center justify-between">
                <CardTitle className="text-foreground/90 flex items-center gap-2">
                  <Bell className="w-5 h-5" />
                  Create Notification from Logs
                </CardTitle>
                <Button
                  size="sm"
                  variant="ghost"
                  className="h-6 w-6 p-0"
                  onClick={() => setShowNotificationDialog(false)}
                >
                  <X className="w-4 h-4" />
                </Button>
              </div>
            </CardHeader>
            <CardContent className="space-y-4">
              <div className="bg-muted/30 rounded p-3 border border-border">
                <p className="text-sm font-medium mb-1">
                  Creating notification for {selectedLogs.length} log {selectedLogs.length === 1 ? 'entry' : 'entries'}
                </p>
              </div>

              {/* Selected Logs Preview */}
              <div>
                <Label className="text-sm mb-2 block">Selected Logs</Label>
                <div className="bg-muted/20 rounded p-3 border border-border max-h-[200px] overflow-y-auto space-y-2">
                  {getSelectedLogsData().map((log) => (
                    <div key={log.id} className="text-xs bg-background rounded p-2 border border-border">
                      <div className="flex items-center gap-2 mb-1">
                        {getLevelBadge(log.level)}
                        <Badge variant="outline" className="text-xs">{log.source}</Badge>
                        <span className="text-muted-foreground font-mono">
                          {formatTimestamp(log.timestamp)}
                        </span>
                      </div>
                      <p className="text-foreground/90">{log.message}</p>
                    </div>
                  ))}
                </div>
              </div>

              {/* Notification Configuration */}
              <div className="space-y-3">
                <div>
                  <Label htmlFor="notif-title" className="text-sm">
                    Alert Title
                  </Label>
                  <Input
                    id="notif-title"
                    placeholder="e.g., Critical Storage Issues Detected"
                    className="mt-1.5 text-sm"
                    defaultValue={
                      selectedLogs.length === 1
                        ? getSelectedLogsData()[0].message
                        : `${selectedLogs.length} log entries requiring attention`
                    }
                  />
                </div>

                <div>
                  <Label htmlFor="notif-severity" className="text-sm">
                    Severity
                  </Label>
                  <Select defaultValue={getSelectedLogsData()[0]?.level.toLowerCase() || 'medium'}>
                    <SelectTrigger id="notif-severity" className="mt-1.5 text-sm">
                      <SelectValue />
                    </SelectTrigger>
                    <SelectContent>
                      <SelectItem value="critical">Critical</SelectItem>
                      <SelectItem value="high">High</SelectItem>
                      <SelectItem value="medium">Medium</SelectItem>
                      <SelectItem value="low">Low</SelectItem>
                    </SelectContent>
                  </Select>
                </div>

                <div>
                  <Label htmlFor="notif-message" className="text-sm">
                    Message
                  </Label>
                  <textarea
                    id="notif-message"
                    className="w-full mt-1.5 text-sm p-2 border border-border rounded bg-background min-h-[100px]"
                    placeholder="Notification message..."
                    defaultValue={getSelectedLogsData().map((log) => 
                      `[${log.level}] ${log.source} (${log.node}): ${log.message}`
                    ).join('\n\n')}
                  />
                </div>
              </div>

              <p
                className="text-muted-foreground opacity-60 pt-2 border-t border-border"
                style={{ fontSize: '0.7rem', lineHeight: '1.3' }}
              >
                This will create a new notification alert based on the selected log entries. You can
                configure notification endpoints and delivery methods on the next screen.
              </p>

              <div className="flex gap-2 justify-end">
                <Button variant="outline" onClick={() => setShowNotificationDialog(false)}>
                  Cancel
                </Button>
                <Button onClick={() => {
                  console.log('Creating notification from logs:', selectedLogs);
                  setShowNotificationDialog(false);
                }}>
                  <Bell className="w-4 h-4 mr-2" />
                  Create Notification
                </Button>
              </div>
            </CardContent>
          </Card>
        </div>
      )}
    </div>
  );
}
