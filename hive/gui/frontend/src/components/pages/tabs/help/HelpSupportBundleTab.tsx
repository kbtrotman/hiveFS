import { useState } from 'react';
import { Card, CardContent, CardHeader, CardTitle } from '../../../ui/card';
import { Input } from '../../../ui/input';
import { Label } from '../../../ui/label';
import { Button } from '../../../ui/button';
import { Separator } from '../../../ui/separator';
import { Badge } from '../../../ui/badge';
import { Checkbox } from '../../../ui/checkbox';
import {
  Package,
  FileArchive,
  Github,
  Download,
  Upload,
  CheckCircle,
  AlertCircle,
  Loader2,
  File,
  Server,
  Database,
  Activity,
  Shield,
  Settings,
  FileText,
  HardDrive,
  Clock,
  Trash2,
  ExternalLink,
} from 'lucide-react';

interface BundleItem {
  id: string;
  name: string;
  description: string;
  category: string;
  icon: typeof File;
  size: number;
  selected: boolean;
  required: boolean;
}

interface GeneratedBundle {
  id: string;
  filename: string;
  size: number;
  createdAt: string;
  status: 'generating' | 'ready' | 'uploaded' | 'failed';
  uploadUrl?: string;
  items: number;
}

export function HelpSupportBundleTab() {
  const [includeDescription, setIncludeDescription] = useState('');
  const [contactEmail, setContactEmail] = useState('');
  const [issueNumber, setIssueNumber] = useState('');
  const [isGenerating, setIsGenerating] = useState(false);
  const [generationProgress, setGenerationProgress] = useState(0);
  const [currentBundle, setCurrentBundle] = useState<GeneratedBundle | null>(null);

  const [bundleItems, setBundleItems] = useState<BundleItem[]>([
    {
      id: 'system-info',
      name: 'System Information',
      description: 'OS version, hardware specs, kernel info, uptime',
      category: 'System',
      icon: Server,
      size: 12 * 1024, // 12 KB
      selected: true,
      required: true,
    },
    {
      id: 'cluster-config',
      name: 'Cluster Configuration',
      description: 'Cluster topology, node list, network configuration',
      category: 'Configuration',
      icon: Settings,
      size: 45 * 1024, // 45 KB
      selected: true,
      required: true,
    },
    {
      id: 'daemon-logs',
      name: 'HiveFS Daemon Logs',
      description: 'Last 1000 lines from hivefs daemon (last 24 hours)',
      category: 'Logs',
      icon: FileText,
      size: 2.4 * 1024 * 1024, // 2.4 MB
      selected: true,
      required: true,
    },
    {
      id: 'application-logs',
      name: 'Application Logs',
      description: 'HiveFS application logs (errors, warnings, info)',
      category: 'Logs',
      icon: Activity,
      size: 1.8 * 1024 * 1024, // 1.8 MB
      selected: true,
      required: false,
    },
    {
      id: 'audit-logs',
      name: 'Security Audit Logs',
      description: 'Recent security events and audit trail (last 7 days)',
      category: 'Security',
      icon: Shield,
      size: 890 * 1024, // 890 KB
      selected: false,
      required: false,
    },
    {
      id: 'performance-metrics',
      name: 'Performance Metrics',
      description: 'CPU, memory, disk I/O, network stats (last 24 hours)',
      category: 'System',
      icon: Activity,
      size: 1.2 * 1024 * 1024, // 1.2 MB
      selected: true,
      required: false,
    },
    {
      id: 'storage-stats',
      name: 'Storage Statistics',
      description: 'Disk usage, file counts, block allocation data',
      category: 'System',
      icon: HardDrive,
      size: 156 * 1024, // 156 KB
      selected: true,
      required: false,
    },
    {
      id: 'database-logs',
      name: 'Database Query Logs',
      description: 'Recent database queries and slow query log',
      category: 'Logs',
      icon: Database,
      size: 3.2 * 1024 * 1024, // 3.2 MB
      selected: false,
      required: false,
    },
    {
      id: 'version-history',
      name: 'File Versioning Activity',
      description: 'Version history stats and recent restore operations',
      category: 'Logs',
      icon: Clock,
      size: 567 * 1024, // 567 KB
      selected: false,
      required: false,
    },
    {
      id: 'crash-dumps',
      name: 'Crash Dumps',
      description: 'Core dumps and stack traces from recent crashes',
      category: 'System',
      icon: AlertCircle,
      size: 5.6 * 1024 * 1024, // 5.6 MB
      selected: false,
      required: false,
    },
  ]);

  // Mock previous bundles
  const [previousBundles] = useState<GeneratedBundle[]>([
    {
      id: '1',
      filename: 'hivefs-support-bundle-2026-01-25-143022.tar.gz',
      size: 8.7 * 1024 * 1024,
      createdAt: '2026-01-25T14:30:22Z',
      status: 'uploaded',
      uploadUrl: 'https://github.com/kbtrotman/hivefs/issues/42',
      items: 7,
    },
    {
      id: '2',
      filename: 'hivefs-support-bundle-2026-01-23-091545.tar.gz',
      size: 12.3 * 1024 * 1024,
      createdAt: '2026-01-23T09:15:45Z',
      status: 'ready',
      items: 9,
    },
  ]);

  const formatBytes = (bytes: number) => {
    if (bytes === 0) return '0 B';
    const k = 1024;
    const sizes = ['B', 'KB', 'MB', 'GB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
  };

  const formatTimestamp = (timestamp: string) => {
    return new Date(timestamp).toLocaleString();
  };

  const getRelativeTime = (timestamp: string) => {
    const date = new Date(timestamp);
    const now = new Date();
    const diff = now.getTime() - date.getTime();
    const days = Math.floor(diff / 86400000);
    const hours = Math.floor((diff % 86400000) / 3600000);

    if (days > 0) return `${days}d ago`;
    if (hours > 0) return `${hours}h ago`;
    return 'Recently';
  };

  const toggleItem = (id: string) => {
    setBundleItems((items) =>
      items.map((item) =>
        item.id === id && !item.required ? { ...item, selected: !item.selected } : item
      )
    );
  };

  const selectedItems = bundleItems.filter((item) => item.selected);
  const totalSize = selectedItems.reduce((sum, item) => sum + item.size, 0);
  const estimatedCompressedSize = totalSize * 0.35; // Estimate 35% compression ratio

  const handleGenerateBundle = () => {
    setIsGenerating(true);
    setGenerationProgress(0);

    // Simulate bundle generation
    const interval = setInterval(() => {
      setGenerationProgress((prev) => {
        if (prev >= 100) {
          clearInterval(interval);
          setIsGenerating(false);

          // Create the bundle
          const now = new Date();
          const timestamp = now
            .toISOString()
            .replace(/[-:]/g, '')
            .replace('T', '-')
            .split('.')[0];
          const newBundle: GeneratedBundle = {
            id: Date.now().toString(),
            filename: `hivefs-support-bundle-${timestamp}.tar.gz`,
            size: estimatedCompressedSize,
            createdAt: now.toISOString(),
            status: 'ready',
            items: selectedItems.length,
          };

          setCurrentBundle(newBundle);
          return 100;
        }
        return prev + 5;
      });
    }, 150);
  };

  const handleDownloadBundle = () => {
    console.log('Downloading bundle:', currentBundle?.filename);
    // In real implementation, would trigger download
  };

  const handleUploadToGitHub = () => {
    if (!currentBundle) return;

    setCurrentBundle({ ...currentBundle, status: 'generating' });

    // Simulate upload
    setTimeout(() => {
      setCurrentBundle({
        ...currentBundle,
        status: 'uploaded',
        uploadUrl: `https://github.com/kbtrotman/hivefs/issues/${issueNumber || 'new'}`,
      });
    }, 2000);
  };

  const getCategoryIcon = (category: string) => {
    switch (category) {
      case 'System':
        return Server;
      case 'Logs':
        return FileText;
      case 'Configuration':
        return Settings;
      case 'Security':
        return Shield;
      default:
        return File;
    }
  };

  const getStatusBadge = (status: string) => {
    switch (status) {
      case 'generating':
        return (
          <Badge variant="outline" className="text-xs text-blue-600 border-blue-600">
            <Loader2 className="w-3 h-3 mr-1 animate-spin" />
            Generating
          </Badge>
        );
      case 'ready':
        return (
          <Badge variant="outline" className="text-xs text-green-600 border-green-600">
            <CheckCircle className="w-3 h-3 mr-1" />
            Ready
          </Badge>
        );
      case 'uploaded':
        return (
          <Badge variant="outline" className="text-xs text-purple-600 border-purple-600">
            <Upload className="w-3 h-3 mr-1" />
            Uploaded
          </Badge>
        );
      case 'failed':
        return (
          <Badge variant="outline" className="text-xs text-red-600 border-red-600">
            <AlertCircle className="w-3 h-3 mr-1" />
            Failed
          </Badge>
        );
      default:
        return null;
    }
  };

  const categories = Array.from(new Set(bundleItems.map((item) => item.category)));

  return (
    <div className="flex h-full w-full flex-col gap-6 overflow-auto bg-gradient-to-br from-background via-primary/5 to-background p-8">
      {/* Page Header */}
      <div>
        <h1 className="text-foreground/80">Create Support Bundle</h1>
        <p className="text-muted-foreground text-sm mt-1">
          Generate a diagnostic package with logs and system information for troubleshooting
        </p>
      </div>

      {/* Info Banner */}
      <div className="bg-blue-500/10 border border-blue-500/30 rounded-lg p-4">
        <div className="flex items-start gap-3">
          <AlertCircle className="w-5 h-5 text-blue-500 mt-0.5 flex-shrink-0" />
          <div>
            <p className="text-sm font-medium text-blue-700 dark:text-blue-400 mb-1">
              What is a Support Bundle?
            </p>
            <p className="text-xs text-muted-foreground">
              A support bundle is a compressed archive containing system logs, configuration files,
              and diagnostic information. This helps troubleshoot issues more efficiently. All
              sensitive data is automatically sanitized before inclusion. You can download the
              bundle locally or upload it directly to your GitHub issue.
            </p>
          </div>
        </div>
      </div>

      <div className="grid grid-cols-2 gap-6">
        {/* Select Bundle Contents */}
        <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
          <CardHeader>
            <CardTitle className="text-foreground/90 flex items-center gap-2">
              <Package className="w-5 h-5" />
              Select Bundle Contents
            </CardTitle>
          </CardHeader>
          <CardContent className="space-y-4">
            {categories.map((category) => (
              <div key={category}>
                <div className="flex items-center gap-2 mb-2">
                  {(() => {
                    const Icon = getCategoryIcon(category);
                    return <Icon className="w-4 h-4 text-muted-foreground" />;
                  })()}
                  <p className="text-sm font-medium text-foreground/90">{category}</p>
                </div>
                <div className="space-y-2 ml-6">
                  {bundleItems
                    .filter((item) => item.category === category)
                    .map((item) => {
                      const Icon = item.icon;
                      return (
                        <div
                          key={item.id}
                          className={`flex items-start gap-3 p-3 rounded border transition-colors ${
                            item.selected
                              ? 'bg-primary/5 border-primary/30'
                              : 'bg-muted/20 border-border'
                          }`}
                        >
                          <Checkbox
                            id={item.id}
                            checked={item.selected}
                            onCheckedChange={() => toggleItem(item.id)}
                            disabled={item.required}
                            className="mt-0.5"
                          />
                          <div className="flex-1 min-w-0">
                            <div className="flex items-center gap-2 mb-1">
                              <Label
                                htmlFor={item.id}
                                className="text-sm font-medium cursor-pointer"
                              >
                                {item.name}
                              </Label>
                              {item.required && (
                                <Badge variant="secondary" className="text-xs">
                                  Required
                                </Badge>
                              )}
                            </div>
                            <p className="text-xs text-muted-foreground mb-1">
                              {item.description}
                            </p>
                            <p className="text-xs text-muted-foreground">
                              Size: {formatBytes(item.size)}
                            </p>
                          </div>
                        </div>
                      );
                    })}
                </div>
                {category !== categories[categories.length - 1] && (
                  <Separator className="mt-4" />
                )}
              </div>
            ))}

            <Separator />

            <div className="bg-muted/30 rounded p-3">
              <div className="grid grid-cols-2 gap-3 text-sm">
                <div>
                  <p className="text-muted-foreground text-xs mb-1">Selected Items</p>
                  <p className="font-semibold">{selectedItems.length} files</p>
                </div>
                <div>
                  <p className="text-muted-foreground text-xs mb-1">Total Size</p>
                  <p className="font-semibold">{formatBytes(totalSize)}</p>
                </div>
                <div>
                  <p className="text-muted-foreground text-xs mb-1">Estimated Compressed</p>
                  <p className="font-semibold">{formatBytes(estimatedCompressedSize)}</p>
                </div>
                <div>
                  <p className="text-muted-foreground text-xs mb-1">Compression</p>
                  <p className="font-semibold">~65%</p>
                </div>
              </div>
            </div>
          </CardContent>
        </Card>

        {/* Bundle Information & Generation */}
        <div className="space-y-6">
          <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
            <CardHeader>
              <CardTitle className="text-foreground/90 flex items-center gap-2">
                <FileArchive className="w-5 h-5" />
                Bundle Information
              </CardTitle>
            </CardHeader>
            <CardContent className="space-y-4">
              <div>
                <Label htmlFor="description" className="text-sm">
                  Issue Description (Optional)
                </Label>
                <p className="text-xs text-muted-foreground mt-1 mb-2">
                  Brief description of the problem for bundle metadata
                </p>
                <Input
                  id="description"
                  placeholder="e.g., Cluster node connectivity issues"
                  value={includeDescription}
                  onChange={(e) => setIncludeDescription(e.target.value)}
                  className="text-sm"
                />
              </div>

              <div>
                <Label htmlFor="contact-email" className="text-sm">
                  Contact Email (Optional)
                </Label>
                <p className="text-xs text-muted-foreground mt-1 mb-2">
                  For follow-up questions about this bundle
                </p>
                <Input
                  id="contact-email"
                  type="email"
                  placeholder="your.email@example.com"
                  value={contactEmail}
                  onChange={(e) => setContactEmail(e.target.value)}
                  className="text-sm"
                />
              </div>

              <div>
                <Label htmlFor="issue-number" className="text-sm">
                  GitHub Issue Number (Optional)
                </Label>
                <p className="text-xs text-muted-foreground mt-1 mb-2">
                  Link bundle to existing GitHub issue
                </p>
                <Input
                  id="issue-number"
                  placeholder="e.g., 123"
                  value={issueNumber}
                  onChange={(e) => setIssueNumber(e.target.value)}
                  className="text-sm"
                />
              </div>

              <Separator />

              {!isGenerating && !currentBundle && (
                <Button
                  className="w-full"
                  onClick={handleGenerateBundle}
                  disabled={selectedItems.length === 0}
                >
                  <Package className="w-4 h-4 mr-2" />
                  Generate Support Bundle
                </Button>
              )}

              {isGenerating && (
                <div className="space-y-3">
                  <div className="flex items-center justify-between text-sm">
                    <span className="text-muted-foreground">Generating bundle...</span>
                    <span className="font-medium">{generationProgress}%</span>
                  </div>
                  <div className="w-full bg-muted rounded-full h-2">
                    <div
                      className="bg-primary h-2 rounded-full transition-all duration-300"
                      style={{ width: `${generationProgress}%` }}
                    />
                  </div>
                  <div className="flex items-center gap-2 text-xs text-muted-foreground">
                    <Loader2 className="w-3 h-3 animate-spin" />
                    <span>Collecting logs and system information...</span>
                  </div>
                </div>
              )}

              {currentBundle && (
                <div className="space-y-3">
                  <div className="bg-green-500/10 border border-green-500/30 rounded p-3">
                    <div className="flex items-start gap-2">
                      <CheckCircle className="w-5 h-5 text-green-600 mt-0.5" />
                      <div className="flex-1">
                        <p className="text-sm font-medium text-green-700 dark:text-green-400 mb-1">
                          Bundle Generated Successfully
                        </p>
                        <p className="text-xs text-muted-foreground mb-2">
                          {currentBundle.filename}
                        </p>
                        <div className="flex items-center gap-3 text-xs text-muted-foreground">
                          <span>{formatBytes(currentBundle.size)}</span>
                          <span>•</span>
                          <span>{currentBundle.items} items</span>
                          <span>•</span>
                          <span>{getRelativeTime(currentBundle.createdAt)}</span>
                        </div>
                      </div>
                    </div>
                  </div>

                  {currentBundle.status === 'uploaded' && currentBundle.uploadUrl && (
                    <div className="bg-purple-500/10 border border-purple-500/30 rounded p-3">
                      <div className="flex items-start gap-2">
                        <Upload className="w-5 h-5 text-purple-600 mt-0.5" />
                        <div className="flex-1">
                          <p className="text-sm font-medium text-purple-700 dark:text-purple-400 mb-1">
                            Uploaded to GitHub
                          </p>
                          <a
                            href={currentBundle.uploadUrl}
                            target="_blank"
                            rel="noopener noreferrer"
                            className="text-xs text-primary hover:underline flex items-center gap-1"
                          >
                            View on GitHub
                            <ExternalLink className="w-3 h-3" />
                          </a>
                        </div>
                      </div>
                    </div>
                  )}

                  <div className="flex gap-2">
                    <Button
                      variant="outline"
                      className="flex-1"
                      onClick={handleDownloadBundle}
                      disabled={currentBundle.status === 'generating'}
                    >
                      <Download className="w-4 h-4 mr-2" />
                      Download
                    </Button>
                    <Button
                      className="flex-1"
                      onClick={handleUploadToGitHub}
                      disabled={
                        currentBundle.status === 'generating' ||
                        currentBundle.status === 'uploaded'
                      }
                    >
                      {currentBundle.status === 'generating' ? (
                        <>
                          <Loader2 className="w-4 h-4 mr-2 animate-spin" />
                          Uploading...
                        </>
                      ) : currentBundle.status === 'uploaded' ? (
                        <>
                          <CheckCircle className="w-4 h-4 mr-2" />
                          Uploaded
                        </>
                      ) : (
                        <>
                          <Github className="w-4 h-4 mr-2" />
                          Upload to GitHub
                        </>
                      )}
                    </Button>
                  </div>

                  <Button
                    variant="ghost"
                    className="w-full text-xs"
                    onClick={() => {
                      setCurrentBundle(null);
                      setGenerationProgress(0);
                    }}
                  >
                    Create Another Bundle
                  </Button>
                </div>
              )}
            </CardContent>
          </Card>

          {/* Privacy Notice */}
          <Card className="border-yellow-500/30 bg-yellow-500/10 shadow-lg">
            <CardHeader>
              <CardTitle className="text-foreground/90 flex items-center gap-2">
                <Shield className="w-5 h-5 text-yellow-600" />
                Privacy & Security
              </CardTitle>
            </CardHeader>
            <CardContent className="space-y-2">
              <div className="flex items-start gap-2 text-xs">
                <CheckCircle className="w-3 h-3 text-green-500 mt-0.5 flex-shrink-0" />
                <p className="text-muted-foreground">
                  Passwords and authentication tokens are automatically redacted
                </p>
              </div>
              <div className="flex items-start gap-2 text-xs">
                <CheckCircle className="w-3 h-3 text-green-500 mt-0.5 flex-shrink-0" />
                <p className="text-muted-foreground">
                  Personal information is sanitized from logs before bundling
                </p>
              </div>
              <div className="flex items-start gap-2 text-xs">
                <CheckCircle className="w-3 h-3 text-green-500 mt-0.5 flex-shrink-0" />
                <p className="text-muted-foreground">
                  IP addresses are anonymized (last octet removed)
                </p>
              </div>
              <div className="flex items-start gap-2 text-xs">
                <CheckCircle className="w-3 h-3 text-green-500 mt-0.5 flex-shrink-0" />
                <p className="text-muted-foreground">
                  Bundle contents can be reviewed before uploading
                </p>
              </div>
            </CardContent>
          </Card>
        </div>
      </div>

      {/* Previous Bundles */}
      <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
        <CardHeader>
          <div className="flex items-center justify-between">
            <CardTitle className="text-foreground/90 flex items-center gap-2">
              <Clock className="w-5 h-5" />
              Previous Bundles
            </CardTitle>
            <Badge variant="outline" className="text-xs">
              {previousBundles.length} bundle(s)
            </Badge>
          </div>
        </CardHeader>
        <CardContent>
          <div className="space-y-2">
            {previousBundles.map((bundle) => (
              <div key={bundle.id} className="border rounded-lg p-3 bg-muted/20">
                <div className="flex items-start justify-between">
                  <div className="flex-1">
                    <div className="flex items-center gap-2 mb-2">
                      <FileArchive className="w-4 h-4 text-blue-500" />
                      <p className="text-sm font-medium font-mono">{bundle.filename}</p>
                      {getStatusBadge(bundle.status)}
                    </div>
                    <div className="flex items-center gap-3 text-xs text-muted-foreground">
                      <span>{formatBytes(bundle.size)}</span>
                      <span>•</span>
                      <span>{bundle.items} items</span>
                      <span>•</span>
                      <span>Created {formatTimestamp(bundle.createdAt)}</span>
                      {bundle.uploadUrl && (
                        <>
                          <span>•</span>
                          <a
                            href={bundle.uploadUrl}
                            target="_blank"
                            rel="noopener noreferrer"
                            className="text-primary hover:underline flex items-center gap-1"
                          >
                            View on GitHub
                            <ExternalLink className="w-3 h-3" />
                          </a>
                        </>
                      )}
                    </div>
                  </div>
                  <div className="flex gap-1">
                    <Button size="sm" variant="ghost" className="h-8 w-8 p-0" title="Download">
                      <Download className="w-4 h-4" />
                    </Button>
                    {bundle.status === 'ready' && (
                      <Button size="sm" variant="ghost" className="h-8 w-8 p-0" title="Upload">
                        <Upload className="w-4 h-4" />
                      </Button>
                    )}
                    <Button
                      size="sm"
                      variant="ghost"
                      className="h-8 w-8 p-0 text-destructive"
                      title="Delete"
                    >
                      <Trash2 className="w-4 h-4" />
                    </Button>
                  </div>
                </div>
              </div>
            ))}

            {previousBundles.length === 0 && (
              <div className="text-center py-8 text-muted-foreground">
                <FileArchive className="w-10 h-10 mx-auto mb-2 opacity-50" />
                <p className="text-sm">No previous bundles</p>
                <p className="text-xs mt-1">Generated bundles will appear here</p>
              </div>
            )}
          </div>
        </CardContent>
      </Card>
    </div>
  );
}
