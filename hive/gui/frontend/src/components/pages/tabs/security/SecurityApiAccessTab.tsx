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
  Key,
  Plus,
  Copy,
  Trash2,
  Eye,
  EyeOff,
  AlertTriangle,
  CheckCircle,
  XCircle,
  Clock,
  Activity,
  Shield,
  Calendar,
  RotateCw,
} from 'lucide-react';

interface ApiKey {
  id: string;
  name: string;
  description: string;
  key: string;
  prefix: string;
  createdAt: string;
  createdBy: string;
  expiresAt?: string;
  lastUsed?: string;
  status: 'active' | 'expired' | 'revoked';
  permissions: string[];
  rateLimit: number;
  usageCount: number;
}

export function SecurityApiAccessTab() {
  const [isCreatingKey, setIsCreatingKey] = useState(false);
  const [newKeyName, setNewKeyName] = useState('');
  const [newKeyDescription, setNewKeyDescription] = useState('');
  const [newKeyExpiration, setNewKeyExpiration] = useState('90');
  const [newKeyRateLimit, setNewKeyRateLimit] = useState('1000');
  const [selectedPermissions, setSelectedPermissions] = useState<string[]>([
    'read:cluster',
    'read:nodes',
  ]);
  const [revealedKeys, setRevealedKeys] = useState<string[]>([]);
  const [newlyCreatedKey, setNewlyCreatedKey] = useState<string | null>(null);

  // Available permissions
  const availablePermissions = [
    { id: 'read:cluster', label: 'Read Cluster Info', category: 'read' },
    { id: 'read:nodes', label: 'Read Node Info', category: 'read' },
    { id: 'read:storage', label: 'Read Storage Info', category: 'read' },
    { id: 'read:users', label: 'Read User Info', category: 'read' },
    { id: 'read:reports', label: 'Read Reports', category: 'read' },
    { id: 'write:cluster', label: 'Modify Cluster Settings', category: 'write' },
    { id: 'write:nodes', label: 'Manage Nodes', category: 'write' },
    { id: 'write:storage', label: 'Manage Storage', category: 'write' },
    { id: 'write:users', label: 'Manage Users', category: 'write' },
    { id: 'admin:full', label: 'Full Administrative Access', category: 'admin' },
  ];

  // Mock API keys data
  const [apiKeys, setApiKeys] = useState<ApiKey[]>([
    {
      id: '1',
      name: 'Production Monitoring',
      description: 'Used by monitoring system for cluster health checks',
      key: 'hfs_prod_a1b2c3d4e5f6g7h8i9j0k1l2m3n4o5p6',
      prefix: 'hfs_prod_a1b2c3',
      createdAt: '2026-01-15T10:00:00Z',
      createdBy: 'john.smith@hivefs.com',
      expiresAt: '2026-04-15T10:00:00Z',
      lastUsed: '2026-01-25T14:30:00Z',
      status: 'active',
      permissions: ['read:cluster', 'read:nodes', 'read:storage'],
      rateLimit: 1000,
      usageCount: 45230,
    },
    {
      id: '2',
      name: 'Automation Scripts',
      description: 'API key for automated deployment scripts',
      key: 'hfs_auto_z9y8x7w6v5u4t3s2r1q0p9o8n7m6l5k4',
      prefix: 'hfs_auto_z9y8x7',
      createdAt: '2026-01-10T08:00:00Z',
      createdBy: 'sarah.johnson@hivefs.com',
      expiresAt: '2026-07-10T08:00:00Z',
      lastUsed: '2026-01-24T16:45:00Z',
      status: 'active',
      permissions: ['read:cluster', 'write:cluster', 'write:nodes'],
      rateLimit: 500,
      usageCount: 12450,
    },
    {
      id: '3',
      name: 'Development Testing',
      description: 'Test key for development environment',
      key: 'hfs_dev_m5n6o7p8q9r0s1t2u3v4w5x6y7z8a9b0',
      prefix: 'hfs_dev_m5n6o7',
      createdAt: '2025-12-01T12:00:00Z',
      createdBy: 'michael.chen@hivefs.com',
      expiresAt: '2026-01-01T12:00:00Z',
      status: 'expired',
      permissions: ['read:cluster', 'read:nodes'],
      rateLimit: 100,
      usageCount: 8920,
    },
    {
      id: '4',
      name: 'Legacy Integration',
      description: 'Old integration that was replaced',
      key: 'hfs_legacy_c1d2e3f4g5h6i7j8k9l0m1n2o3p4q5r6',
      prefix: 'hfs_legacy_c1d2e3',
      createdAt: '2025-11-01T09:00:00Z',
      createdBy: 'john.smith@hivefs.com',
      status: 'revoked',
      permissions: ['read:cluster'],
      rateLimit: 200,
      usageCount: 3450,
    },
  ]);

  const togglePermission = (permissionId: string) => {
    setSelectedPermissions((prev) =>
      prev.includes(permissionId)
        ? prev.filter((p) => p !== permissionId)
        : [...prev, permissionId]
    );
  };

  const handleCreateKey = () => {
    const newKey = `hfs_${newKeyName.toLowerCase().replace(/\s+/g, '_')}_${Math.random()
      .toString(36)
      .substring(2, 15)}${Math.random().toString(36).substring(2, 15)}`;

    const expirationDate = new Date();
    expirationDate.setDate(expirationDate.getDate() + parseInt(newKeyExpiration));

    const apiKey: ApiKey = {
      id: Date.now().toString(),
      name: newKeyName,
      description: newKeyDescription,
      key: newKey,
      prefix: newKey.substring(0, 14),
      createdAt: new Date().toISOString(),
      createdBy: 'current.user@hivefs.com',
      expiresAt: newKeyExpiration !== 'never' ? expirationDate.toISOString() : undefined,
      status: 'active',
      permissions: selectedPermissions,
      rateLimit: parseInt(newKeyRateLimit),
      usageCount: 0,
    };

    setApiKeys([apiKey, ...apiKeys]);
    setNewlyCreatedKey(newKey);
    setNewKeyName('');
    setNewKeyDescription('');
    setSelectedPermissions(['read:cluster', 'read:nodes']);
    setIsCreatingKey(false);
  };

  const toggleRevealKey = (id: string) => {
    setRevealedKeys((prev) =>
      prev.includes(id) ? prev.filter((keyId) => keyId !== id) : [...prev, id]
    );
  };

  const copyToClipboard = (text: string) => {
    navigator.clipboard.writeText(text);
    console.log('Copied to clipboard:', text);
  };

  const revokeKey = (id: string) => {
    setApiKeys(
      apiKeys.map((key) => (key.id === id ? { ...key, status: 'revoked' as const } : key))
    );
  };

  const deleteKey = (id: string) => {
    setApiKeys(apiKeys.filter((key) => key.id !== id));
  };

  const getStatusBadge = (status: string) => {
    switch (status) {
      case 'active':
        return (
          <Badge variant="default" className="text-xs bg-green-500">
            <CheckCircle className="w-3 h-3 mr-1" />
            Active
          </Badge>
        );
      case 'expired':
        return (
          <Badge variant="outline" className="text-xs text-yellow-600 border-yellow-600">
            <Clock className="w-3 h-3 mr-1" />
            Expired
          </Badge>
        );
      case 'revoked':
        return (
          <Badge variant="outline" className="text-xs text-red-600 border-red-600">
            <XCircle className="w-3 h-3 mr-1" />
            Revoked
          </Badge>
        );
      default:
        return null;
    }
  };

  const formatTimestamp = (timestamp: string) => {
    const date = new Date(timestamp);
    return date.toLocaleDateString() + ' ' + date.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
  };

  const isExpiringSoon = (expiresAt?: string) => {
    if (!expiresAt) return false;
    const now = new Date();
    const expiry = new Date(expiresAt);
    const daysUntilExpiry = (expiry.getTime() - now.getTime()) / (1000 * 60 * 60 * 24);
    return daysUntilExpiry > 0 && daysUntilExpiry <= 30;
  };

  const activeKeys = apiKeys.filter((k) => k.status === 'active').length;
  const expiringKeys = apiKeys.filter((k) => k.status === 'active' && isExpiringSoon(k.expiresAt)).length;

  return (
    <div className="flex h-full w-full flex-col gap-6 overflow-auto bg-gradient-to-br from-background via-primary/5 to-background p-8">
      {/* Page Header */}
      <div>
        <h1 className="text-foreground/80">API Keys</h1>
        <p className="text-muted-foreground text-sm mt-1">
          Manage API keys for programmatic access to HiveFS RESTful API
        </p>
      </div>

      {/* Summary Cards */}
      <div className="grid grid-cols-3 gap-4">
        <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
          <CardContent className="pt-4">
            <div className="flex items-center justify-between">
              <div>
                <p className="text-xs text-muted-foreground">Active Keys</p>
                <p className="text-2xl font-semibold mt-1">{activeKeys}</p>
              </div>
              <Key className="w-8 h-8 text-green-500 opacity-50" />
            </div>
          </CardContent>
        </Card>

        <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
          <CardContent className="pt-4">
            <div className="flex items-center justify-between">
              <div>
                <p className="text-xs text-muted-foreground">Expiring Soon</p>
                <p className="text-2xl font-semibold mt-1 text-yellow-600">{expiringKeys}</p>
              </div>
              <AlertTriangle className="w-8 h-8 text-yellow-600 opacity-50" />
            </div>
          </CardContent>
        </Card>

        <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
          <CardContent className="pt-4">
            <div className="flex items-center justify-between">
              <div>
                <p className="text-xs text-muted-foreground">Total Keys</p>
                <p className="text-2xl font-semibold mt-1">{apiKeys.length}</p>
              </div>
              <Shield className="w-8 h-8 text-muted-foreground opacity-50" />
            </div>
          </CardContent>
        </Card>
      </div>

      {/* Newly Created Key Alert */}
      {newlyCreatedKey && (
        <Card className="border-green-400/50 bg-green-500/10 shadow-lg">
          <CardContent className="pt-4">
            <div className="flex items-start gap-3">
              <CheckCircle className="w-5 h-5 text-green-500 mt-0.5" />
              <div className="flex-1">
                <p className="text-sm font-medium text-green-700 dark:text-green-400 mb-2">
                  API Key Created Successfully
                </p>
                <p className="text-xs text-muted-foreground mb-3">
                  Copy this key now. For security reasons, it won't be shown again.
                </p>
                <div className="bg-background rounded border border-border p-3 font-mono text-sm flex items-center justify-between">
                  <span className="select-all">{newlyCreatedKey}</span>
                  <Button
                    size="sm"
                    variant="outline"
                    className="ml-3"
                    onClick={() => copyToClipboard(newlyCreatedKey)}
                  >
                    <Copy className="w-3 h-3 mr-1" />
                    Copy
                  </Button>
                </div>
              </div>
              <Button
                size="sm"
                variant="ghost"
                className="h-6 w-6 p-0"
                onClick={() => setNewlyCreatedKey(null)}
              >
                <XCircle className="w-4 h-4" />
              </Button>
            </div>
          </CardContent>
        </Card>
      )}

      {/* Create API Key Card */}
      <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
        <CardHeader>
          <div className="flex items-center justify-between">
            <CardTitle className="text-foreground/90 flex items-center gap-2">
              <Plus className="w-5 h-5" />
              Create API Key
            </CardTitle>
            {!isCreatingKey && (
              <Button
                size="sm"
                onClick={() => setIsCreatingKey(true)}
                className="text-xs h-8"
              >
                <Plus className="w-3 h-3 mr-1" />
                New API Key
              </Button>
            )}
          </div>
        </CardHeader>
        {isCreatingKey && (
          <CardContent className="space-y-4">
            <div className="grid grid-cols-2 gap-4">
              <div>
                <Label htmlFor="key-name" className="text-sm">
                  Key Name
                </Label>
                <Input
                  id="key-name"
                  placeholder="e.g., Production Monitoring"
                  value={newKeyName}
                  onChange={(e) => setNewKeyName(e.target.value)}
                  className="mt-1.5 text-sm"
                />
              </div>
              <div>
                <Label htmlFor="key-description" className="text-sm">
                  Description
                </Label>
                <Input
                  id="key-description"
                  placeholder="Brief description of usage"
                  value={newKeyDescription}
                  onChange={(e) => setNewKeyDescription(e.target.value)}
                  className="mt-1.5 text-sm"
                />
              </div>
            </div>

            <div className="grid grid-cols-2 gap-4">
              <div>
                <Label htmlFor="expiration" className="text-sm">
                  Expiration
                </Label>
                <Select value={newKeyExpiration} onValueChange={setNewKeyExpiration}>
                  <SelectTrigger id="expiration" className="mt-1.5 text-sm">
                    <SelectValue />
                  </SelectTrigger>
                  <SelectContent>
                    <SelectItem value="30">30 days</SelectItem>
                    <SelectItem value="90">90 days</SelectItem>
                    <SelectItem value="180">180 days</SelectItem>
                    <SelectItem value="365">1 year</SelectItem>
                    <SelectItem value="never">Never expires</SelectItem>
                  </SelectContent>
                </Select>
              </div>
              <div>
                <Label htmlFor="rate-limit" className="text-sm">
                  Rate Limit (requests/hour)
                </Label>
                <Select value={newKeyRateLimit} onValueChange={setNewKeyRateLimit}>
                  <SelectTrigger id="rate-limit" className="mt-1.5 text-sm">
                    <SelectValue />
                  </SelectTrigger>
                  <SelectContent>
                    <SelectItem value="100">100</SelectItem>
                    <SelectItem value="500">500</SelectItem>
                    <SelectItem value="1000">1,000</SelectItem>
                    <SelectItem value="5000">5,000</SelectItem>
                    <SelectItem value="10000">10,000</SelectItem>
                    <SelectItem value="unlimited">Unlimited</SelectItem>
                  </SelectContent>
                </Select>
              </div>
            </div>

            <Separator />

            {/* Permissions */}
            <div>
              <Label className="text-sm mb-3 block">Permissions</Label>
              <div className="bg-muted/30 rounded-lg p-4 border border-border">
                <div className="space-y-4">
                  {/* Read Permissions */}
                  <div>
                    <p className="text-xs font-medium text-muted-foreground mb-2">
                      Read Permissions
                    </p>
                    <div className="grid grid-cols-2 gap-2">
                      {availablePermissions
                        .filter((p) => p.category === 'read')
                        .map((permission) => (
                          <div
                            key={permission.id}
                            className="flex items-center space-x-2 bg-background rounded p-2 border border-border"
                          >
                            <Checkbox
                              id={permission.id}
                              checked={selectedPermissions.includes(permission.id)}
                              onCheckedChange={() => togglePermission(permission.id)}
                            />
                            <Label
                              htmlFor={permission.id}
                              className="text-xs cursor-pointer flex-1"
                            >
                              {permission.label}
                            </Label>
                          </div>
                        ))}
                    </div>
                  </div>

                  {/* Write Permissions */}
                  <div>
                    <p className="text-xs font-medium text-muted-foreground mb-2">
                      Write Permissions
                    </p>
                    <div className="grid grid-cols-2 gap-2">
                      {availablePermissions
                        .filter((p) => p.category === 'write')
                        .map((permission) => (
                          <div
                            key={permission.id}
                            className="flex items-center space-x-2 bg-background rounded p-2 border border-border"
                          >
                            <Checkbox
                              id={permission.id}
                              checked={selectedPermissions.includes(permission.id)}
                              onCheckedChange={() => togglePermission(permission.id)}
                            />
                            <Label
                              htmlFor={permission.id}
                              className="text-xs cursor-pointer flex-1"
                            >
                              {permission.label}
                            </Label>
                          </div>
                        ))}
                    </div>
                  </div>

                  {/* Admin Permissions */}
                  <div>
                    <p className="text-xs font-medium text-muted-foreground mb-2">
                      Administrative Permissions
                    </p>
                    <div className="bg-red-500/10 border border-red-500/30 rounded p-3">
                      {availablePermissions
                        .filter((p) => p.category === 'admin')
                        .map((permission) => (
                          <div key={permission.id} className="flex items-center space-x-2">
                            <Checkbox
                              id={permission.id}
                              checked={selectedPermissions.includes(permission.id)}
                              onCheckedChange={() => togglePermission(permission.id)}
                            />
                            <Label
                              htmlFor={permission.id}
                              className="text-xs cursor-pointer flex-1"
                            >
                              {permission.label}
                            </Label>
                            <AlertTriangle className="w-4 h-4 text-red-500" />
                          </div>
                        ))}
                      <p className="text-xs text-red-600 dark:text-red-400 mt-2">
                        Warning: Full admin access grants unrestricted API access
                      </p>
                    </div>
                  </div>
                </div>
              </div>
            </div>

            <div className="flex gap-2 justify-end">
              <Button
                variant="outline"
                onClick={() => {
                  setIsCreatingKey(false);
                  setNewKeyName('');
                  setNewKeyDescription('');
                }}
              >
                Cancel
              </Button>
              <Button onClick={handleCreateKey} disabled={!newKeyName.trim()}>
                <Key className="w-4 h-4 mr-2" />
                Create API Key
              </Button>
            </div>
          </CardContent>
        )}
      </Card>

      {/* API Keys List */}
      <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
        <CardHeader>
          <CardTitle className="text-foreground/90 flex items-center gap-2">
            <Key className="w-5 h-5" />
            Existing API Keys ({apiKeys.length})
          </CardTitle>
        </CardHeader>
        <CardContent>
          <div className="space-y-3">
            {apiKeys.map((apiKey) => {
              const isRevealed = revealedKeys.includes(apiKey.id);
              const expiringSoon = isExpiringSoon(apiKey.expiresAt);

              return (
                <div
                  key={apiKey.id}
                  className={`border rounded-lg p-4 ${
                    apiKey.status === 'active'
                      ? 'border-border bg-background'
                      : 'border-muted bg-muted/30'
                  }`}
                >
                  <div className="flex items-start justify-between mb-3">
                    <div className="flex-1">
                      <div className="flex items-center gap-2 mb-1">
                        <p className="text-sm font-medium text-foreground/90">{apiKey.name}</p>
                        {getStatusBadge(apiKey.status)}
                        {expiringSoon && apiKey.status === 'active' && (
                          <Badge variant="outline" className="text-xs text-yellow-600 border-yellow-600">
                            <AlertTriangle className="w-3 h-3 mr-1" />
                            Expiring Soon
                          </Badge>
                        )}
                      </div>
                      <p className="text-xs text-muted-foreground">{apiKey.description}</p>
                    </div>
                    <div className="flex gap-1">
                      {apiKey.status === 'active' && (
                        <>
                          <Button
                            size="sm"
                            variant="ghost"
                            className="h-8 w-8 p-0"
                            onClick={() => toggleRevealKey(apiKey.id)}
                            title={isRevealed ? 'Hide key' : 'Reveal key'}
                          >
                            {isRevealed ? (
                              <EyeOff className="w-4 h-4" />
                            ) : (
                              <Eye className="w-4 h-4" />
                            )}
                          </Button>
                          <Button
                            size="sm"
                            variant="ghost"
                            className="h-8 w-8 p-0"
                            onClick={() => revokeKey(apiKey.id)}
                            title="Revoke key"
                          >
                            <XCircle className="w-4 h-4 text-orange-500" />
                          </Button>
                        </>
                      )}
                      {apiKey.status !== 'active' && (
                        <Button
                          size="sm"
                          variant="ghost"
                          className="h-8 w-8 p-0"
                          onClick={() => deleteKey(apiKey.id)}
                          title="Delete key"
                        >
                          <Trash2 className="w-4 h-4 text-destructive" />
                        </Button>
                      )}
                    </div>
                  </div>

                  {/* API Key Display */}
                  <div className="bg-muted/50 rounded p-2 mb-3 flex items-center justify-between font-mono text-sm">
                    <span className={apiKey.status === 'active' ? '' : 'line-through opacity-50'}>
                      {isRevealed ? apiKey.key : `${apiKey.prefix}••••••••••••••••••••••••••`}
                    </span>
                    {apiKey.status === 'active' && (
                      <Button
                        size="sm"
                        variant="ghost"
                        className="h-6 text-xs"
                        onClick={() => copyToClipboard(apiKey.key)}
                      >
                        <Copy className="w-3 h-3 mr-1" />
                        Copy
                      </Button>
                    )}
                  </div>

                  {/* Metadata Grid */}
                  <div className="grid grid-cols-4 gap-4 text-xs">
                    <div>
                      <p className="text-muted-foreground mb-0.5">Created</p>
                      <p className="text-foreground/90">{formatTimestamp(apiKey.createdAt)}</p>
                      <p className="text-muted-foreground">{apiKey.createdBy}</p>
                    </div>
                    <div>
                      <p className="text-muted-foreground mb-0.5">Expires</p>
                      <p className="text-foreground/90">
                        {apiKey.expiresAt ? formatTimestamp(apiKey.expiresAt) : 'Never'}
                      </p>
                    </div>
                    <div>
                      <p className="text-muted-foreground mb-0.5">Last Used</p>
                      <p className="text-foreground/90">
                        {apiKey.lastUsed ? formatTimestamp(apiKey.lastUsed) : 'Never'}
                      </p>
                    </div>
                    <div>
                      <p className="text-muted-foreground mb-0.5">Usage / Rate Limit</p>
                      <p className="text-foreground/90">
                        {apiKey.usageCount.toLocaleString()} / {apiKey.rateLimit === -1 ? '∞' : apiKey.rateLimit}/hr
                      </p>
                    </div>
                  </div>

                  <Separator className="my-3" />

                  {/* Permissions */}
                  <div>
                    <p className="text-xs text-muted-foreground mb-2">
                      Permissions ({apiKey.permissions.length})
                    </p>
                    <div className="flex flex-wrap gap-1">
                      {apiKey.permissions.map((permission) => (
                        <Badge key={permission} variant="outline" className="text-xs">
                          {permission}
                        </Badge>
                      ))}
                    </div>
                  </div>
                </div>
              );
            })}

            {apiKeys.length === 0 && (
              <div className="text-center py-12 text-muted-foreground">
                <Key className="w-12 h-12 mx-auto mb-3 opacity-50" />
                <p className="text-sm">No API keys created yet</p>
                <p className="text-xs mt-1">Create an API key to access the HiveFS REST API</p>
              </div>
            )}
          </div>

          <p
            className="text-muted-foreground opacity-60 mt-4"
            style={{ fontSize: '0.7rem', lineHeight: '1.3' }}
          >
            API keys provide programmatic access to the HiveFS RESTful API. Keep your keys secure
            and never share them publicly. All API key operations are logged in the security audit.
          </p>
        </CardContent>
      </Card>
    </div>
  );
}
