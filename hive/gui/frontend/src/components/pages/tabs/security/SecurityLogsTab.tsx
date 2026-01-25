import { useState } from 'react';
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
  Download,
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

interface User {
  id: string;
  name: string;
  email: string;
  role: string;
}

export function SecurityLogsTab() {
  const [filterLevel, setFilterLevel] = useState<string>('all');
  const [filterSource, setFilterSource] = useState<string>('all');
  const [filterNode, setFilterNode] = useState<string>('all');
  const [searchQuery, setSearchQuery] = useState('');
  const [expandedLogs, setExpandedLogs] = useState<string[]>([]);
  const [selectedLogs, setSelectedLogs] = useState<string[]>([]);
  const [isLiveMode, setIsLiveMode] = useState(false);
  const [showExportDialog, setShowExportDialog] = useState(false);
  const [showNotificationDialog, setShowNotificationDialog] = useState(false);
  const [exportRecipients, setExportRecipients] = useState<string[]>([]);
  const [exportIncludeSupport, setExportIncludeSupport] = useState(false);

  // Mock users for email recipients
  const users: User[] = [
    { id: '1', name: 'John Smith', email: 'john.smith@hivefs.com', role: 'Administrator' },
    { id: '2', name: 'Sarah Johnson', email: 'sarah.johnson@hivefs.com', role: 'Administrator' },
    { id: '3', name: 'Michael Chen', email: 'michael.chen@hivefs.com', role: 'Operator' },
    { id: '4', name: 'Emily Davis', email: 'emily.davis@hivefs.com', role: 'Viewer' },
  ];

  // Mock log entries
  const [logEntries] = useState<LogEntry[]>([
    {
      id: '1',
      timestamp: '2026-01-25T14:35:22.123Z',
      level: 'CRITICAL',
      source: 'storage-daemon',
      node: 'node-01',
      message: 'Critical: Storage pool capacity exceeded 95% threshold',
      details: 'Storage pool "primary-pool" is at 96.2% capacity. Immediate action required.',
      metadata: { poolId: 'primary-pool', capacity: 96.2, threshold: 95 },
    },
    {
      id: '2',
      timestamp: '2026-01-25T14:34:15.456Z',
      level: 'ERROR',
      source: 'replication-daemon',
      node: 'node-03',
      message: 'Replication failed for chunk 0x4f2a8b',
      details: 'Failed to replicate chunk to node-05 after 3 retry attempts',
      stackTrace: 'ReplicationError: Connection timeout\n  at Replicator.sync (replicator.go:145)\n  at ChunkManager.replicate (chunk.go:89)',
      metadata: { chunkId: '0x4f2a8b', targetNode: 'node-05', retries: 3 },
    },
    {
      id: '3',
      timestamp: '2026-01-25T14:33:00.789Z',
      level: 'WARN',
      source: 'cluster-manager',
      node: 'node-01',
      message: 'Node heartbeat delayed from node-04',
      details: 'Heartbeat from node-04 delayed by 2.3 seconds. Possible network issue.',
      metadata: { sourceNode: 'node-04', delay: 2.3 },
    },
    {
      id: '4',
      timestamp: '2026-01-25T14:32:45.234Z',
      level: 'INFO',
      source: 'api-server',
      node: 'node-02',
      message: 'API request completed successfully',
      details: 'GET /api/v1/cluster/status completed in 45ms',
      metadata: { method: 'GET', endpoint: '/api/v1/cluster/status', duration: 45, statusCode: 200 },
    },
    {
      id: '5',
      timestamp: '2026-01-25T14:32:30.567Z',
      level: 'ERROR',
      source: 'metadata-service',
      node: 'node-02',
      message: 'Database connection pool exhausted',
      details: 'All connections in pool are in use. Consider increasing max_connections.',
      metadata: { poolSize: 50, activeConnections: 50 },
    },
    {
      id: '6',
      timestamp: '2026-01-25T14:31:20.890Z',
      level: 'WARN',
      source: 'storage-daemon',
      node: 'node-05',
      message: 'High disk I/O wait detected',
      details: 'Disk I/O wait time exceeded 100ms on /dev/sda1',
      metadata: { device: '/dev/sda1', ioWait: 125 },
    },
    {
      id: '7',
      timestamp: '2026-01-25T14:30:15.123Z',
      level: 'INFO',
      source: 'auth-service',
      node: 'node-01',
      message: 'User authentication successful',
      details: 'User john.smith@hivefs.com authenticated via API key',
      metadata: { user: 'john.smith@hivefs.com', method: 'api_key' },
    },
    {
      id: '8',
      timestamp: '2026-01-25T14:29:45.456Z',
      level: 'DEBUG',
      source: 'cache-service',
      node: 'node-03',
      message: 'Cache miss for key cluster.nodes.status',
      details: 'Cache lookup failed, fetching from source',
      metadata: { cacheKey: 'cluster.nodes.status', ttl: 60 },
    },
    {
      id: '9',
      timestamp: '2026-01-25T14:28:30.789Z',
      level: 'ERROR',
      source: 'cluster-manager',
      node: 'node-01',
      message: 'Failed to elect new leader',
      details: 'Consensus not reached after 5 voting rounds',
      stackTrace: 'ConsensusError: Quorum not met\n  at Leader.elect (leader.go:234)\n  at Cluster.electLeader (cluster.go:567)',
      metadata: { votingRounds: 5, nodesVoted: 3, nodesRequired: 4 },
    },
    {
      id: '10',
      timestamp: '2026-01-25T14:27:00.234Z',
      level: 'CRITICAL',
      source: 'replication-daemon',
      node: 'node-04',
      message: 'Data inconsistency detected',
      details: 'Checksum mismatch on chunk 0x8d3f1a between replicas',
      metadata: { chunkId: '0x8d3f1a', expectedChecksum: 'a8f2e4', actualChecksum: 'b9c3f5' },
    },
    {
      id: '11',
      timestamp: '2026-01-25T14:26:15.567Z',
      level: 'INFO',
      source: 'scheduler',
      node: 'node-02',
      message: 'Scheduled maintenance task completed',
      details: 'Log rotation completed successfully',
      metadata: { task: 'log-rotation', duration: 1230 },
    },
    {
      id: '12',
      timestamp: '2026-01-25T14:25:00.890Z',
      level: 'WARN',
      source: 'api-server',
      node: 'node-01',
      message: 'Rate limit approaching for API key',
      details: 'API key hfs_prod_a1b2c3 has used 950/1000 requests this hour',
      metadata: { apiKey: 'hfs_prod_a1b2c3', usage: 950, limit: 1000 },
    },
  ]);

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

  // Get unique sources and nodes for filters
  const uniqueSources = Array.from(new Set(logEntries.map((e) => e.source))).sort();
  const uniqueNodes = Array.from(new Set(logEntries.map((e) => e.node))).sort();

  // Filter logs
  const filteredLogs = logEntries.filter((log) => {
    if (filterLevel !== 'all' && log.level !== filterLevel) return false;
    if (filterSource !== 'all' && log.source !== filterSource) return false;
    if (filterNode !== 'all' && log.node !== filterNode) return false;
    if (searchQuery && !log.message.toLowerCase().includes(searchQuery.toLowerCase()) &&
        !log.details?.toLowerCase().includes(searchQuery.toLowerCase())) {
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
      <div className="grid grid-cols-4 gap-4">
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
          <div className="grid grid-cols-5 gap-4 mb-4">
            <div>
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

            <div>
              <Label htmlFor="filter-source" className="text-xs text-muted-foreground">
                Source
              </Label>
              <Select value={filterSource} onValueChange={setFilterSource}>
                <SelectTrigger id="filter-source" className="mt-1 text-sm h-8">
                  <SelectValue />
                </SelectTrigger>
                <SelectContent>
                  <SelectItem value="all">All Sources</SelectItem>
                  {uniqueSources.map((source) => (
                    <SelectItem key={source} value={source}>
                      {source}
                    </SelectItem>
                  ))}
                </SelectContent>
              </Select>
            </div>

            <div>
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

            <div className="col-span-2">
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
                  className="pl-8 text-sm h-8"
                />
              </div>
            </div>
          </div>

          <div className="flex items-center justify-between">
            <div className="flex items-center gap-3">
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
            </div>

            <div className="flex gap-2">
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

      {/* Log Entries */}
      <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
        <CardHeader>
          <div className="flex items-center justify-between">
            <CardTitle className="text-foreground/90 flex items-center gap-2">
              <FileText className="w-5 h-5" />
              Log Entries ({filteredLogs.length})
            </CardTitle>
            <Button
              size="sm"
              variant="ghost"
              className="text-xs h-7"
              onClick={toggleSelectAll}
            >
              {selectedLogs.length === filteredLogs.length ? (
                <>
                  <Square className="w-3 h-3 mr-1" />
                  Deselect All
                </>
              ) : (
                <>
                  <CheckSquare className="w-3 h-3 mr-1" />
                  Select All
                </>
              )}
            </Button>
          </div>
        </CardHeader>
        <CardContent>
          <div className="space-y-2">
            {filteredLogs.map((log) => {
              const isExpanded = expandedLogs.includes(log.id);
              const isSelected = selectedLogs.includes(log.id);

              return (
                <div
                  key={log.id}
                  className={`border rounded-lg transition-all ${
                    isSelected ? 'border-primary bg-primary/5' : 'border-border bg-background'
                  }`}
                >
                  {/* Summary Row */}
                  <div className="p-3 flex items-center gap-3">
                    <Checkbox
                      checked={isSelected}
                      onCheckedChange={() => toggleSelected(log.id)}
                    />

                    <Button
                      size="sm"
                      variant="ghost"
                      className="h-6 w-6 p-0"
                      onClick={() => toggleExpanded(log.id)}
                    >
                      {isExpanded ? (
                        <ChevronDown className="w-4 h-4" />
                      ) : (
                        <ChevronRight className="w-4 h-4" />
                      )}
                    </Button>

                    <div className="flex-1 grid grid-cols-12 gap-3 items-center">
                      <div className="col-span-2 text-xs font-mono text-muted-foreground">
                        {formatTimestamp(log.timestamp)}
                      </div>

                      <div className="col-span-1">
                        {getLevelBadge(log.level)}
                      </div>

                      <div className="col-span-2">
                        <Badge variant="outline" className="text-xs">
                          {log.source}
                        </Badge>
                      </div>

                      <div className="col-span-1">
                        <Badge variant="secondary" className="text-xs">
                          {log.node}
                        </Badge>
                      </div>

                      <div className="col-span-6">
                        <p className="text-sm text-foreground/90 truncate">{log.message}</p>
                      </div>
                    </div>
                  </div>

                  {/* Expanded Details */}
                  {isExpanded && (
                    <>
                      <Separator />
                      <div className="p-4 space-y-3 bg-muted/20">
                        {/* Full Timestamp */}
                        <div>
                          <p className="text-xs text-muted-foreground mb-1">Full Timestamp</p>
                          <p className="text-sm font-mono">{formatFullTimestamp(log.timestamp)}</p>
                        </div>

                        {/* Message */}
                        <div>
                          <p className="text-xs text-muted-foreground mb-1">Message</p>
                          <p className="text-sm bg-muted/50 rounded p-2 border border-border">
                            {log.message}
                          </p>
                        </div>

                        {/* Details */}
                        {log.details && (
                          <div>
                            <p className="text-xs text-muted-foreground mb-1">Details</p>
                            <p className="text-sm bg-muted/50 rounded p-2 border border-border">
                              {log.details}
                            </p>
                          </div>
                        )}

                        {/* Stack Trace */}
                        {log.stackTrace && (
                          <div>
                            <p className="text-xs text-muted-foreground mb-1">Stack Trace</p>
                            <pre className="text-xs bg-black/80 text-green-400 rounded p-3 border border-border overflow-x-auto font-mono">
                              {log.stackTrace}
                            </pre>
                          </div>
                        )}

                        {/* Metadata */}
                        {log.metadata && Object.keys(log.metadata).length > 0 && (
                          <div>
                            <p className="text-xs text-muted-foreground mb-2">Metadata</p>
                            <div className="bg-muted/50 rounded p-3 border border-border">
                              <div className="grid grid-cols-2 gap-2">
                                {Object.entries(log.metadata).map(([key, value]) => (
                                  <div key={key} className="text-xs">
                                    <span className="text-muted-foreground">{key}:</span>{' '}
                                    <span className="font-mono">{JSON.stringify(value)}</span>
                                  </div>
                                ))}
                              </div>
                            </div>
                          </div>
                        )}
                      </div>
                    </>
                  )}
                </div>
              );
            })}

            {filteredLogs.length === 0 && (
              <div className="text-center py-12 text-muted-foreground">
                <FileText className="w-12 h-12 mx-auto mb-3 opacity-50" />
                <p className="text-sm">No log entries found</p>
                <p className="text-xs mt-1">Try adjusting your filters</p>
              </div>
            )}
          </div>
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
