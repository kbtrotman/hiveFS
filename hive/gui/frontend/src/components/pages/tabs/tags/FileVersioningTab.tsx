import { useState } from 'react';
import { Card, CardContent, CardHeader, CardTitle } from '../../../ui/card';
import { Input } from '../../../ui/input';
import { Label } from '../../../ui/label';
import { Button } from '../../../ui/button';
import { Separator } from '../../../ui/separator';
import { Badge } from '../../../ui/badge';
import { Slider } from '../../../ui/slider';
import {
  Search,
  RotateCcw,
  File,
  Folder,
  Calendar,
  Clock,
  Archive,
  AlertCircle,
  ChevronRight,
  ChevronDown,
  FolderOpen,
  CheckCircle,
  XCircle,
  Loader2,
  History,
} from 'lucide-react';

interface FileVersion {
  id: string;
  filepath: string;
  filename: string;
  size: number;
  modifiedAt: string;
  modifiedBy: string;
  checksum: string;
  changeBlocks: number;
  isDeleted: boolean;
}

interface FileSearchResult {
  filepath: string;
  filename: string;
  currentSize: number;
  lastModified: string;
  versionCount: number;
  oldestVersion: string;
  isDeleted: boolean;
}

interface FolderNode {
  name: string;
  path: string;
  children?: FolderNode[];
  isExpanded?: boolean;
}

interface RestoreLog {
  id: string;
  timestamp: string;
  filepath: string;
  restoreToDate: string;
  status: 'in-progress' | 'completed' | 'failed';
  message: string;
  initiatedBy: string;
}

export function FileVersioningTab() {
  const [searchQuery, setSearchQuery] = useState('');
  const [selectedFile, setSelectedFile] = useState<FileSearchResult | null>(null);
  const [timelinePosition, setTimelinePosition] = useState(100);
  const [showRestoreConfirm, setShowRestoreConfirm] = useState(false);
  const [selectedPath, setSelectedPath] = useState<string>('/');
  const [expandedFolders, setExpandedFolders] = useState<string[]>(['/']);

  // Mock filesystem tree
  const [fileTree] = useState<FolderNode>({
    name: '/',
    path: '/',
    isExpanded: true,
    children: [
      {
        name: 'projects',
        path: '/projects',
        children: [
          {
            name: 'web-app',
            path: '/projects/web-app',
            children: [
              { name: 'src', path: '/projects/web-app/src' },
              { name: 'public', path: '/projects/web-app/public' },
            ],
          },
          { name: 'mobile-app', path: '/projects/mobile-app' },
        ],
      },
      {
        name: 'documents',
        path: '/documents',
        children: [
          { name: 'reports', path: '/documents/reports' },
          { name: 'presentations', path: '/documents/presentations' },
        ],
      },
      {
        name: 'data',
        path: '/data',
        children: [
          { name: 'backups', path: '/data/backups' },
          { name: 'exports', path: '/data/exports' },
        ],
      },
      { name: 'config', path: '/config' },
      { name: 'logs', path: '/logs' },
    ],
  });

  // Mock restore logs
  const [restoreLogs, setRestoreLogs] = useState<RestoreLog[]>([
    {
      id: '1',
      timestamp: '2026-01-25T14:30:00Z',
      filepath: '/projects/web-app/src/components/Dashboard.tsx',
      restoreToDate: '2026-01-23T16:45:00Z',
      status: 'completed',
      message: 'File successfully restored to version from 2 days ago',
      initiatedBy: 'john.smith@hivefs.com',
    },
    {
      id: '2',
      timestamp: '2026-01-25T13:15:00Z',
      filepath: '/data/customer-records.csv',
      restoreToDate: '2026-01-22T14:20:00Z',
      status: 'completed',
      message: 'Deleted file successfully recovered',
      initiatedBy: 'sarah.johnson@hivefs.com',
    },
    {
      id: '3',
      timestamp: '2026-01-25T12:00:00Z',
      filepath: '/documents/Q4-Financial-Report.xlsx',
      restoreToDate: '2026-01-20T10:00:00Z',
      status: 'failed',
      message: 'Restore failed: Version blocks not found in retention window',
      initiatedBy: 'michael.chen@hivefs.com',
    },
  ]);

  // Mock file search results
  const [searchResults] = useState<FileSearchResult[]>([
    {
      filepath: '/projects/web-app/src/components/Dashboard.tsx',
      filename: 'Dashboard.tsx',
      currentSize: 45678,
      lastModified: '2026-01-25T14:30:00Z',
      versionCount: 12,
      oldestVersion: '2026-01-18T09:15:00Z',
      isDeleted: false,
    },
    {
      filepath: '/projects/web-app/src/utils/api.ts',
      filename: 'api.ts',
      currentSize: 23456,
      lastModified: '2026-01-24T16:45:00Z',
      versionCount: 8,
      oldestVersion: '2026-01-19T11:30:00Z',
      isDeleted: false,
    },
    {
      filepath: '/documents/Q4-Financial-Report.xlsx',
      filename: 'Q4-Financial-Report.xlsx',
      currentSize: 1234567,
      lastModified: '2026-01-23T10:00:00Z',
      versionCount: 15,
      oldestVersion: '2026-01-15T08:00:00Z',
      isDeleted: false,
    },
    {
      filepath: '/data/customer-records.csv',
      filename: 'customer-records.csv',
      currentSize: 0,
      lastModified: '2026-01-22T14:20:00Z',
      versionCount: 6,
      oldestVersion: '2026-01-20T09:00:00Z',
      isDeleted: true,
    },
    {
      filepath: '/config/production.yaml',
      filename: 'production.yaml',
      currentSize: 8901,
      lastModified: '2026-01-25T08:15:00Z',
      versionCount: 18,
      oldestVersion: '2026-01-12T13:45:00Z',
      isDeleted: false,
    },
  ]);

  // Mock file versions for selected file
  const [fileVersions] = useState<FileVersion[]>([
    {
      id: '1',
      filepath: '/projects/web-app/src/components/Dashboard.tsx',
      filename: 'Dashboard.tsx',
      size: 45678,
      modifiedAt: '2026-01-25T14:30:00Z',
      modifiedBy: 'sarah.johnson@hivefs.com',
      checksum: 'a1b2c3d4e5f6',
      changeBlocks: 0,
      isDeleted: false,
    },
    {
      id: '2',
      filepath: '/projects/web-app/src/components/Dashboard.tsx',
      filename: 'Dashboard.tsx',
      size: 44512,
      modifiedAt: '2026-01-24T11:20:00Z',
      modifiedBy: 'michael.chen@hivefs.com',
      checksum: 'b2c3d4e5f6a1',
      changeBlocks: 3,
      isDeleted: false,
    },
    {
      id: '3',
      filepath: '/projects/web-app/src/components/Dashboard.tsx',
      filename: 'Dashboard.tsx',
      size: 43890,
      modifiedAt: '2026-01-23T16:45:00Z',
      modifiedBy: 'sarah.johnson@hivefs.com',
      checksum: 'c3d4e5f6a1b2',
      changeBlocks: 5,
      isDeleted: false,
    },
    {
      id: '4',
      filepath: '/projects/web-app/src/components/Dashboard.tsx',
      filename: 'Dashboard.tsx',
      size: 42345,
      modifiedAt: '2026-01-22T09:30:00Z',
      modifiedBy: 'john.smith@hivefs.com',
      checksum: 'd4e5f6a1b2c3',
      changeBlocks: 7,
      isDeleted: false,
    },
    {
      id: '5',
      filepath: '/projects/web-app/src/components/Dashboard.tsx',
      filename: 'Dashboard.tsx',
      size: 41234,
      modifiedAt: '2026-01-21T14:15:00Z',
      modifiedBy: 'sarah.johnson@hivefs.com',
      checksum: 'e5f6a1b2c3d4',
      changeBlocks: 4,
      isDeleted: false,
    },
    {
      id: '6',
      filepath: '/projects/web-app/src/components/Dashboard.tsx',
      filename: 'Dashboard.tsx',
      size: 40123,
      modifiedAt: '2026-01-20T10:00:00Z',
      modifiedBy: 'michael.chen@hivefs.com',
      checksum: 'f6a1b2c3d4e5',
      changeBlocks: 6,
      isDeleted: false,
    },
    {
      id: '7',
      filepath: '/projects/web-app/src/components/Dashboard.tsx',
      filename: 'Dashboard.tsx',
      size: 39012,
      modifiedAt: '2026-01-19T15:30:00Z',
      modifiedBy: 'john.smith@hivefs.com',
      checksum: 'a1b2c3d4e5f7',
      changeBlocks: 8,
      isDeleted: false,
    },
    {
      id: '8',
      filepath: '/projects/web-app/src/components/Dashboard.tsx',
      filename: 'Dashboard.tsx',
      size: 38456,
      modifiedAt: '2026-01-18T09:15:00Z',
      modifiedBy: 'sarah.johnson@hivefs.com',
      checksum: 'b2c3d4e5f6a8',
      changeBlocks: 9,
      isDeleted: false,
    },
  ]);

  const formatBytes = (bytes: number) => {
    if (bytes === 0) return '0 B';
    const k = 1024;
    const sizes = ['B', 'KB', 'MB', 'GB', 'TB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
  };

  const formatTimestamp = (timestamp: string) => {
    const date = new Date(timestamp);
    return date.toLocaleString();
  };

  const getRelativeTime = (timestamp: string) => {
    const date = new Date(timestamp);
    const now = new Date();
    const diff = now.getTime() - date.getTime();
    const days = Math.floor(diff / 86400000);
    const hours = Math.floor((diff % 86400000) / 3600000);
    const minutes = Math.floor((diff % 3600000) / 60000);

    if (days > 0) return `${days}d ago`;
    if (hours > 0) return `${hours}h ago`;
    if (minutes > 0) return `${minutes}m ago`;
    return 'Just now';
  };

  const toggleFolder = (path: string) => {
    setExpandedFolders((prev) =>
      prev.includes(path) ? prev.filter((p) => p !== path) : [...prev, path]
    );
  };

  const renderFolderTree = (node: FolderNode, depth: number = 0): JSX.Element => {
    const isExpanded = expandedFolders.includes(node.path);
    const isSelected = selectedPath === node.path;
    const hasChildren = node.children && node.children.length > 0;

    return (
      <div key={node.path}>
        <div
          className={`flex items-center gap-2 px-2 py-1.5 rounded cursor-pointer transition-colors ${
            isSelected ? 'bg-primary/10 text-primary' : 'hover:bg-muted'
          }`}
          style={{ paddingLeft: `${depth * 16 + 8}px` }}
          onClick={() => {
            setSelectedPath(node.path);
            if (hasChildren) {
              toggleFolder(node.path);
            }
          }}
        >
          {hasChildren ? (
            <button
              onClick={(e) => {
                e.stopPropagation();
                toggleFolder(node.path);
              }}
              className="p-0 h-4 w-4"
            >
              {isExpanded ? (
                <ChevronDown className="w-4 h-4" />
              ) : (
                <ChevronRight className="w-4 h-4" />
              )}
            </button>
          ) : (
            <div className="w-4" />
          )}
          {isExpanded && hasChildren ? (
            <FolderOpen className="w-4 h-4" />
          ) : (
            <Folder className="w-4 h-4" />
          )}
          <span className="text-sm">{node.name}</span>
        </div>
        {isExpanded && hasChildren && (
          <div>
            {node.children!.map((child) => renderFolderTree(child, depth + 1))}
          </div>
        )}
      </div>
    );
  };

  const filteredResults = searchResults.filter((result) => {
    const matchesSearch =
      result.filename.toLowerCase().includes(searchQuery.toLowerCase()) ||
      result.filepath.toLowerCase().includes(searchQuery.toLowerCase());
    const matchesPath = result.filepath.startsWith(selectedPath);
    return matchesSearch && matchesPath;
  });

  const handleSelectFile = (file: FileSearchResult) => {
    setSelectedFile(file);
    setTimelinePosition(100);
  };

  const getVersionAtPosition = () => {
    if (!selectedFile) return null;
    const totalVersions = fileVersions.length;
    const index = Math.floor((1 - timelinePosition / 100) * (totalVersions - 1));
    return fileVersions[index] || fileVersions[0];
  };

  const selectedVersion = getVersionAtPosition();

  const handleRestoreFile = () => {
    if (!selectedVersion) return;

    // Add to restore logs
    const newLog: RestoreLog = {
      id: Date.now().toString(),
      timestamp: new Date().toISOString(),
      filepath: selectedFile!.filepath,
      restoreToDate: selectedVersion.modifiedAt,
      status: 'in-progress',
      message: 'Initiating file restore...',
      initiatedBy: 'current.user@hivefs.com',
    };

    setRestoreLogs([newLog, ...restoreLogs]);

    // Simulate completion after 2 seconds
    setTimeout(() => {
      setRestoreLogs((logs) =>
        logs.map((log) =>
          log.id === newLog.id
            ? {
                ...log,
                status: 'completed' as const,
                message: selectedFile!.isDeleted
                  ? 'Deleted file successfully recovered'
                  : `File successfully restored to version from ${getRelativeTime(
                      selectedVersion.modifiedAt
                    )}`,
              }
            : log
        )
      );
    }, 2000);

    setShowRestoreConfirm(false);
    setSelectedFile(null);
    setTimelinePosition(100);
  };

  const getTimelineDate = () => {
    if (!selectedFile || fileVersions.length === 0) return new Date();
    const oldest = new Date(fileVersions[fileVersions.length - 1].modifiedAt);
    const newest = new Date(fileVersions[0].modifiedAt);
    const range = newest.getTime() - oldest.getTime();
    const position = timelinePosition / 100;
    return new Date(oldest.getTime() + range * position);
  };

  const getStatusIcon = (status: string) => {
    switch (status) {
      case 'in-progress':
        return <Loader2 className="w-4 h-4 text-blue-500 animate-spin" />;
      case 'completed':
        return <CheckCircle className="w-4 h-4 text-green-500" />;
      case 'failed':
        return <XCircle className="w-4 h-4 text-red-500" />;
      default:
        return null;
    }
  };

  const getStatusBadge = (status: string) => {
    switch (status) {
      case 'in-progress':
        return (
          <Badge variant="outline" className="text-xs text-blue-600 border-blue-600">
            In Progress
          </Badge>
        );
      case 'completed':
        return (
          <Badge variant="outline" className="text-xs text-green-600 border-green-600">
            Completed
          </Badge>
        );
      case 'failed':
        return (
          <Badge variant="outline" className="text-xs text-red-600 border-red-600">
            Failed
          </Badge>
        );
      default:
        return null;
    }
  };

  return (
    <div className="flex h-full w-full flex-col gap-6 overflow-auto bg-gradient-to-br from-background via-primary/5 to-background p-8">
      {/* Page Header */}
      <div>
        <h1 className="text-foreground/80">File Versioning</h1>
        <p className="text-muted-foreground text-sm mt-1">
          Search for files and restore them to previous versions
        </p>
      </div>

      <div className="grid grid-cols-2 gap-6">
        {/* Left Column: Filesystem Tree */}
        <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
          <CardHeader>
            <CardTitle className="text-foreground/90 flex items-center gap-2">
              <Folder className="w-5 h-5" />
              Select Starting Path
            </CardTitle>
          </CardHeader>
          <CardContent>
            <div>
              <p className="text-xs text-muted-foreground mb-3">
                Choose a starting directory to narrow your search. This improves search
                performance on large filesystems.
              </p>
              <div className="bg-muted/30 rounded p-2 mb-3 border border-border">
                <p className="text-xs text-muted-foreground">Selected path:</p>
                <p className="text-sm font-mono font-medium">{selectedPath}</p>
              </div>
              <div className="border border-border rounded max-h-[400px] overflow-y-auto bg-background">
                {renderFolderTree(fileTree)}
              </div>
            </div>
          </CardContent>
        </Card>

        {/* Right Column: Search and Restore */}
        <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
          <CardHeader>
            <CardTitle className="text-foreground/90 flex items-center gap-2">
              <RotateCcw className="w-5 h-5" />
              Search & Restore
            </CardTitle>
          </CardHeader>
          <CardContent className="space-y-4">
            {/* Search Field */}
            <div>
              <Label htmlFor="file-search" className="text-sm">
                Search for File
              </Label>
              <div className="relative mt-1.5">
                <Search className="absolute left-2 top-1/2 -translate-y-1/2 w-4 h-4 text-muted-foreground" />
                <Input
                  id="file-search"
                  placeholder="Search by filename or path..."
                  value={searchQuery}
                  onChange={(e) => setSearchQuery(e.target.value)}
                  className="pl-8 text-sm"
                />
              </div>
              <p className="text-xs text-muted-foreground mt-1">
                Searching in: <span className="font-mono">{selectedPath}</span>
              </p>
            </div>

            {/* File List or Empty State */}
            {!searchQuery ? (
              <div className="text-center py-12 text-muted-foreground">
                <Search className="w-12 h-12 mx-auto mb-3 opacity-50" />
                <p className="text-sm">Enter a filename to search</p>
                <p className="text-xs mt-1">Results will be filtered by selected path</p>
              </div>
            ) : !selectedFile ? (
              <>
                <Separator />
                <div>
                  <p className="text-xs text-muted-foreground mb-2">
                    Found {filteredResults.length} file(s) in {selectedPath}
                  </p>
                  <div className="space-y-2 max-h-[450px] overflow-y-auto">
                    {filteredResults.map((file) => (
                      <div
                        key={file.filepath}
                        onClick={() => handleSelectFile(file)}
                        className="border rounded p-3 cursor-pointer transition-all border-border bg-background hover:border-primary/50"
                      >
                        <div className="flex items-start gap-2">
                          {file.isDeleted ? (
                            <Archive className="w-4 h-4 text-red-500 mt-0.5" />
                          ) : (
                            <File className="w-4 h-4 text-blue-500 mt-0.5" />
                          )}
                          <div className="flex-1 min-w-0">
                            <div className="flex items-center gap-2">
                              <p className="text-sm font-medium text-foreground/90 truncate">
                                {file.filename}
                              </p>
                              {file.isDeleted && (
                                <Badge variant="destructive" className="text-xs">
                                  Deleted
                                </Badge>
                              )}
                            </div>
                            <p className="text-xs text-muted-foreground truncate">
                              {file.filepath}
                            </p>
                            <div className="flex items-center gap-3 mt-1.5 text-xs text-muted-foreground">
                              <span>{formatBytes(file.currentSize)}</span>
                              <span>•</span>
                              <span>{file.versionCount} versions</span>
                              <span>•</span>
                              <span>{getRelativeTime(file.lastModified)}</span>
                            </div>
                          </div>
                        </div>
                      </div>
                    ))}
                    {filteredResults.length === 0 && (
                      <div className="text-center py-8 text-muted-foreground">
                        <Search className="w-10 h-10 mx-auto mb-2 opacity-50" />
                        <p className="text-sm">No files found</p>
                        <p className="text-xs mt-1">Try a different path or search term</p>
                      </div>
                    )}
                  </div>
                </div>
              </>
            ) : (
              <>
                <Separator />

                {/* Selected File Info */}
                <div className="bg-muted/30 rounded p-4 border border-border">
                  <div className="flex items-start gap-3">
                    {selectedFile.isDeleted ? (
                      <Archive className="w-5 h-5 text-red-500 mt-0.5" />
                    ) : (
                      <File className="w-5 h-5 text-blue-500 mt-0.5" />
                    )}
                    <div className="flex-1 min-w-0">
                      <div className="flex items-center gap-2">
                        <p className="text-sm font-medium text-foreground/90">
                          {selectedFile.filename}
                        </p>
                        {selectedFile.isDeleted && (
                          <Badge variant="destructive" className="text-xs">
                            Deleted
                          </Badge>
                        )}
                      </div>
                      <p className="text-xs text-muted-foreground break-all">
                        {selectedFile.filepath}
                      </p>
                      <div className="mt-2 grid grid-cols-2 gap-2 text-xs">
                        <div>
                          <span className="text-muted-foreground">Versions:</span>
                          <span className="ml-2 font-medium">{selectedFile.versionCount}</span>
                        </div>
                        <div>
                          <span className="text-muted-foreground">Last Modified:</span>
                          <span className="ml-2 font-medium">
                            {getRelativeTime(selectedFile.lastModified)}
                          </span>
                        </div>
                      </div>
                    </div>
                  </div>
                </div>

                {/* Timeline Slider */}
                <div>
                  <div className="flex items-center justify-between mb-3">
                    <Label className="text-sm">Select Point in Time</Label>
                    <Badge variant="outline" className="text-xs">
                      <Calendar className="w-3 h-3 mr-1" />
                      {formatTimestamp(getTimelineDate().toISOString())}
                    </Badge>
                  </div>

                  <div className="bg-muted/30 rounded-lg p-4 border border-border">
                    <div className="mb-4">
                      <Slider
                        value={[timelinePosition]}
                        onValueChange={(value) => setTimelinePosition(value[0])}
                        min={0}
                        max={100}
                        step={1}
                        className="w-full"
                      />
                    </div>

                    <div className="flex justify-between text-xs text-muted-foreground">
                      <div className="flex items-center gap-1">
                        <Clock className="w-3 h-3" />
                        <span>{formatTimestamp(selectedFile.oldestVersion)}</span>
                      </div>
                      <div className="flex items-center gap-1">
                        <Clock className="w-3 h-3" />
                        <span>Current</span>
                      </div>
                    </div>
                  </div>

                  <p className="text-xs text-muted-foreground mt-2">
                    Drag the slider to select a point in time. The file will be restored to its
                    state at the selected moment.
                  </p>
                </div>

                {/* Version Details */}
                {selectedVersion && (
                  <div>
                    <Label className="text-sm mb-3 block">Version Details</Label>
                    <div className="bg-muted/30 rounded-lg p-4 border border-border space-y-3">
                      <div className="grid grid-cols-2 gap-3">
                        <div>
                          <p className="text-xs text-muted-foreground mb-0.5">File Size</p>
                          <p className="text-sm font-medium">
                            {formatBytes(selectedVersion.size)}
                          </p>
                        </div>
                        <div>
                          <p className="text-xs text-muted-foreground mb-0.5">Modified</p>
                          <p className="text-sm font-medium">
                            {getRelativeTime(selectedVersion.modifiedAt)}
                          </p>
                        </div>
                        <div>
                          <p className="text-xs text-muted-foreground mb-0.5">Modified By</p>
                          <p className="text-sm font-medium">{selectedVersion.modifiedBy}</p>
                        </div>
                        <div>
                          <p className="text-xs text-muted-foreground mb-0.5">Change Blocks</p>
                          <p className="text-sm font-medium">
                            {selectedVersion.changeBlocks === 0
                              ? 'Current'
                              : selectedVersion.changeBlocks}
                          </p>
                        </div>
                      </div>

                      <Separator />

                      <div>
                        <p className="text-xs text-muted-foreground mb-0.5">Checksum</p>
                        <p className="text-xs font-mono bg-muted/50 rounded px-2 py-1">
                          {selectedVersion.checksum}
                        </p>
                      </div>

                      {timelinePosition < 100 && (
                        <div className="bg-yellow-500/10 border border-yellow-500/30 rounded p-3 mt-3">
                          <div className="flex items-start gap-2">
                            <AlertCircle className="w-4 h-4 text-yellow-600 mt-0.5" />
                            <div>
                              <p className="text-sm font-medium text-yellow-700 dark:text-yellow-400">
                                Restoring Previous Version
                              </p>
                              <p className="text-xs text-muted-foreground mt-1">
                                This will restore the file to its state from{' '}
                                {getRelativeTime(selectedVersion.modifiedAt)}. The current version
                                will be preserved in the version history.
                              </p>
                            </div>
                          </div>
                        </div>
                      )}

                      {selectedFile.isDeleted && timelinePosition < 100 && (
                        <div className="bg-blue-500/10 border border-blue-500/30 rounded p-3">
                          <div className="flex items-start gap-2">
                            <AlertCircle className="w-4 h-4 text-blue-500 mt-0.5" />
                            <div>
                              <p className="text-sm font-medium text-blue-700 dark:text-blue-400">
                                Recovering Deleted File
                              </p>
                              <p className="text-xs text-muted-foreground mt-1">
                                This file has been deleted. Restoring will recreate it at the
                                original path.
                              </p>
                            </div>
                          </div>
                        </div>
                      )}
                    </div>
                  </div>
                )}

                {/* Action Buttons */}
                <div className="flex gap-2">
                  <Button
                    variant="outline"
                    className="flex-1"
                    onClick={() => {
                      setSelectedFile(null);
                      setTimelinePosition(100);
                    }}
                  >
                    Cancel
                  </Button>
                  <Button
                    className="flex-1"
                    onClick={() => setShowRestoreConfirm(true)}
                    disabled={timelinePosition === 100 && !selectedFile.isDeleted}
                  >
                    <RotateCcw className="w-4 h-4 mr-2" />
                    {selectedFile.isDeleted
                      ? 'Recover File'
                      : timelinePosition === 100
                      ? 'Already Current'
                      : 'Restore Version'}
                  </Button>
                </div>
              </>
            )}
          </CardContent>
        </Card>
      </div>

      {/* Restore Operation Log */}
      <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
        <CardHeader>
          <CardTitle className="text-foreground/90 flex items-center gap-2">
            <History className="w-5 h-5" />
            Restore Operations Log
          </CardTitle>
        </CardHeader>
        <CardContent>
          <div className="space-y-2">
            {restoreLogs.map((log) => (
              <div
                key={log.id}
                className={`border rounded-lg p-3 ${
                  log.status === 'failed'
                    ? 'border-red-500/30 bg-red-500/5'
                    : log.status === 'completed'
                    ? 'border-green-500/30 bg-green-500/5'
                    : 'border-blue-500/30 bg-blue-500/5'
                }`}
              >
                <div className="flex items-start gap-3">
                  {getStatusIcon(log.status)}
                  <div className="flex-1 min-w-0">
                    <div className="flex items-center gap-2 mb-1">
                      <p className="text-sm font-medium text-foreground/90 truncate">
                        {log.filepath}
                      </p>
                      {getStatusBadge(log.status)}
                    </div>
                    <p className="text-xs text-muted-foreground mb-2">{log.message}</p>
                    <div className="grid grid-cols-3 gap-3 text-xs text-muted-foreground">
                      <div>
                        <span>Initiated:</span>
                        <span className="ml-1">{getRelativeTime(log.timestamp)}</span>
                      </div>
                      <div>
                        <span>By:</span>
                        <span className="ml-1">{log.initiatedBy}</span>
                      </div>
                      <div>
                        <span>Restore To:</span>
                        <span className="ml-1">{formatTimestamp(log.restoreToDate)}</span>
                      </div>
                    </div>
                  </div>
                </div>
              </div>
            ))}

            {restoreLogs.length === 0 && (
              <div className="text-center py-8 text-muted-foreground">
                <History className="w-10 h-10 mx-auto mb-2 opacity-50" />
                <p className="text-sm">No restore operations yet</p>
                <p className="text-xs mt-1">Restore operations will appear here</p>
              </div>
            )}
          </div>
        </CardContent>
      </Card>

      {/* Restore Confirmation Dialog */}
      {showRestoreConfirm && selectedVersion && selectedFile && (
        <div className="fixed inset-0 bg-black/50 flex items-center justify-center z-50">
          <Card className="w-[500px] shadow-2xl">
            <CardHeader>
              <CardTitle className="text-foreground/90 flex items-center gap-2">
                <AlertCircle className="w-5 h-5 text-yellow-600" />
                Confirm File Restore
              </CardTitle>
            </CardHeader>
            <CardContent className="space-y-4">
              <div className="bg-yellow-500/10 border border-yellow-500/30 rounded p-4">
                <p className="text-sm font-medium mb-2">
                  {selectedFile.isDeleted
                    ? 'You are about to recover a deleted file:'
                    : 'You are about to restore this file:'}
                </p>
                <div className="space-y-2 text-sm">
                  <div>
                    <span className="text-muted-foreground">File:</span>
                    <span className="ml-2 font-medium break-all">{selectedFile.filepath}</span>
                  </div>
                  <div>
                    <span className="text-muted-foreground">Restore to:</span>
                    <span className="ml-2 font-medium">
                      {formatTimestamp(selectedVersion.modifiedAt)}
                    </span>
                  </div>
                  <div>
                    <span className="text-muted-foreground">Modified by:</span>
                    <span className="ml-2 font-medium">{selectedVersion.modifiedBy}</span>
                  </div>
                  <div>
                    <span className="text-muted-foreground">Size:</span>
                    <span className="ml-2 font-medium">{formatBytes(selectedVersion.size)}</span>
                  </div>
                </div>
              </div>

              <p className="text-xs text-muted-foreground">
                {selectedFile.isDeleted
                  ? 'The file will be recreated at its original location. Any file currently at that path will be moved to version history.'
                  : 'The current version will be preserved in the version history. This action is reversible.'}
              </p>

              <div className="flex gap-2">
                <Button
                  variant="outline"
                  className="flex-1"
                  onClick={() => setShowRestoreConfirm(false)}
                >
                  Cancel
                </Button>
                <Button className="flex-1" onClick={handleRestoreFile}>
                  <RotateCcw className="w-4 h-4 mr-2" />
                  Confirm Restore
                </Button>
              </div>
            </CardContent>
          </Card>
        </div>
      )}
    </div>
  );
}
