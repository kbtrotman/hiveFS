import { useState } from 'react';
import { Card, CardContent, CardHeader, CardTitle } from '../../../ui/card';
import { Input } from '../../../ui/input';
import { Label } from '../../../ui/label';
import { Button } from '../../../ui/button';
import { Separator } from '../../../ui/separator';
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '../../../ui/select';
import { Badge } from '../../../ui/badge';
import {
  Tag,
  Search,
  TrendingUp,
  BarChart3,
  Layers,
  FolderTree,
  ChevronRight,
  ChevronDown,
  File,
  Users,
  Calendar,
  Filter,
  Eye,
} from 'lucide-react';

interface TagDefinition {
  id: string;
  name: string;
  description: string;
  category: string;
  color: string;
  createdAt: string;
  createdBy: string;
  parentTag?: string;
  childTags?: string[];
  fileCount: number;
  lastUsed?: string;
}

interface TagNode {
  tag: TagDefinition;
  children: TagNode[];
}

export function FileTaggingTab() {
  const [searchQuery, setSearchQuery] = useState('');
  const [filterCategory, setFilterCategory] = useState('all');
  const [sortBy, setSortBy] = useState('usage');
  const [expandedTags, setExpandedTags] = useState<string[]>(['1', '2', '4', '5']);

  // Mock tag definitions (same as settings)
  const tagDefinitions: TagDefinition[] = [
    {
      id: '1',
      name: 'US-Based',
      description: 'Files originating from United States locations',
      category: 'location',
      color: '#3b82f6',
      createdAt: '2026-01-01T10:00:00Z',
      createdBy: 'admin@hivefs.com',
      childTags: ['2', '3'],
      fileCount: 15234,
      lastUsed: '2026-01-25T14:30:00Z',
    },
    {
      id: '2',
      name: 'West-Region',
      description: 'Western United States region',
      category: 'location',
      color: '#3b82f6',
      createdAt: '2026-01-01T10:05:00Z',
      createdBy: 'admin@hivefs.com',
      parentTag: '1',
      childTags: ['4'],
      fileCount: 8932,
      lastUsed: '2026-01-25T13:15:00Z',
    },
    {
      id: '3',
      name: 'Southeast-Region',
      description: 'Southeastern United States region',
      category: 'location',
      color: '#3b82f6',
      createdAt: '2026-01-01T10:06:00Z',
      createdBy: 'admin@hivefs.com',
      parentTag: '1',
      fileCount: 6302,
      lastUsed: '2026-01-25T10:20:00Z',
    },
    {
      id: '4',
      name: 'Southwest-Division',
      description: 'Southwest division of West region',
      category: 'location',
      color: '#3b82f6',
      createdAt: '2026-01-01T10:10:00Z',
      createdBy: 'admin@hivefs.com',
      parentTag: '2',
      childTags: ['5'],
      fileCount: 4521,
      lastUsed: '2026-01-25T12:00:00Z',
    },
    {
      id: '5',
      name: 'Texas-Datacenter',
      description: 'Texas datacenter location',
      category: 'location',
      color: '#3b82f6',
      createdAt: '2026-01-01T10:15:00Z',
      createdBy: 'admin@hivefs.com',
      parentTag: '4',
      childTags: ['6'],
      fileCount: 3012,
      lastUsed: '2026-01-25T11:30:00Z',
    },
    {
      id: '6',
      name: 'Longhorn-Project',
      description: 'Project Longhorn files',
      category: 'project',
      color: '#f59e0b',
      createdAt: '2026-01-05T14:00:00Z',
      createdBy: 'project.manager@hivefs.com',
      parentTag: '5',
      fileCount: 1847,
      lastUsed: '2026-01-25T09:45:00Z',
    },
    {
      id: '7',
      name: 'Security-Group',
      description: 'Security team related files',
      category: 'department',
      color: '#10b981',
      createdAt: '2026-01-02T09:00:00Z',
      createdBy: 'admin@hivefs.com',
      fileCount: 2456,
      lastUsed: '2026-01-25T08:00:00Z',
    },
    {
      id: '8',
      name: 'Confidential',
      description: 'Confidential classification',
      category: 'security-level',
      color: '#ef4444',
      createdAt: '2026-01-01T08:00:00Z',
      createdBy: 'admin@hivefs.com',
      fileCount: 987,
      lastUsed: '2026-01-24T16:20:00Z',
    },
    {
      id: '9',
      name: 'Q4-2025',
      description: 'Fourth quarter 2025 files',
      category: 'time-period',
      color: '#8b5cf6',
      createdAt: '2025-10-01T00:00:00Z',
      createdBy: 'admin@hivefs.com',
      fileCount: 5432,
      lastUsed: '2026-01-23T14:00:00Z',
    },
    {
      id: '10',
      name: 'Marketing-Team',
      description: 'Marketing department files',
      category: 'department',
      color: '#10b981',
      createdAt: '2026-01-03T11:00:00Z',
      createdBy: 'admin@hivefs.com',
      fileCount: 3421,
      lastUsed: '2026-01-25T15:00:00Z',
    },
    {
      id: '11',
      name: 'Client-Acme',
      description: 'Acme Corporation client files',
      category: 'client',
      color: '#ec4899',
      createdAt: '2026-01-10T09:00:00Z',
      createdBy: 'sales@hivefs.com',
      fileCount: 1234,
      lastUsed: '2026-01-25T13:30:00Z',
    },
    {
      id: '12',
      name: 'In-Progress',
      description: 'Work in progress',
      category: 'status',
      color: '#06b6d4',
      createdAt: '2026-01-05T10:00:00Z',
      createdBy: 'admin@hivefs.com',
      fileCount: 4567,
      lastUsed: '2026-01-25T14:45:00Z',
    },
  ];

  const tagCategories = [
    { value: 'general', label: 'General', color: '#6b7280' },
    { value: 'location', label: 'Location', color: '#3b82f6' },
    { value: 'department', label: 'Department', color: '#10b981' },
    { value: 'project', label: 'Project', color: '#f59e0b' },
    { value: 'security-level', label: 'Security Level', color: '#ef4444' },
    { value: 'time-period', label: 'Time Period', color: '#8b5cf6' },
    { value: 'client', label: 'Client', color: '#ec4899' },
    { value: 'status', label: 'Status', color: '#06b6d4' },
  ];

  // Calculate statistics
  const totalTags = tagDefinitions.length;
  const totalTaggedFiles = tagDefinitions.reduce((sum, tag) => sum + tag.fileCount, 0);
  const mostUsedTag = tagDefinitions.reduce((max, tag) =>
    tag.fileCount > max.fileCount ? tag : max
  );
  const categoryCounts = tagCategories.map((cat) => ({
    ...cat,
    count: tagDefinitions.filter((tag) => tag.category === cat.value).length,
  }));
  const nestedTagsCount = tagDefinitions.filter((tag) => tag.parentTag).length;

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

  const toggleExpanded = (tagId: string) => {
    setExpandedTags((prev) =>
      prev.includes(tagId) ? prev.filter((id) => id !== tagId) : [...prev, tagId]
    );
  };

  // Build hierarchical tree
  const buildTagTree = (): TagNode[] => {
    const tagMap = new Map<string, TagNode>();
    const rootNodes: TagNode[] = [];

    // Initialize nodes
    tagDefinitions.forEach((tag) => {
      tagMap.set(tag.id, { tag, children: [] });
    });

    // Build tree
    tagDefinitions.forEach((tag) => {
      const node = tagMap.get(tag.id)!;
      if (tag.parentTag) {
        const parent = tagMap.get(tag.parentTag);
        if (parent) {
          parent.children.push(node);
        } else {
          rootNodes.push(node);
        }
      } else {
        rootNodes.push(node);
      }
    });

    return rootNodes;
  };

  const renderTagTree = (node: TagNode, depth: number = 0): JSX.Element => {
    const isExpanded = expandedTags.includes(node.tag.id);
    const hasChildren = node.children.length > 0;

    return (
      <div key={node.tag.id}>
        <div
          className="flex items-center gap-2 py-2 px-3 rounded hover:bg-muted/50 transition-colors"
          style={{ paddingLeft: `${depth * 24 + 12}px` }}
        >
          {hasChildren ? (
            <button
              onClick={() => toggleExpanded(node.tag.id)}
              className="p-0 h-4 w-4 flex-shrink-0"
            >
              {isExpanded ? (
                <ChevronDown className="w-4 h-4" />
              ) : (
                <ChevronRight className="w-4 h-4" />
              )}
            </button>
          ) : (
            <div className="w-4 flex-shrink-0" />
          )}
          <Badge
            variant="outline"
            className="text-xs"
            style={{ borderColor: node.tag.color, color: node.tag.color }}
          >
            <Tag className="w-3 h-3 mr-1" />
            {node.tag.name}
          </Badge>
          <span className="text-xs text-muted-foreground">
            {node.tag.fileCount.toLocaleString()} files
          </span>
          {hasChildren && (
            <span className="text-xs text-muted-foreground">
              ({node.children.length} child{node.children.length !== 1 ? 'ren' : ''})
            </span>
          )}
        </div>
        {isExpanded && hasChildren && (
          <div>{node.children.map((child) => renderTagTree(child, depth + 1))}</div>
        )}
      </div>
    );
  };

  // Filter and sort tags
  const filteredTags = tagDefinitions
    .filter((tag) => {
      const matchesSearch =
        tag.name.toLowerCase().includes(searchQuery.toLowerCase()) ||
        tag.description.toLowerCase().includes(searchQuery.toLowerCase());
      const matchesCategory = filterCategory === 'all' || tag.category === filterCategory;
      return matchesSearch && matchesCategory;
    })
    .sort((a, b) => {
      switch (sortBy) {
        case 'usage':
          return b.fileCount - a.fileCount;
        case 'name':
          return a.name.localeCompare(b.name);
        case 'recent':
          return new Date(b.lastUsed || b.createdAt).getTime() - new Date(a.lastUsed || a.createdAt).getTime();
        default:
          return 0;
      }
    });

  const tagTree = buildTagTree();

  return (
    <div className="flex h-full w-full flex-col gap-6 overflow-auto bg-gradient-to-br from-background via-primary/5 to-background p-8">
      {/* Page Header */}
      <div>
        <h1 className="text-foreground/80">File Tag Statistics</h1>
        <p className="text-muted-foreground text-sm mt-1">
          View tag usage statistics, hierarchies, and manage tag organization
        </p>
      </div>

      {/* Metrics Strip */}
      <div className="grid grid-cols-4 gap-4">
        <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
          <CardContent className="pt-4">
            <div className="flex items-center justify-between">
              <div>
                <p className="text-xs text-muted-foreground">Total Tags</p>
                <p className="text-2xl font-semibold mt-1">{totalTags}</p>
              </div>
              <Tag className="w-8 h-8 text-blue-500 opacity-50" />
            </div>
          </CardContent>
        </Card>

        <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
          <CardContent className="pt-4">
            <div className="flex items-center justify-between">
              <div>
                <p className="text-xs text-muted-foreground">Tagged Files</p>
                <p className="text-2xl font-semibold mt-1">{totalTaggedFiles.toLocaleString()}</p>
              </div>
              <File className="w-8 h-8 text-green-500 opacity-50" />
            </div>
          </CardContent>
        </Card>

        <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
          <CardContent className="pt-4">
            <div className="flex items-center justify-between">
              <div>
                <p className="text-xs text-muted-foreground">Nested Tags</p>
                <p className="text-2xl font-semibold mt-1">{nestedTagsCount}</p>
              </div>
              <Layers className="w-8 h-8 text-purple-500 opacity-50" />
            </div>
          </CardContent>
        </Card>

        <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
          <CardContent className="pt-4">
            <div className="flex items-center justify-between">
              <div>
                <p className="text-xs text-muted-foreground">Most Used</p>
                <p className="text-lg font-semibold mt-1 truncate" title={mostUsedTag.name}>
                  {mostUsedTag.name}
                </p>
                <p className="text-xs text-muted-foreground">
                  {mostUsedTag.fileCount.toLocaleString()} files
                </p>
              </div>
              <TrendingUp className="w-8 h-8 text-orange-500 opacity-50" />
            </div>
          </CardContent>
        </Card>
      </div>

      {/* Category Distribution */}
      <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
        <CardHeader>
          <CardTitle className="text-foreground/90 flex items-center gap-2">
            <BarChart3 className="w-5 h-5" />
            Tag Categories Distribution
          </CardTitle>
        </CardHeader>
        <CardContent>
          <div className="grid grid-cols-4 gap-4">
            {categoryCounts
              .filter((cat) => cat.count > 0)
              .map((cat) => (
                <div
                  key={cat.value}
                  className="border rounded-lg p-3 bg-muted/20"
                  style={{ borderColor: cat.color + '40' }}
                >
                  <div className="flex items-center justify-between mb-2">
                    <Badge
                      variant="outline"
                      className="text-xs"
                      style={{ borderColor: cat.color, color: cat.color }}
                    >
                      {cat.label}
                    </Badge>
                    <span className="text-lg font-semibold">{cat.count}</span>
                  </div>
                  <div className="w-full bg-muted rounded-full h-2">
                    <div
                      className="h-2 rounded-full transition-all"
                      style={{
                        width: `${(cat.count / totalTags) * 100}%`,
                        backgroundColor: cat.color,
                      }}
                    />
                  </div>
                  <p className="text-xs text-muted-foreground mt-1">
                    {((cat.count / totalTags) * 100).toFixed(1)}% of tags
                  </p>
                </div>
              ))}
          </div>
        </CardContent>
      </Card>

      <div className="grid grid-cols-2 gap-6">
        {/* Tag Hierarchy Tree */}
        <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
          <CardHeader>
            <CardTitle className="text-foreground/90 flex items-center gap-2">
              <FolderTree className="w-5 h-5" />
              Tag Hierarchy
            </CardTitle>
          </CardHeader>
          <CardContent>
            <div className="border border-border rounded bg-muted/20 max-h-[500px] overflow-y-auto">
              {tagTree.map((node) => renderTagTree(node))}
            </div>
            <p className="text-xs text-muted-foreground mt-3">
              Click chevrons to expand/collapse nested tags. This view shows the hierarchical
              relationships between tags.
            </p>
          </CardContent>
        </Card>

        {/* All Tags List */}
        <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
          <CardHeader>
            <CardTitle className="text-foreground/90 flex items-center gap-2">
              <Tag className="w-5 h-5" />
              All Tags
            </CardTitle>
          </CardHeader>
          <CardContent className="space-y-4">
            {/* Filters */}
            <div className="grid grid-cols-3 gap-3">
              <div>
                <Label htmlFor="search-tags" className="text-xs text-muted-foreground">
                  Search
                </Label>
                <div className="relative mt-1">
                  <Search className="absolute left-2 top-1/2 -translate-y-1/2 w-4 h-4 text-muted-foreground" />
                  <Input
                    id="search-tags"
                    placeholder="Search tags..."
                    value={searchQuery}
                    onChange={(e) => setSearchQuery(e.target.value)}
                    className="pl-8 text-sm h-8"
                  />
                </div>
              </div>

              <div>
                <Label htmlFor="filter-category" className="text-xs text-muted-foreground">
                  Category
                </Label>
                <Select value={filterCategory} onValueChange={setFilterCategory}>
                  <SelectTrigger id="filter-category" className="mt-1 text-sm h-8">
                    <SelectValue />
                  </SelectTrigger>
                  <SelectContent>
                    <SelectItem value="all">All Categories</SelectItem>
                    {tagCategories.map((cat) => (
                      <SelectItem key={cat.value} value={cat.value}>
                        {cat.label}
                      </SelectItem>
                    ))}
                  </SelectContent>
                </Select>
              </div>

              <div>
                <Label htmlFor="sort-by" className="text-xs text-muted-foreground">
                  Sort By
                </Label>
                <Select value={sortBy} onValueChange={setSortBy}>
                  <SelectTrigger id="sort-by" className="mt-1 text-sm h-8">
                    <SelectValue />
                  </SelectTrigger>
                  <SelectContent>
                    <SelectItem value="usage">Most Used</SelectItem>
                    <SelectItem value="name">Name (A-Z)</SelectItem>
                    <SelectItem value="recent">Recently Used</SelectItem>
                  </SelectContent>
                </Select>
              </div>
            </div>

            <Separator />

            {/* Tag List */}
            <div className="space-y-2 max-h-[400px] overflow-y-auto">
              {filteredTags.map((tag) => (
                <div key={tag.id} className="border rounded p-3 bg-background hover:bg-muted/20 transition-colors">
                  <div className="flex items-start justify-between">
                    <div className="flex-1">
                      <div className="flex items-center gap-2 mb-1">
                        <Badge
                          variant="outline"
                          className="text-xs"
                          style={{ borderColor: tag.color, color: tag.color }}
                        >
                          <Tag className="w-3 h-3 mr-1" />
                          {tag.name}
                        </Badge>
                        <Badge variant="secondary" className="text-xs">
                          {tagCategories.find((c) => c.value === tag.category)?.label}
                        </Badge>
                        {tag.parentTag && (
                          <Badge variant="outline" className="text-xs">
                            Nested
                          </Badge>
                        )}
                      </div>
                      <p className="text-xs text-muted-foreground mb-2">{tag.description}</p>
                      <div className="flex items-center gap-3 text-xs text-muted-foreground">
                        <div className="flex items-center gap-1">
                          <File className="w-3 h-3" />
                          <span>{tag.fileCount.toLocaleString()} files</span>
                        </div>
                        <span>•</span>
                        <div className="flex items-center gap-1">
                          <Users className="w-3 h-3" />
                          <span>{tag.createdBy}</span>
                        </div>
                        <span>•</span>
                        <div className="flex items-center gap-1">
                          <Calendar className="w-3 h-3" />
                          <span>Used {getRelativeTime(tag.lastUsed || tag.createdAt)}</span>
                        </div>
                      </div>
                    </div>
                    <Button size="sm" variant="ghost" className="h-8 w-8 p-0" title="View files">
                      <Eye className="w-4 h-4" />
                    </Button>
                  </div>
                </div>
              ))}

              {filteredTags.length === 0 && (
                <div className="text-center py-8 text-muted-foreground">
                  <Tag className="w-10 h-10 mx-auto mb-2 opacity-50" />
                  <p className="text-sm">No tags found</p>
                  <p className="text-xs mt-1">Try adjusting your filters</p>
                </div>
              )}
            </div>

            <p className="text-xs text-muted-foreground">
              Showing {filteredTags.length} of {totalTags} tags
            </p>
          </CardContent>
        </Card>
      </div>

      {/* Top Tags by Usage */}
      <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
        <CardHeader>
          <CardTitle className="text-foreground/90 flex items-center gap-2">
            <TrendingUp className="w-5 h-5" />
            Top Tags by Usage
          </CardTitle>
        </CardHeader>
        <CardContent>
          <div className="space-y-3">
            {tagDefinitions
              .sort((a, b) => b.fileCount - a.fileCount)
              .slice(0, 10)
              .map((tag, index) => (
                <div key={tag.id} className="flex items-center gap-3">
                  <div className="w-8 h-8 rounded-full bg-muted flex items-center justify-center flex-shrink-0">
                    <span className="text-sm font-semibold text-muted-foreground">
                      {index + 1}
                    </span>
                  </div>
                  <Badge
                    variant="outline"
                    className="text-xs"
                    style={{ borderColor: tag.color, color: tag.color }}
                  >
                    <Tag className="w-3 h-3 mr-1" />
                    {tag.name}
                  </Badge>
                  <div className="flex-1">
                    <div className="w-full bg-muted rounded-full h-2">
                      <div
                        className="h-2 rounded-full transition-all"
                        style={{
                          width: `${(tag.fileCount / mostUsedTag.fileCount) * 100}%`,
                          backgroundColor: tag.color,
                        }}
                      />
                    </div>
                  </div>
                  <span className="text-sm font-medium w-24 text-right">
                    {tag.fileCount.toLocaleString()} files
                  </span>
                  <span className="text-xs text-muted-foreground w-16 text-right">
                    {((tag.fileCount / totalTaggedFiles) * 100).toFixed(1)}%
                  </span>
                </div>
              ))}
          </div>
        </CardContent>
      </Card>
    </div>
  );
}
