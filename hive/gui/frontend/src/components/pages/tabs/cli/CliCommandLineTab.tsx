import { Card, CardContent, CardDescription, CardHeader, CardTitle } from '../../../ui/card';
import { Button } from '../../../ui/button';
import { Input } from '../../../ui/input';
import { Badge } from '../../../ui/badge';
import { Terminal, Copy, Download, Key } from 'lucide-react';

const connectionMethods = [
  { name: 'SSH', command: 'ssh admin@hive-master-01.local', port: '22', status: 'active' },
  { name: 'HiveFS CLI', command: 'hivefs connect --host hive-master-01.local', port: '8022', status: 'active' },
  { name: 'REST API', command: 'curl https://api.hivefs.local/v1', port: '443', status: 'active' },
];

const recentCommands = [
  { command: 'hivefs ls /data/production', time: '2 min ago', user: 'admin', result: 'success' },
  { command: 'hivefs tag add production /data/release/*', time: '15 min ago', user: 'admin', result: 'success' },
  { command: 'hivefs stats --verbose', time: '32 min ago', user: 'operator1', result: 'success' },
  { command: 'hivefs backup create --full', time: '1 hour ago', user: 'backup-service', result: 'success' },
  { command: 'hivefs user list --all', time: '2 hours ago', user: 'admin', result: 'success' },
];

export function CliCommandLineTab() {
  return (
    <div className="space-y-6">
      <div className="flex justify-between items-center">
        <div>
          <h2>Command-line Access</h2>
          <p className="text-muted-foreground">Connect to HiveFS via CLI and manage access keys</p>
        </div>
        <Button>
          <Key className="w-4 h-4 mr-2" />
          Generate API Key
        </Button>
      </div>

      <div className="grid grid-cols-1 md:grid-cols-3 gap-4">
        <Card>
          <CardContent className="pt-6">
            <div className="flex items-center justify-between">
              <div>
                <p className="text-sm text-muted-foreground">Active Sessions</p>
                <p className="mt-2">8</p>
              </div>
              <Terminal className="w-5 h-5 text-blue-500" />
            </div>
          </CardContent>
        </Card>
        <Card>
          <CardContent className="pt-6">
            <div className="flex items-center justify-between">
              <div>
                <p className="text-sm text-muted-foreground">API Keys</p>
                <p className="mt-2">5</p>
              </div>
              <Key className="w-5 h-5 text-green-500" />
            </div>
          </CardContent>
        </Card>
        <Card>
          <CardContent className="pt-6">
            <p className="text-sm text-muted-foreground">Last Command</p>
            <p className="mt-2 text-sm truncate">hivefs ls /data/production</p>
          </CardContent>
        </Card>
      </div>

      <Card>
        <CardHeader>
          <CardTitle>Connection Methods</CardTitle>
          <CardDescription>Available CLI and API access methods</CardDescription>
        </CardHeader>
        <CardContent className="space-y-4">
          {connectionMethods.map((method, index) => (
            <div key={index} className="flex items-center justify-between p-4 border border-border rounded-lg">
              <div className="flex-1">
                <div className="flex items-center gap-2 mb-2">
                  <p>{method.name}</p>
                  <Badge variant={method.status === 'active' ? 'default' : 'secondary'}>
                    {method.status}
                  </Badge>
                </div>
                <code className="text-sm bg-muted px-2 py-1 rounded">
                  {method.command}
                </code>
                <p className="text-xs text-muted-foreground mt-2">Port: {method.port}</p>
              </div>
              <Button variant="ghost" size="icon">
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
              <Button variant="ghost" size="icon">
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
              <Button variant="ghost" size="icon">
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
              <Button variant="ghost" size="icon">
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
          <CardDescription>Commands executed in the last 24 hours</CardDescription>
        </CardHeader>
        <CardContent>
          <div className="space-y-3">
            {recentCommands.map((cmd, index) => (
              <div key={index} className="flex items-center justify-between py-2 border-b border-border last:border-0">
                <div className="flex-1">
                  <code className="text-sm">{cmd.command}</code>
                  <p className="text-xs text-muted-foreground mt-1">
                    {cmd.time} • {cmd.user}
                  </p>
                </div>
                <Badge variant={cmd.result === 'success' ? 'default' : 'destructive'}>
                  {cmd.result}
                </Badge>
              </div>
            ))}
          </div>
        </CardContent>
      </Card>
    </div>
  );
}
