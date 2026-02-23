import { useMemo, useState, useCallback, useEffect, type MouseEvent } from 'react';
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from '../../../ui/card';
import { Badge } from '../../../ui/badge';
import { Button } from '../../../ui/button';
import { Input } from '../../../ui/input';
import { Label } from '../../../ui/label';
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '../../../ui/select';
import {
  Activity,
  ChevronDown,
  ChevronRight,
  Folder,
  FolderOpen,
  Link2,
  Globe,
  User,
  Plus,
  Trash2,
  GitMerge,
  Loader2,
  Check,
} from 'lucide-react';
import {
  formatTimestamp,
  resolveStatHealth,
  aggregateStatsByNode,
  useDiskStats,
} from '../../../useDiskStats';

interface ApiTreeNode {
  key: string;
  title: string;
  isLeaf: boolean;
  hasChildren: boolean;
  nodeKind?: string;
  inodeId?: string;
  dentryId?: string;
}

interface DiskTreeNode {
  id: string;
  name: string;
  type: string;
  hasChildren: boolean;
  icon?: typeof Globe;
  children?: DiskTreeNode[];
  junction?: string;
  inodeId?: string;
  dentryId?: string;
}

const NODE_KIND_ICON: Record<string, typeof Globe> = {
  global: Globe,
  client: User,
};

interface TreeNodeProps {
  node: DiskTreeNode;
  level: number;
  onSelect: (node: DiskTreeNode) => void;
  ensureChildren: (node: DiskTreeNode) => void;
  selectedId: string | null;
  loadingState: Record<string, boolean>;
}

function TreeNode({ node, level, onSelect, ensureChildren, selectedId, loadingState }: TreeNodeProps) {
  const [isExpanded, setIsExpanded] = useState(level < 2);
  const hasChildren = node.hasChildren;
  const children = node.children || [];
  const Icon = node.icon || NODE_KIND_ICON[node.type] || (isExpanded ? FolderOpen : Folder);
  const isSelected = selectedId === node.id;

  const handleToggle = (e: MouseEvent<HTMLButtonElement>) => {
    e.stopPropagation();
    const willExpand = !isExpanded;
    setIsExpanded(willExpand);
    if (willExpand) {
      ensureChildren(node);
    }
  };

  useEffect(() => {
    if (isExpanded && hasChildren && (!children || children.length === 0)) {
      ensureChildren(node);
    }
  }, [isExpanded, hasChildren, children, ensureChildren, node]);

  return (
    <div>
      <div
        className={`flex items-center gap-2 py-2 px-3 cursor-pointer hover:bg-muted rounded-md transition-colors ${
          isSelected ? 'bg-primary/10 border border-primary/30' : ''
        }`}
        style={{ paddingLeft: `${level * 1.5 + 0.75}rem` }}
        onClick={() => onSelect(node)}
      >
        {hasChildren ? (
          <button onClick={handleToggle} className="p-0 hover:bg-muted rounded">
            {isExpanded ? <ChevronDown className="w-4 h-4" /> : <ChevronRight className="w-4 h-4" />}
          </button>
        ) : (
          <span className="w-4" />
        )}

        <Icon
          className={`w-4 h-4 ${
            node.type === 'global' ? 'text-blue-500' : node.type === 'client' ? 'text-purple-500' : 'text-muted-foreground'
          }`}
        />
        <span className="text-sm">{node.name}</span>

        {node.junction && <Link2 className="w-3 h-3 text-orange-500 ml-1" title={`Junction to ${node.junction}`} />}

        {node.type === 'global' && (
          <Badge variant="outline" className="ml-2 text-xs">
            Global
          </Badge>
        )}
        {node.type === 'client' && (
          <Badge variant="secondary" className="ml-2 text-xs">
            Client
          </Badge>
        )}
      </div>

      <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
        <CardHeader>
          <CardTitle>Quota Management</CardTitle>
          <CardDescription>
            Assign user or group quotas for the currently selected path.
          </CardDescription>
        </CardHeader>
        <CardContent className="space-y-4">
          {!selectedNode ? (
            <p className="text-sm text-muted-foreground">
              Select a path in the Filesystem Browser to review or assign quotas.
            </p>
          ) : (
            <>
              <div className="rounded-md border border-border p-3 text-sm">
                <p className="text-muted-foreground">Selected Path</p>
                <p className="font-mono text-foreground">{selectedPath}</p>
              </div>
              <div className="grid grid-cols-1 gap-6 lg:grid-cols-2">
                <div className="space-y-4">
                  <div>
                    <Label className="text-sm">Quota Scope</Label>
                    <Select value={quotaScope} onValueChange={(value) => setQuotaScope(value as 'user' | 'group')}>
                      <SelectTrigger className="mt-1">
                        <SelectValue placeholder="Choose scope" />
                      </SelectTrigger>
                      <SelectContent>
                        <SelectItem value="user">User</SelectItem>
                        <SelectItem value="group">Group</SelectItem>
                      </SelectContent>
                    </Select>
                  </div>
                  <div>
                    <Label className="text-sm">
                      {quotaScope === 'user' ? 'Username' : 'Group Name'}
                    </Label>
                    <Input
                      className="mt-1"
                      placeholder={quotaScope === 'user' ? 'e.g. jdoe' : 'e.g. finance-team'}
                      value={quotaTarget}
                      onChange={(e) => setQuotaTarget(e.target.value)}
                    />
                  </div>
                  <div>
                    <Label className="text-sm">Quota Limit (GB)</Label>
                    <Input
                      className="mt-1"
                      type="number"
                      min="1"
                      placeholder="Enter limit"
                      value={quotaLimitGb}
                      onChange={(e) => setQuotaLimitGb(e.target.value)}
                    />
                  </div>
                  <div className="flex flex-wrap items-center gap-2 text-xs text-muted-foreground">
                    <p>Superblock-level quotas supported soon.</p>
                  </div>
                  <div className="pt-2">
                    <Button
                      onClick={handleQuotaSave}
                      disabled={!quotaTarget.trim() || quotaLimitNumber <= 0}
                    >
                      <Check className="w-4 h-4 mr-2" />
                      Save Quota
                    </Button>
                  </div>
                </div>
                <div className="bg-muted/30 rounded-lg p-4 space-y-3 border border-border">
                  <div>
                    <p className="text-sm font-medium">Quota Preview</p>
                    <p className="text-xs text-muted-foreground">
                      Shows sample usage for this path and entity.
                    </p>
                  </div>
                  <div className="flex justify-between text-xs text-muted-foreground">
                    <span>Usage</span>
                    <span>
                      {mockQuotaUsageGb} GB /{' '}
                      {quotaLimitNumber > 0 ? `${quotaLimitNumber} GB` : 'No limit'}
                    </span>
                  </div>
                  <div className="h-2 rounded-full bg-muted">
                    <div
                      className={`h-2 rounded-full ${
                        quotaUsagePercent > 90
                          ? 'bg-destructive'
                          : quotaUsagePercent > 75
                            ? 'bg-amber-500'
                            : 'bg-emerald-500'
                      }`}
                      style={{ width: `${quotaLimitNumber ? quotaUsagePercent : 0}%` }}
                    />
                  </div>
                  <div className="grid grid-cols-2 gap-2 text-xs">
                    <div className="rounded border border-border p-2">
                      <p className="text-muted-foreground">Scope</p>
                      <p className="font-medium capitalize">{quotaScope}</p>
                    </div>
                    <div className="rounded border border-border p-2">
                      <p className="text-muted-foreground">Target</p>
                      <p className="font-medium">{quotaTarget.trim() || '—'}</p>
                    </div>
                    <div className="rounded border border-border p-2">
                      <p className="text-muted-foreground">Warn at</p>
                      <p className="font-medium">{quotaLimitNumber ? `${Math.round(quotaLimitNumber * 0.85)} GB` : '—'}</p>
                    </div>
                    <div className="rounded border border-border p-2">
                      <p className="text-muted-foreground">Status</p>
                      <p
                        className={`font-medium ${
                          quotaUsagePercent > 90
                            ? 'text-red-600'
                            : quotaUsagePercent > 75
                              ? 'text-amber-500'
                              : 'text-emerald-600'
                        }`}
                      >
                        {quotaLimitNumber === 0
                          ? 'Unlimited'
                          : quotaUsagePercent > 90
                            ? 'Critical'
                            : quotaUsagePercent > 75
                              ? 'Warning'
                              : 'Healthy'}
                      </p>
                    </div>
                  </div>
                </div>
              </div>
            </>
          )}
        </CardContent>
      </Card>

      {hasChildren && isExpanded && (
        <div>
          {loadingState[node.id] && children.length === 0 ? (
            <div
              className="flex items-center gap-2 text-sm text-muted-foreground"
              style={{ paddingLeft: `${(level + 1) * 1.5 + 0.75}rem` }}
            >
              <Loader2 className="w-4 h-4 animate-spin" /> Loading...
            </div>
          ) : (
            children.map((child) => (
              <TreeNode
                key={child.id}
                node={child}
                level={level + 1}
                onSelect={onSelect}
                ensureChildren={ensureChildren}
                selectedId={selectedId}
                loadingState={loadingState}
              />
            ))
          )}
        </div>
      )}
    </div>
  );
}

export function ClusterFSTab() {
  const { stats, isLoading, error, lastUpdated } = useDiskStats();
  const aggregated = useMemo(() => aggregateStatsByNode(stats), [stats]);
  const [selectedNode, setSelectedNode] = useState<DiskTreeNode | null>(null);
  const [newPathName, setNewPathName] = useState('');
  const [junctionTarget, setJunctionTarget] = useState('');
  const [treeData, setTreeData] = useState<DiskTreeNode[]>([]);
  const [loadingNodes, setLoadingNodes] = useState<Record<string, boolean>>({});
  const [quotaScope, setQuotaScope] = useState<'user' | 'group'>('user');
  const [quotaTarget, setQuotaTarget] = useState('');
  const [quotaLimitGb, setQuotaLimitGb] = useState('250');
  const mockQuotaUsageGb = 64;
  const quotaLimitNumber = Number(quotaLimitGb) || 0;
  const quotaUsagePercent =
    quotaLimitNumber > 0 ? Math.min(100, (mockQuotaUsageGb / quotaLimitNumber) * 100) : 0;
  const selectedPath = selectedNode ? `/${selectedNode.id.replace(/-/g, '/')}` : null;

  const adaptApiNode = useCallback(
    (node: ApiTreeNode): DiskTreeNode => ({
      id: node.key,
      name: node.title,
      type: (node.nodeKind || 'folder').toLowerCase(),
      hasChildren: node.hasChildren,
      icon: NODE_KIND_ICON[(node.nodeKind || '').toLowerCase()],
      children: node.hasChildren ? [] : undefined,
      inodeId: node.inodeId,
      dentryId: node.dentryId,
    }),
    []
  );

  const mergeChildren = useCallback((parentId: string, children: DiskTreeNode[], current: DiskTreeNode[]): DiskTreeNode[] => {
    if (parentId === 'root') {
      return children;
    }
    return current.map((node) => {
      if (node.id === parentId) {
        return { ...node, children, hasChildren: children.length > 0 };
      }
      if (node.children && node.children.length) {
        return { ...node, children: mergeChildren(parentId, children, node.children) };
      }
      return node;
    });
  }, []);

  const fetchTreeNodes = useCallback(
    async (parentId: string) => {
      setLoadingNodes((prev) => ({ ...prev, [parentId]: true }));
      try {
        const url = parentId === 'root'
          ? 'http://127.0.0.1:8000/api/v1/tree'
          : `http://127.0.0.1:8000/api/v1/tree?parent=${encodeURIComponent(parentId)}`;
        const res = await fetch(url);
        if (!res.ok) {
          throw new Error(`Failed to fetch tree nodes (${res.status})`);
        }
        const data: ApiTreeNode[] = await res.json();
        const mapped = data.map(adaptApiNode);
        setTreeData((prev) => mergeChildren(parentId, mapped, prev));
      } catch (err) {
        console.error('tree fetch failed', err);
      } finally {
        setLoadingNodes((prev) => {
          const next = { ...prev };
          delete next[parentId];
          return next;
        });
      }
    },
    [adaptApiNode, mergeChildren]
  );

  useEffect(() => {
    fetchTreeNodes('root');
  }, [fetchTreeNodes]);

  const ensureChildrenLoaded = useCallback(
    (node: DiskTreeNode) => {
      if (!node.hasChildren)
        return;
      const alreadyLoaded = node.children && node.children.length > 0;
      if (!alreadyLoaded && !loadingNodes[node.id]) {
        fetchTreeNodes(node.id);
      }
    },
    [fetchTreeNodes, loadingNodes]
  );

  const handleQuotaSave = useCallback(() => {
    if (!selectedNode)
      return;
    console.debug('Saving quota assignment', {
      path: selectedNode.id,
      scope: quotaScope,
      target: quotaTarget,
      limitGb: quotaLimitGb,
    });
  }, [quotaScope, quotaTarget, quotaLimitGb, selectedNode]);

  const summary = useMemo(() => {
    const statuses = aggregated.map((stat) => resolveStatHealth(stat));
    const errors = statuses.filter((status) => status === 'error').length;
    const warnings = statuses.filter((status) => status === 'warning').length;
    const overall =
      errors > 0 ? 'Attention required' : warnings > 0 ? 'Warnings detected' : 'Healthy';

    return {
      overall,
      alerts: errors + warnings,
      reporting: aggregated.length,
      lastSample: lastUpdated,
    };
  }, [aggregated, lastUpdated]);

  return (
    <div className="space-y-6">
      <div>
        <h2>Filesystem</h2>
        <p className="text-muted-foreground">Management of virtual filesystems and global structure</p>
      </div>

      <div className="grid grid-cols-1 md:grid-cols-4 gap-4">
        <Card>
          <CardContent className="pt-6">
            <div className="flex items-center justify-between">
              <div>
                <p className="text-sm text-muted-foreground">Number of global Inodes</p>
                <p className="mt-2 text-green-500">
                  {isLoading ? 'Loading…' : summary.overall}
                </p>
              </div>
              <Activity className="w-5 h-5 text-green-500" />
            </div>
          </CardContent>
        </Card>
        <Card>
          <CardContent className="pt-6">
            <p className="text-sm text-muted-foreground">Number of Global Blocks</p>
            <p className="mt-2">{isLoading ? '—' : summary.alerts}</p>
          </CardContent>
        </Card>
        <Card>
          <CardContent className="pt-6">
            <p className="text-sm text-muted-foreground">Number of Erasure Code Stripes</p>
            <p className="mt-2">{isLoading ? '—' : summary.reporting}</p>
          </CardContent>
        </Card>
        <Card>
          <CardContent className="pt-6">
            <p className="text-sm text-muted-foreground">De-dupe Efficiency</p>
            <p className="mt-2">{summary.lastSample ? formatTimestamp(summary.lastSample) : '—'}</p>
          </CardContent>
        </Card>
      </div>

      {error && (
        <div className="rounded-md border border-destructive/40 bg-destructive/10 px-4 py-3 text-sm text-destructive">
          {error}
        </div>
      )}

      <div className="grid grid-cols-1 lg:grid-cols-2 gap-6">
        <Card>
          <CardHeader>
            <CardTitle>Filesystem Browser</CardTitle>
            <CardDescription>Navigate filesystem hierarchy</CardDescription>
          </CardHeader>
          <CardContent>
            <div className="space-y-1 max-h-[500px] overflow-y-auto">
              {treeData.map((node) => (
                <TreeNode
                  key={node.id}
                  node={node}
                  level={0}
                  onSelect={setSelectedNode}
                  ensureChildren={ensureChildrenLoaded}
                  selectedId={selectedNode?.id || null}
                  loadingState={loadingNodes}
                />
              ))}
            </div>
            <div className="mt-4 p-3 bg-muted rounded-md text-sm">
              <div className="flex items-center gap-2 mb-2">
                <Globe className="w-4 h-4 text-blue-500" />
                <span>
                  <strong>Global Shared:</strong> Accessible by all clients
                </span>
              </div>
              <div className="flex items-center gap-2 mb-2">
                <User className="w-4 h-4 text-purple-500" />
                <span>
                  <strong>Client:</strong> Private to specific client
                </span>
              </div>
              <div className="flex items-center gap-2">
                <Link2 className="w-4 h-4 text-orange-500" />
                <span>
                  <strong>Junction:</strong> Links to another path
                </span>
              </div>
            </div>
          </CardContent>
        </Card>

        <Card>
          <CardHeader>
            <CardTitle>Path Administration</CardTitle>
            <CardDescription>Manage paths, junctions, and access control</CardDescription>
          </CardHeader>
          <CardContent className="space-y-6">
            {selectedNode ? (
              <>
                <div className="p-4 bg-muted rounded-lg space-y-2">
                  <div className="flex items-center justify-between">
                    <span className="text-sm">Selected Path:</span>
                    <Badge variant={selectedNode.type === 'global' ? 'default' : 'secondary'}>
                      {selectedNode.type === 'global'
                        ? 'Global'
                        : selectedNode.type === 'client'
                          ? 'Client'
                          : 'Folder'}
                    </Badge>
                  </div>
                  <p className="font-mono text-sm">/{selectedNode.id.replace(/-/g, '/')}</p>
                  {selectedNode.junction && (
                    <div className="flex items-center gap-2 text-sm text-orange-500 pt-2 border-t border-border">
                      <GitMerge className="w-4 h-4" />
                      <span>Junctioned to: {selectedNode.junction}</span>
                    </div>
                  )}
                </div>

                <div className="space-y-3">
                  <Label>Add New Path</Label>
                  <div className="flex gap-2">
                    <Input
                      placeholder="Enter folder name"
                      value={newPathName}
                      onChange={(e) => setNewPathName(e.target.value)}
                    />
                    <Button>
                      <Plus className="w-4 h-4 mr-2" />
                      Add
                    </Button>
                  </div>
                  <p className="text-xs text-muted-foreground">Creates a new directory under the selected path</p>
                </div>

                <div className="space-y-3 pt-3 border-t border-border">
                  <Label className="flex items-center gap-2">
                    <GitMerge className="w-4 h-4" />
                    Create Junction
                  </Label>
                  <div className="space-y-2">
                    <Select value={junctionTarget} onValueChange={setJunctionTarget}>
                      <SelectTrigger>
                        <SelectValue placeholder="Select target path" />
                      </SelectTrigger>
                      <SelectContent>
                        <SelectItem value="/global/data/shared">/global/data/shared</SelectItem>
                        <SelectItem value="/global/data/public">/global/data/public</SelectItem>
                        <SelectItem value="/client-01/home">/client-01/home</SelectItem>
                        <SelectItem value="/client-02/workspace">/client-02/workspace</SelectItem>
                      </SelectContent>
                    </Select>
                    <Button variant="outline" className="w-full">
                      <Link2 className="w-4 h-4 mr-2" />
                      Create Junction Link
                    </Button>
                  </div>
                  <div className="p-3 bg-blue-50 dark:bg-blue-950/20 rounded-md text-xs">
                    <strong>Junction:</strong> Makes the target path accessible from this location.
                  </div>
                </div>

                {selectedNode.type === 'folder' && (
                  <div className="space-y-3 pt-3 border-t border-border">
                    <Label>Access Control</Label>
                    <div className="flex gap-2">
                      <Button variant="outline" className="flex-1">
                        <Globe className="w-4 h-4 mr-2" />
                        Make Global
                      </Button>
                      <Button variant="outline" className="flex-1">
                        <User className="w-4 h-4 mr-2" />
                        Make Private
                      </Button>
                    </div>
                    <p className="text-xs text-muted-foreground">
                      Change whether this path is shared globally or client-specific
                    </p>
                  </div>
                )}

                <div className="pt-3 border-t border-border">
                  <Button variant="destructive" className="w-full">
                    <Trash2 className="w-4 h-4 mr-2" />
                    Remove Path
                  </Button>
                  <p className="text-xs text-muted-foreground mt-2">
                    Permanently removes this path and all contents
                  </p>
                </div>
              </>
            ) : (
              <p className="text-sm text-muted-foreground">Select a path from the browser to manage it.</p>
            )}
          </CardContent>
        </Card>
      </div>
      
    </div>
  );
}
