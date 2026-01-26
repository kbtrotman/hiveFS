import { useState } from 'react';
import { Card, CardContent, CardHeader, CardTitle } from '../../../ui/card';
import { Input } from '../../../ui/input';
import { Label } from '../../../ui/label';
import { Button } from '../../../ui/button';
import { Separator } from '../../../ui/separator';
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '../../../ui/select';
import {
  Settings,
  History,
  AlertCircle,
  Check,
  Database,
  HardDrive,
  TrendingUp,
  Layers,
  Clock,
  Shield,
} from 'lucide-react';

export function VersionSettingsTab() {
  const [retentionDays, setRetentionDays] = useState('14');
  const [maxStorageSize, setMaxStorageSize] = useState('unlimited');
  const [maxStorageSizeCustom, setMaxStorageSizeCustom] = useState('');

  // Mock metrics
  const metrics = {
    retainedBlocks: 2847392,
    retainedSize: 1.2 * 1024 * 1024 * 1024 * 1024, // 1.2 TB
    versionedFiles: 45231,
    averageVersionsPerFile: 8.3,
    oldestVersion: '14 days ago',
  };

  const formatBytes = (bytes: number) => {
    if (bytes === 0) return '0 B';
    const k = 1024;
    const sizes = ['B', 'KB', 'MB', 'GB', 'TB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
  };

  const handleSaveRetention = () => {
    console.log('Saving retention policy:', retentionDays, 'days');
  };

  const handleSaveStorageLimit = () => {
    console.log('Saving storage limit:', maxStorageSize, maxStorageSizeCustom);
  };

  return (
    <div className="flex h-full w-full flex-col gap-6 overflow-auto bg-gradient-to-br from-background via-primary/5 to-background p-8">
      {/* Page Header */}
      <div>
        <h1 className="text-foreground/80">File Version Settings</h1>
        <p className="text-muted-foreground text-sm mt-1">
          Configure file versioning retention policies and storage limits
        </p>
      </div>

      {/* Info Banner */}
      <div className="bg-blue-500/10 border border-blue-500/30 rounded-lg p-4">
        <div className="flex items-start gap-3">
          <AlertCircle className="w-5 h-5 text-blue-500 mt-0.5 flex-shrink-0" />
          <div>
            <p className="text-sm font-medium text-blue-700 dark:text-blue-400 mb-1">
              How File Versioning Works
            </p>
            <p className="text-xs text-muted-foreground">
              HiveFS tracks changed blocks over time, not full file copies. Files can be
              restored to any point within the retention period. The number of recoverable
              versions varies by file activity. Versioning uses a copy-on-write mechanism
              that only stores blocks that change, making it highly space-efficient.
            </p>
          </div>
        </div>
      </div>

      {/* Metrics Strip */}
      <div className="grid grid-cols-5 gap-4">
        <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
          <CardContent className="pt-4">
            <div className="flex items-center justify-between">
              <div>
                <p className="text-xs text-muted-foreground">Retained Blocks</p>
                <p className="text-xl font-semibold mt-1">
                  {(metrics.retainedBlocks / 1000000).toFixed(2)}M
                </p>
              </div>
              <Database className="w-7 h-7 text-blue-500 opacity-50" />
            </div>
          </CardContent>
        </Card>

        <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
          <CardContent className="pt-4">
            <div className="flex items-center justify-between">
              <div>
                <p className="text-xs text-muted-foreground">Total Size Retained</p>
                <p className="text-xl font-semibold mt-1">{formatBytes(metrics.retainedSize)}</p>
              </div>
              <HardDrive className="w-7 h-7 text-purple-500 opacity-50" />
            </div>
          </CardContent>
        </Card>

        <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
          <CardContent className="pt-4">
            <div className="flex items-center justify-between">
              <div>
                <p className="text-xs text-muted-foreground">Versioned Files</p>
                <p className="text-xl font-semibold mt-1">
                  {metrics.versionedFiles.toLocaleString()}
                </p>
              </div>
              <Layers className="w-7 h-7 text-green-500 opacity-50" />
            </div>
          </CardContent>
        </Card>

        <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
          <CardContent className="pt-4">
            <div className="flex items-center justify-between">
              <div>
                <p className="text-xs text-muted-foreground">Avg Versions/File</p>
                <p className="text-xl font-semibold mt-1">{metrics.averageVersionsPerFile}</p>
              </div>
              <TrendingUp className="w-7 h-7 text-orange-500 opacity-50" />
            </div>
          </CardContent>
        </Card>

        <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
          <CardContent className="pt-4">
            <div className="flex items-center justify-between">
              <div>
                <p className="text-xs text-muted-foreground">Oldest Version</p>
                <p className="text-xl font-semibold mt-1">{metrics.oldestVersion}</p>
              </div>
              <Clock className="w-7 h-7 text-cyan-500 opacity-50" />
            </div>
          </CardContent>
        </Card>
      </div>

      <div className="grid grid-cols-2 gap-6">
        {/* Retention Period Settings */}
        <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
          <CardHeader>
            <CardTitle className="text-foreground/90 flex items-center gap-2">
              <History className="w-5 h-5" />
              Retention Period
            </CardTitle>
          </CardHeader>
          <CardContent className="space-y-4">
            <div>
              <Label htmlFor="retention-days" className="text-sm">
                Time-Based Retention
              </Label>
              <p className="text-xs text-muted-foreground mt-1 mb-2">
                How long to keep changed blocks for file version history
              </p>
              <Select value={retentionDays} onValueChange={setRetentionDays}>
                <SelectTrigger id="retention-days" className="text-sm">
                  <SelectValue />
                </SelectTrigger>
                <SelectContent>
                  <SelectItem value="0">0 days (Turn off)</SelectItem>
                  <SelectItem value="1">1 day</SelectItem>
                  <SelectItem value="3">3 days</SelectItem>
                  <SelectItem value="7">1 week</SelectItem>
                  <SelectItem value="14">2 weeks</SelectItem>
                  <SelectItem value="30">1 month</SelectItem>
                  <SelectItem value="60">2 months</SelectItem>
                  <SelectItem value="90">3 months</SelectItem>
                  <SelectItem value="180">6 months</SelectItem>
                  <SelectItem value="365">1 year</SelectItem>
                </SelectContent>
              </Select>
            </div>

            <div className="bg-muted/30 rounded p-3 space-y-2">
              <p className="text-xs font-medium text-foreground/90">Current Configuration</p>
              <div className="grid grid-cols-2 gap-2 text-xs">
                <div>
                  <span className="text-muted-foreground">Retention:</span>
                  <span className="ml-2 font-medium">
                    {retentionDays === '0' ? 'Disabled' : `${retentionDays} days`}
                  </span>
                </div>
                <div>
                  <span className="text-muted-foreground">Status:</span>
                  <span
                    className={`ml-2 font-medium ${
                      retentionDays === '0' ? 'text-red-600' : 'text-green-600'
                    }`}
                  >
                    {retentionDays === '0' ? 'Disabled' : 'Active'}
                  </span>
                </div>
              </div>
            </div>

            <Button onClick={handleSaveRetention} className="w-full">
              <Check className="w-4 h-4 mr-2" />
              Save Retention Policy
            </Button>

            <p
              className="text-muted-foreground opacity-60 pt-2 border-t border-border"
              style={{ fontSize: '0.7rem', lineHeight: '1.3' }}
            >
              Reducing the retention period will permanently delete older version blocks that
              fall outside the new window. This operation cannot be undone.
            </p>
          </CardContent>
        </Card>

        {/* Storage Limit Settings */}
        <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
          <CardHeader>
            <CardTitle className="text-foreground/90 flex items-center gap-2">
              <Shield className="w-5 h-5" />
              Storage Limit
            </CardTitle>
          </CardHeader>
          <CardContent className="space-y-4">
            <div>
              <Label htmlFor="max-storage" className="text-sm">
                Maximum Versioning Storage
              </Label>
              <p className="text-xs text-muted-foreground mt-1 mb-2">
                Limit the total storage used for version blocks
              </p>
              <Select value={maxStorageSize} onValueChange={setMaxStorageSize}>
                <SelectTrigger id="max-storage" className="text-sm">
                  <SelectValue />
                </SelectTrigger>
                <SelectContent>
                  <SelectItem value="unlimited">Unlimited</SelectItem>
                  <SelectItem value="100">100 GB</SelectItem>
                  <SelectItem value="250">250 GB</SelectItem>
                  <SelectItem value="500">500 GB</SelectItem>
                  <SelectItem value="1000">1 TB</SelectItem>
                  <SelectItem value="2000">2 TB</SelectItem>
                  <SelectItem value="5000">5 TB</SelectItem>
                  <SelectItem value="10000">10 TB</SelectItem>
                  <SelectItem value="custom">Custom...</SelectItem>
                </SelectContent>
              </Select>
            </div>

            {maxStorageSize === 'custom' && (
              <div>
                <Label htmlFor="custom-storage" className="text-sm">
                  Custom Storage Limit (GB)
                </Label>
                <Input
                  id="custom-storage"
                  type="number"
                  placeholder="Enter size in GB"
                  value={maxStorageSizeCustom}
                  onChange={(e) => setMaxStorageSizeCustom(e.target.value)}
                  className="mt-1.5 text-sm"
                />
              </div>
            )}

            <div className="bg-yellow-500/10 border border-yellow-500/30 rounded p-3">
              <div className="flex items-start gap-2">
                <AlertCircle className="w-4 h-4 text-yellow-600 mt-0.5" />
                <div>
                  <p className="text-sm font-medium text-yellow-700 dark:text-yellow-400">
                    Storage Limit Behavior
                  </p>
                  <p className="text-xs text-muted-foreground mt-1">
                    When the storage limit is reached, older version blocks will be automatically
                    pruned to stay within the limit. Recent versions are always preserved first.
                    This works in conjunction with time-based retention.
                  </p>
                </div>
              </div>
            </div>

            <div className="bg-muted/30 rounded p-3 space-y-2">
              <p className="text-xs font-medium text-foreground/90">Current Usage</p>
              <div className="space-y-2">
                <div className="flex justify-between text-xs">
                  <span className="text-muted-foreground">Current Size:</span>
                  <span className="font-medium">{formatBytes(metrics.retainedSize)}</span>
                </div>
                <div className="flex justify-between text-xs">
                  <span className="text-muted-foreground">Limit:</span>
                  <span className="font-medium">
                    {maxStorageSize === 'unlimited'
                      ? 'Unlimited'
                      : maxStorageSize === 'custom'
                      ? maxStorageSizeCustom
                        ? `${maxStorageSizeCustom} GB`
                        : 'Not set'
                      : `${maxStorageSize} GB`}
                  </span>
                </div>
                {maxStorageSize !== 'unlimited' && (
                  <div className="pt-2">
                    <div className="w-full bg-muted rounded-full h-2">
                      <div
                        className="bg-blue-500 h-2 rounded-full transition-all"
                        style={{
                          width: `${Math.min(
                            100,
                            (metrics.retainedSize /
                              (parseFloat(
                                maxStorageSize === 'custom'
                                  ? maxStorageSizeCustom || '1000'
                                  : maxStorageSize
                              ) *
                                1024 *
                                1024 *
                                1024)) *
                              100
                          )}%`,
                        }}
                      />
                    </div>
                    <p className="text-xs text-muted-foreground mt-1">
                      {(
                        (metrics.retainedSize /
                          (parseFloat(
                            maxStorageSize === 'custom'
                              ? maxStorageSizeCustom || '1000'
                              : maxStorageSize
                          ) *
                            1024 *
                            1024 *
                            1024)) *
                        100
                      ).toFixed(1)}
                      % used
                    </p>
                  </div>
                )}
              </div>
            </div>

            <Button onClick={handleSaveStorageLimit} className="w-full">
              <Check className="w-4 h-4 mr-2" />
              Save Storage Limit
            </Button>

            <p
              className="text-muted-foreground opacity-60 pt-2 border-t border-border"
              style={{ fontSize: '0.7rem', lineHeight: '1.3' }}
            >
              Storage limits help prevent version blocks from consuming excessive disk space.
              Pruning happens automatically and preserves the most recent versions.
            </p>
          </CardContent>
        </Card>
      </div>

      {/* Additional Info */}
      <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
        <CardHeader>
          <CardTitle className="text-foreground/90 flex items-center gap-2">
            <Settings className="w-5 h-5" />
            Policy Interaction
          </CardTitle>
        </CardHeader>
        <CardContent>
          <div className="space-y-3">
            <p className="text-sm text-foreground/90">
              Both retention policies work together to manage version storage:
            </p>
            <div className="grid grid-cols-2 gap-4">
              <div className="bg-muted/30 rounded p-3 border border-border">
                <p className="text-sm font-medium mb-2 flex items-center gap-2">
                  <History className="w-4 h-4" />
                  Time-Based Retention
                </p>
                <p className="text-xs text-muted-foreground">
                  Automatically removes version blocks older than the specified retention period,
                  regardless of storage usage.
                </p>
              </div>
              <div className="bg-muted/30 rounded p-3 border border-border">
                <p className="text-sm font-medium mb-2 flex items-center gap-2">
                  <Shield className="w-4 h-4" />
                  Storage Limit
                </p>
                <p className="text-xs text-muted-foreground">
                  When the limit is reached, prunes oldest version blocks first until usage falls
                  below the limit, even if within the retention period.
                </p>
              </div>
            </div>
            <div className="bg-blue-500/10 border border-blue-500/30 rounded p-3 mt-3">
              <p className="text-xs text-muted-foreground">
                <strong>Example:</strong> With a 30-day retention and 1 TB storage limit, versions
                are kept for up to 30 days. However, if version storage exceeds 1 TB, the oldest
                versions are pruned (even if less than 30 days old) to maintain the limit.
              </p>
            </div>
          </div>
        </CardContent>
      </Card>
    </div>
  );
}
