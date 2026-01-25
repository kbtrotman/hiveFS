import { useCallback, useEffect, useState, type MouseEvent } from 'react';
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from '../../../ui/card';
import { Button } from '../../../ui/button';
import { Input } from '../../../ui/input';
import { Label } from '../../../ui/label';
import { Badge } from '../../../ui/badge';
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '../../../ui/select';
import {
  HardDrive,
  Database,
  Archive,
  ChevronRight,
  ChevronDown,
  Folder,
  FolderOpen,
  Link2,
  Globe,
  User,
  Plus,
  Trash2,
  GitMerge,
  Loader2,
} from 'lucide-react';

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
  onToggle: (node: DiskTreeNode) => void;
  selectedId: string | null;
  loadingState: Record<string, boolean>;
}

function TreeNode({ node, level, onSelect, onToggle, selectedId, loadingState }: TreeNodeProps) {
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
      onToggle(node);
    }
  };

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
                onToggle={onToggle}
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

export function DiskManageTab() {
  const [selectedNode, setSelectedNode] = useState<DiskTreeNode | null>(null);
  const [newPathName, setNewPathName] = useState('');
  const [junctionTarget, setJunctionTarget] = useState('');
  const [treeData, setTreeData] = useState<DiskTreeNode[]>([]);
  const [loadingNodes, setLoadingNodes] = useState<Record<string, boolean>>({});

  const adaptApiNode = (node: ApiTreeNode): DiskTreeNode => ({
    id: node.key,
    name: node.title,
    type: (node.nodeKind || 'folder').toLowerCase(),
    hasChildren: node.hasChildren,
    icon: NODE_KIND_ICON[(node.nodeKind || '').toLowerCase()],
    children: node.hasChildren ? [] : undefined,
    inodeId: node.inodeId,
    dentryId: node.dentryId,
  });

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
    [mergeChildren]
  );

  useEffect(() => {
    fetchTreeNodes('root');
  }, [fetchTreeNodes]);

  const handleToggle = useCallback(
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

  return (
    <div className="space-y-6">
      <div className="flex justify-between items-center">
        <div>
          <h2>Disk Management</h2>
          <p className="text-muted-foreground">Monitor storage volumes and manage filesystem paths</p>
        </div>
        <Button>Add Volume</Button>
      </div>

      {/* Top 4 cards */}
      <div className="grid grid-cols-1 md:grid-cols-4 gap-4">
        <Card>
          <CardContent className="pt-6 relative">
            <div className="absolute inset-x-0 top-0 h-px bg-gradient-to-r from-transparent via-cyan-400/40 to-transparent" />
            <div className="flex items-center justify-between">
              <div>
                <p className="text-sm text-muted-foreground">Total Storage</p>
                <p className="mt-2">4.9 TB</p>
              </div>
              <HardDrive className="w-5 h-5 text-blue-400" />
            </div>
          </CardContent>
        </Card>
        <Card>
          <CardContent className="pt-6 relative">
            <div className="absolute inset-x-0 top-0 h-px bg-gradient-to-r from-transparent via-green-400/40 to-transparent" />
            <div className="flex items-center justify-between">
              <div>
                <p className="text-sm text-muted-foreground">Used Space</p>
                <p className="mt-2">3.6 TB</p>
              </div>
              <Database className="w-5 h-5 text-green-400" />
            </div>
          </CardContent>
        </Card>
        <Card>
          <CardContent className="pt-6 relative">
            <div className="absolute inset-x-0 top-0 h-px bg-gradient-to-r from-transparent via-purple-400/40 to-transparent" />
            <div className="flex items-center justify-between">
              <div>
                <p className="text-sm text-muted-foreground">Available</p>
                <p className="mt-2">1.3 TB</p>
              </div>
              <Archive className="w-5 h-5 text-purple-400" />
            </div>
          </CardContent>
        </Card>
        <Card>
          <CardContent className="pt-6 relative">
            <div className="absolute inset-x-0 top-0 h-px bg-gradient-to-r from-transparent via-teal-400/40 to-transparent" />
            <p className="text-sm text-muted-foreground">Utilization</p>
            <p className="mt-2 text-3xl font-semibold">73%</p>
            <div className="mt-2 h-1.5 rounded-full bg-muted">
              <div className="h-full rounded-full bg-gradient-to-r from-sky-400 via-blue-500 to-teal-400" style={{ width: '73%' }} />
            </div>
          </CardContent>
        </Card>
      </div>

      {/* Bottom 2 cards - Disk Health first, then Recent Activity */}
      <div className="grid grid-cols-1 lg:grid-cols-2 gap-6">
        <Card>
          <CardHeader>
            <CardTitle>Disk Health</CardTitle>
            <CardDescription>SMART status and disk diagnostics</CardDescription>
          </CardHeader>
          <CardContent className="space-y-3">
            <div className="flex justify-between items-center">
              <span className="text-sm text-muted-foreground">Overall Health</span>
              <span className="text-green-500">Good</span>
            </div>
            <div className="flex justify-between items-center">
              <span className="text-sm text-muted-foreground">Failed Sectors</span>
              <span>0</span>
            </div>
            <div className="flex justify-between items-center">
              <span className="text-sm text-muted-foreground">Temperature</span>
              <span>42°C</span>
            </div>
            <div className="flex justify-between items-center">
              <span className="text-sm text-muted-foreground">Power-On Hours</span>
              <span>12,450</span>
            </div>
            <div className="flex justify-between items-center">
              <span className="text-sm text-muted-foreground">Last Check</span>
              <span>2 hours ago</span>
            </div>
          </CardContent>
        </Card>

        <Card>
          <CardHeader>
            <CardTitle>Recent Activity</CardTitle>
            <CardDescription>Recent disk operations</CardDescription>
          </CardHeader>
          <CardContent>
            <div className="space-y-4">
              {[
                { action: 'Write', file: 'dataset_2024_12.hive', size: '2.3 GB', time: '2 min ago' },
                { action: 'Delete', file: 'temp_cache_old.dat', size: '450 MB', time: '15 min ago' },
                { action: 'Write', file: 'user_profile_sync.hive', size: '128 MB', time: '32 min ago' },
                { action: 'Read', file: 'analytics_data.hive', size: '5.2 GB', time: '1 hour ago' },
              ].map((activity, index) => (
                <div key={index} className="flex items-center justify-between py-2 border-b border-border last:border-0">
                  <div>
                    <p className="text-sm">{activity.file}</p>
                    <p className="text-xs text-muted-foreground">{activity.time}</p>
                  </div>
                  <div className="text-right">
                    <p className="text-sm">{activity.size}</p>
                    <p className="text-xs text-muted-foreground">{activity.action}</p>
                  </div>
                </div>
              ))}
            </div>
          </CardContent>
        </Card>
      </div>

      {/* Filesystem Browser and Admin Panel */}
      <div className="grid grid-cols-1 lg:grid-cols-2 gap-6">
        {/* Left: Tree View */}
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
                  onToggle={handleToggle}
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

        {/* Right: Admin Panel */}
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
              <div className="flex flex-col items-center justify-center py-12 text-center text-muted-foreground">
                <Folder className="w-12 h-12 mb-3 opacity-50" />
                <p>Select a path from the tree to manage it</p>
                <p className="text-sm mt-1">You can add paths, create junctions, or remove existing paths</p>
              </div>
            )}
          </CardContent>
        </Card>
      </div>
    </div>
  );
}
