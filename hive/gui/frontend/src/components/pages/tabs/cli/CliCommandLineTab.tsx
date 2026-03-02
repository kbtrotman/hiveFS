import { useMemo, useState } from 'react';
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from '../../../ui/card';
import { Button } from '../../../ui/button';
import { Input } from '../../../ui/input';
import { Badge } from '../../../ui/badge';
import { Terminal, Copy, Download, Key } from 'lucide-react';
import {
  aggregateStatsByNode,
  formatRelativeTime,
  formatTimestamp,
  safeNumber,
  sumStatField,
  useDiskStats,
} from '../../../useDiskStats';
import { useApiResource } from '../../../useApiResource';
import { AddClientDialog } from '../../../AddClientDialog';

interface StorageNode {
  node_id?: number | null;
  node_name?: string | null;
  node_address?: string | null;
  node_guard_port?: number | null;
  node_data_port?: number | null;
  fenced?: boolean | number | null;
}

interface AuditEntry {
  audit_entry_id?: number | string;
  change_summary?: string | null;
  description?: string | null;
  page_name?: string | null;
  username?: string | null;
  user_id?: number | string | null;
  created_at?: string | null;
}

interface MfaRecord {
  mfa_enabled?: boolean;
  user?: number | string;
  method?: string | null;
  created_at?: string | null;
}

interface ConnectionMethod {
  id: string;
  name: string;
  command: string;
  port: string;
  status: 'active' | 'maintenance';
}

interface CommandEntry {
  id: string;
  command: string;
  time: string;
  user: string;
}

const formatNumber = (value: number) =>
  Number.isFinite(value) ? new Intl.NumberFormat().format(value) : '—';

export function CliCommandLineTab() {
  const { stats, isLoading: statsLoading, error: statsError } = useDiskStats();
  const aggregated = useMemo(() => aggregateStatsByNode(stats), [stats]);
  const {
    data: nodes,
    isLoading: nodesLoading,
    error: nodesError,
  } = useApiResource<StorageNode[]>('nodes', {
    initialData: [],
    transform: (payload) => (Array.isArray(payload) ? payload : []),
  });
  const {
    data: auditEntries,
    isLoading: auditLoading,
    error: auditError,
  } = useApiResource<AuditEntry[]>('audit?limit=25', {
    initialData: [],
    transform: (payload) => (Array.isArray(payload) ? payload : []),
  });
  const {
    data: mfaRecords,
    isLoading: mfaLoading,
    error: mfaError,
  } = useApiResource<MfaRecord[]>('mfa', {
    initialData: [],
    transform: (payload) => (Array.isArray(payload) ? payload : []),
  });

  const [isAddClientDialogOpen, setIsAddClientDialogOpen] = useState(false);

  const activeSessions = useMemo(
    () => sumStatField(aggregated, 'clients'),
    [aggregated],
  );
  const connectionMethods: ConnectionMethod[] = useMemo(() => {
    if (!nodes.length) {
      return [
        {
          id: 'ssh',
          name: 'SSH',
          command: 'ssh admin@localhost',
          port: '22',
          status: 'active',
        },
      ];
    }
    return nodes.slice(0, 4).map((node) => {
      const name = node.node_name ?? `node-${node.node_id ?? 'unknown'}`;
      return {
        id: String(node.node_id ?? name),
        name,
        command: `ssh admin@${node.node_address ?? name}`,
        port: String(node.node_guard_port ?? 22),
        status: node.fenced ? 'maintenance' : 'active',
      };
    });
  }, [nodes]);

  const recentCommands: CommandEntry[] = useMemo(() => {
    if (!auditEntries.length) {
      return [];
    }
    return auditEntries.map((entry) => ({
      id: String(entry.audit_entry_id ?? entry.created_at ?? Math.random()),
      command:
        entry.change_summary ??
        entry.description ??
        entry.page_name ??
        'Audit event',
      time: formatRelativeTime(entry.created_at),
      user: entry.username ?? `User ${entry.user_id ?? 'n/a'}`,
    }));
  }, [auditEntries]);

  const apiKeyCount = useMemo(
    () =>
      mfaRecords.reduce(
        (count, record) => count + (record.mfa_enabled ? 1 : 0),
        0,
      ),
    [mfaRecords],
  );

  const combinedError = statsError ?? nodesError ?? auditError ?? mfaError;

  return (
    <div className="space-y-6">
      <div className="flex justify-between items-center">
        <div>
          <h2>Command-line Access</h2>
          <p className="text-muted-foreground">
            Connect to HiveFS via CLI and manage access keys
          </p>
        </div>
        <Button type="button" onClick={() => setIsAddClientDialogOpen(true)}>
          <Key className="w-4 h-4 mr-2" />
          Generate API Token
        </Button>
      </div>

      <div className="grid grid-cols-1 md:grid-cols-3 gap-4">
        <MetricCard
          label="Active Sessions"
          value={
            statsLoading ? '—' : formatNumber(safeNumber(activeSessions, 0))
          }
        />
        <MetricCard
          label="Provisioned API Tokens"
          value={mfaLoading ? '—' : formatNumber(apiKeyCount)}
        />
        <MetricCard
          label="Last Command"
          value={
            auditLoading || !recentCommands.length
              ? '—'
              : recentCommands[0].command
          }
        />
      </div>

      {combinedError && (
        <div className="rounded-md border border-destructive/40 bg-destructive/10 px-4 py-3 text-sm text-destructive">
          {combinedError}
        </div>
      )}

      <Card>
        <CardHeader>
          <CardTitle>Connection Methods</CardTitle>
          <CardDescription>Endpoints discovered from cluster nodes</CardDescription>
        </CardHeader>
        <CardContent className="space-y-4">
          {nodesLoading && (
            <p className="text-sm text-muted-foreground">Loading nodes…</p>
          )}
          {connectionMethods.map((method) => (
            <div
              key={method.id}
              className="flex items-center justify-between p-4 border border-border rounded-lg"
            >
              <div className="flex-1">
                <div className="flex items-center gap-2 mb-2">
                  <p>{method.name}</p>
                  <Badge
                    variant={method.status === 'active' ? 'default' : 'secondary'}
                  >
                    {method.status}
                  </Badge>
                </div>
                <code className="text-sm bg-muted px-2 py-1 rounded">
                  {method.command}
                </code>
                <p className="text-xs text-muted-foreground mt-2">
                  Guard Port: {method.port}
                </p>
              </div>
              <Button variant="ghost" size="icon" onClick={() => navigator.clipboard.writeText(method.command)}>
                <Copy className="w-4 h-4" />
              </Button>
            </div>
          ))}
        </CardContent>
      </Card>

      <Card>
        <CardHeader>
          <CardTitle>HiveFS CLI Installation</CardTitle>
          <CardDescription>Install the command-line interface tool</CardDescription>
        </CardHeader>
        <CardContent className="space-y-4">
          <div>
            <p className="text-sm mb-2">macOS / Linux</p>
            <div className="flex items-center gap-2">
              <code className="flex-1 text-sm bg-muted px-3 py-2 rounded">
                curl -fsSL https://get.hivefs.io | bash
              </code>
              <Button
                variant="ghost"
                size="icon"
                onClick={() =>
                  navigator.clipboard.writeText(
                    'curl -fsSL https://get.hivefs.io | bash',
                  )
                }
              >
                <Copy className="w-4 h-4" />
              </Button>
            </div>
          </div>
          <div>
            <p className="text-sm mb-2">Windows (PowerShell)</p>
            <div className="flex items-center gap-2">
              <code className="flex-1 text-sm bg-muted px-3 py-2 rounded">
                iwr https://get.hivefs.io/install.ps1 -useb | iex
              </code>
              <Button
                variant="ghost"
                size="icon"
                onClick={() =>
                  navigator.clipboard.writeText(
                    'iwr https://get.hivefs.io/install.ps1 -useb | iex',
                  )
                }
              >
                <Copy className="w-4 h-4" />
              </Button>
            </div>
          </div>
          <div>
            <p className="text-sm mb-2">Docker</p>
            <div className="flex items-center gap-2">
              <code className="flex-1 text-sm bg-muted px-3 py-2 rounded">
                docker pull hivefs/cli:latest
              </code>
              <Button
                variant="ghost"
                size="icon"
                onClick={() =>
                  navigator.clipboard.writeText('docker pull hivefs/cli:latest')
                }
              >
                <Copy className="w-4 h-4" />
              </Button>
            </div>
          </div>
          <Button variant="outline">
            <Download className="w-4 h-4 mr-2" />
            Download Manual Installation Package
          </Button>
        </CardContent>
      </Card>

      <Card>
        <CardHeader>
          <CardTitle>Recent Command History</CardTitle>
          <CardDescription>Latest configuration events from the audit log</CardDescription>
        </CardHeader>
        <CardContent>
          <div className="space-y-3">
            {auditLoading && (
              <p className="text-sm text-muted-foreground">Loading history…</p>
            )}
            {!auditLoading && recentCommands.length === 0 && (
              <p className="text-sm text-muted-foreground">
                No recent audit entries were returned.
              </p>
            )}
            {recentCommands.map((cmd) => (
              <div
                key={cmd.id}
                className="flex items-center justify-between py-2 border-b border-border last:border-0"
              >
                <div className="flex-1">
                  <code className="text-sm">{cmd.command}</code>
                  <p className="text-xs text-muted-foreground mt-1">
                    {cmd.time} • {cmd.user}
                  </p>
                </div>
                <Badge variant="default">logged</Badge>
              </div>
            ))}
          </div>
        </CardContent>
      </Card>

      <AddClientDialog
        open={isAddClientDialogOpen}
        onClose={() => setIsAddClientDialogOpen(false)}
      />
    </div>
  );
}

interface MetricCardProps {
  label: string;
  value: string;
}

function MetricCard({ label, value }: MetricCardProps) {
  return (
    <Card>
      <CardContent className="pt-6">
        <p className="text-sm text-muted-foreground">{label}</p>
        <p className="mt-2">{value}</p>
      </CardContent>
    </Card>
  );
}
