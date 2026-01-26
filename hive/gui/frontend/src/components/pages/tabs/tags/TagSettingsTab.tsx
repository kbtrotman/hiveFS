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
  Plus,
  Settings,
  AlertCircle,
  Check,
  Trash2,
  Edit,
  Link,
  Unlink,
  FolderTree,
  Shield,
  Users,
  Clock,
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
}

export function TagSettingsTab() {
  const [isCreatingTag, setIsCreatingTag] = useState(false);
  const [newTagName, setNewTagName] = useState('');
  const [newTagDescription, setNewTagDescription] = useState('');
  const [newTagCategory, setNewTagCategory] = useState('general');
  const [newTagColor, setNewTagColor] = useState('#3b82f6');
  const [retentionDays, setRetentionDays] = useState('90');
  const [autoCleanup, setAutoCleanup] = useState('enabled');
  const [tagPermissions, setTagPermissions] = useState('admins-only');
  const [selectedTagForNesting, setSelectedTagForNesting] = useState<string | null>(null);
  const [parentTagToAssign, setParentTagToAssign] = useState<string>('none');

  // Mock tag definitions
  const [tagDefinitions, setTagDefinitions] = useState<TagDefinition[]>([
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
    },
    {
      id: '7',
      name: 'Security-Group',
      description: 'Security team related files',
      category: 'department',
      color: '#ef4444',
      createdAt: '2026-01-02T09:00:00Z',
      createdBy: 'admin@hivefs.com',
      fileCount: 2456,
    },
    {
      id: '8',
      name: 'Confidential',
      description: 'Confidential classification',
      category: 'security-level',
      color: '#dc2626',
      createdAt: '2026-01-01T08:00:00Z',
      createdBy: 'admin@hivefs.com',
      fileCount: 987,
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
    },
  ]);

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

  const colorOptions = [
    '#3b82f6', '#10b981', '#f59e0b', '#ef4444', '#8b5cf6', '#ec4899', '#06b6d4', '#6b7280',
  ];

  const handleCreateTag = () => {
    const newTag: TagDefinition = {
      id: Date.now().toString(),
      name: newTagName,
      description: newTagDescription,
      category: newTagCategory,
      color: newTagColor,
      createdAt: new Date().toISOString(),
      createdBy: 'current.user@hivefs.com',
      fileCount: 0,
    };

    setTagDefinitions([...tagDefinitions, newTag]);
    setNewTagName('');
    setNewTagDescription('');
    setNewTagCategory('general');
    setNewTagColor('#3b82f6');
    setIsCreatingTag(false);
  };

  const handleDeleteTag = (id: string) => {
    setTagDefinitions(tagDefinitions.filter((tag) => tag.id !== id));
  };

  const handleAssignParent = () => {
    if (!selectedTagForNesting || parentTagToAssign === 'none') return;

    setTagDefinitions((tags) =>
      tags.map((tag) => {
        if (tag.id === selectedTagForNesting) {
          return { ...tag, parentTag: parentTagToAssign };
        }
        if (tag.id === parentTagToAssign) {
          return {
            ...tag,
            childTags: [...(tag.childTags || []), selectedTagForNesting],
          };
        }
        return tag;
      })
    );

    setSelectedTagForNesting(null);
    setParentTagToAssign('none');
  };

  const handleUnlinkTag = (tagId: string) => {
    const tag = tagDefinitions.find((t) => t.id === tagId);
    if (!tag?.parentTag) return;

    setTagDefinitions((tags) =>
      tags.map((t) => {
        if (t.id === tagId) {
          return { ...t, parentTag: undefined };
        }
        if (t.id === tag.parentTag) {
          return {
            ...t,
            childTags: t.childTags?.filter((childId) => childId !== tagId),
          };
        }
        return t;
      })
    );
  };

  const getTagById = (id: string) => tagDefinitions.find((t) => t.id === id);

  const getTagPath = (tag: TagDefinition): string => {
    const parts: string[] = [tag.name];
    let current = tag;

    while (current.parentTag) {
      const parent = getTagById(current.parentTag);
      if (!parent) break;
      parts.unshift(parent.name);
      current = parent;
    }

    return parts.join(' > ');
  };

  return (
    <div className="flex h-full w-full flex-col gap-6 overflow-auto bg-gradient-to-br from-background via-primary/5 to-background p-8">
      {/* Page Header */}
      <div>
        <h1 className="text-foreground/80">File Tag Settings</h1>
        <p className="text-muted-foreground text-sm mt-1">
          Configure file tagging system, create tags, and manage tag hierarchies
        </p>
      </div>

      {/* Info Banner */}
      <div className="bg-blue-500/10 border border-blue-500/30 rounded-lg p-4">
        <div className="flex items-start gap-3">
          <AlertCircle className="w-5 h-5 text-blue-500 mt-0.5 flex-shrink-0" />
          <div>
            <p className="text-sm font-medium text-blue-700 dark:text-blue-400 mb-1">
              About File Tagging
            </p>
            <p className="text-xs text-muted-foreground">
              File tags provide an alternative to traditional tree-based file organization. Tags
              can be hierarchical (nested) to create flexible taxonomies, such as location-based
              hierarchies (US-Based → West-Region → Southwest-Division → Texas-Datacenter) or
              project-based structures. Files can have multiple tags, enabling discovery through
              various organizational perspectives.
            </p>
          </div>
        </div>
      </div>

      <div className="grid grid-cols-1 gap-6 lg:grid-cols-2">
        {/* Create Tag Card */}
        <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
          <CardHeader>
            <div className="flex items-center justify-between">
              <CardTitle className="text-foreground/90 flex items-center gap-2">
                <Plus className="w-5 h-5" />
                Create Tag
              </CardTitle>
              {!isCreatingTag && (
                <Button size="sm" onClick={() => setIsCreatingTag(true)} className="text-xs h-8">
                  <Plus className="w-3 h-3 mr-1" />
                  New Tag
                </Button>
              )}
            </div>
          </CardHeader>
          {isCreatingTag && (
            <CardContent className="space-y-4">
              <div>
                <Label htmlFor="tag-name" className="text-sm">
                  Tag Name
                </Label>
                <Input
                  id="tag-name"
                  placeholder="e.g., Marketing-Team"
                  value={newTagName}
                  onChange={(e) => setNewTagName(e.target.value)}
                  className="mt-1.5 text-sm"
                />
              </div>

              <div>
                <Label htmlFor="tag-description" className="text-sm">
                  Description
                </Label>
                <Input
                  id="tag-description"
                  placeholder="Brief description of tag usage"
                  value={newTagDescription}
                  onChange={(e) => setNewTagDescription(e.target.value)}
                  className="mt-1.5 text-sm"
                />
              </div>

              <div>
                <Label htmlFor="tag-category" className="text-sm">
                  Category
                </Label>
                <Select value={newTagCategory} onValueChange={setNewTagCategory}>
                  <SelectTrigger id="tag-category" className="mt-1.5 text-sm">
                    <SelectValue />
                  </SelectTrigger>
                  <SelectContent>
                    {tagCategories.map((cat) => (
                      <SelectItem key={cat.value} value={cat.value}>
                        {cat.label}
                      </SelectItem>
                    ))}
                  </SelectContent>
                </Select>
              </div>

              <div>
                <Label className="text-sm mb-2 block">Color</Label>
                <div className="flex gap-2">
                  {colorOptions.map((color) => (
                    <button
                      key={color}
                      onClick={() => setNewTagColor(color)}
                      className={`w-8 h-8 rounded border-2 transition-all ${
                        newTagColor === color ? 'border-foreground scale-110' : 'border-transparent'
                      }`}
                      style={{ backgroundColor: color }}
                    />
                  ))}
                </div>
              </div>

              <div className="bg-muted/30 rounded p-3">
                <p className="text-xs text-muted-foreground mb-2">Preview</p>
                <Badge
                  variant="outline"
                  className="text-xs"
                  style={{ borderColor: newTagColor, color: newTagColor }}
                >
                  <Tag className="w-3 h-3 mr-1" />
                  {newTagName || 'Tag Name'}
                </Badge>
              </div>

              <div className="flex gap-2">
                <Button variant="outline" onClick={() => setIsCreatingTag(false)} className="flex-1">
                  Cancel
                </Button>
                <Button onClick={handleCreateTag} disabled={!newTagName} className="flex-1">
                  <Check className="w-4 h-4 mr-2" />
                  Create Tag
                </Button>
              </div>
            </CardContent>
          )}
        </Card>

        {/* Tag Nesting Card */}
        <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
          <CardHeader>
            <CardTitle className="text-foreground/90 flex items-center gap-2">
              <FolderTree className="w-5 h-5" />
              Tag Nesting
            </CardTitle>
          </CardHeader>
          <CardContent className="space-y-4">
            <div>
              <Label htmlFor="child-tag" className="text-sm">
                Select Tag to Nest
              </Label>
              <p className="text-xs text-muted-foreground mt-1 mb-2">
                Choose a tag to assign a parent relationship
              </p>
              <Select
                value={selectedTagForNesting || 'none'}
                onValueChange={(value) => setSelectedTagForNesting(value === 'none' ? null : value)}
              >
                <SelectTrigger id="child-tag" className="text-sm">
                  <SelectValue />
                </SelectTrigger>
                <SelectContent>
                  <SelectItem value="none">Select a tag...</SelectItem>
                  {tagDefinitions.map((tag) => (
                    <SelectItem key={tag.id} value={tag.id}>
                      {tag.name}
                    </SelectItem>
                  ))}
                </SelectContent>
              </Select>
            </div>

            {selectedTagForNesting && (
              <>
                <div>
                  <Label htmlFor="parent-tag" className="text-sm">
                    Assign Parent Tag
                  </Label>
                  <Select value={parentTagToAssign} onValueChange={setParentTagToAssign}>
                    <SelectTrigger id="parent-tag" className="text-sm">
                      <SelectValue />
                    </SelectTrigger>
                    <SelectContent>
                      <SelectItem value="none">No parent (top-level)</SelectItem>
                      {tagDefinitions
                        .filter((tag) => tag.id !== selectedTagForNesting)
                        .map((tag) => (
                          <SelectItem key={tag.id} value={tag.id}>
                            {tag.name}
                          </SelectItem>
                        ))}
                    </SelectContent>
                  </Select>
                </div>

                <div className="bg-blue-500/10 border border-blue-500/30 rounded p-3">
                  <p className="text-xs font-medium text-blue-700 dark:text-blue-400 mb-1">
                    Current Hierarchy
                  </p>
                  <p className="text-xs text-muted-foreground">
                    {getTagPath(getTagById(selectedTagForNesting)!)}
                  </p>
                </div>

                <Button
                  onClick={handleAssignParent}
                  disabled={parentTagToAssign === 'none'}
                  className="w-full"
                >
                  <Link className="w-4 h-4 mr-2" />
                  Assign Parent
                </Button>
              </>
            )}

            {!selectedTagForNesting && (
              <div className="text-center py-8 text-muted-foreground">
                <FolderTree className="w-10 h-10 mx-auto mb-2 opacity-50" />
                <p className="text-sm">Select a tag to manage nesting</p>
              </div>
            )}
          </CardContent>
        </Card>
      </div>

      <div className="grid grid-cols-1 gap-6 lg:grid-cols-2">
        {/* Retention Settings Card */}
        <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
          <CardHeader>
            <CardTitle className="text-foreground/90 flex items-center gap-2">
              <Clock className="w-5 h-5" />
              Tag Retention
            </CardTitle>
          </CardHeader>
          <CardContent className="space-y-4">
            <div>
              <Label htmlFor="retention" className="text-sm">
                Unused Tag Retention
              </Label>
              <p className="text-xs text-muted-foreground mt-1 mb-2">
                How long to keep tags with no files assigned
              </p>
              <Select value={retentionDays} onValueChange={setRetentionDays}>
                <SelectTrigger id="retention" className="text-sm">
                  <SelectValue />
                </SelectTrigger>
                <SelectContent>
                  <SelectItem value="never">Never delete</SelectItem>
                  <SelectItem value="30">30 days</SelectItem>
                  <SelectItem value="60">60 days</SelectItem>
                  <SelectItem value="90">90 days</SelectItem>
                  <SelectItem value="180">180 days</SelectItem>
                  <SelectItem value="365">1 year</SelectItem>
                </SelectContent>
              </Select>
            </div>

            <div>
              <Label htmlFor="auto-cleanup" className="text-sm">
                Auto-Cleanup
              </Label>
              <p className="text-xs text-muted-foreground mt-1 mb-2">
                Automatically remove tags with no file associations
              </p>
              <Select value={autoCleanup} onValueChange={setAutoCleanup}>
                <SelectTrigger id="auto-cleanup" className="text-sm">
                  <SelectValue />
                </SelectTrigger>
                <SelectContent>
                  <SelectItem value="enabled">Enabled</SelectItem>
                  <SelectItem value="disabled">Disabled</SelectItem>
                </SelectContent>
              </Select>
            </div>

            <div className="bg-yellow-500/10 border border-yellow-500/30 rounded p-3">
              <div className="flex items-start gap-2">
                <AlertCircle className="w-4 h-4 text-yellow-600 mt-0.5" />
                <p className="text-xs text-muted-foreground">
                  Tags with child tags will never be auto-deleted, even if unused. Parent tags are
                  preserved to maintain hierarchy structure.
                </p>
              </div>
            </div>

            <Button className="w-full">
              <Check className="w-4 h-4 mr-2" />
              Save Retention Settings
            </Button>
          </CardContent>
        </Card>

        {/* Permissions Card */}
        <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
          <CardHeader>
            <CardTitle className="text-foreground/90 flex items-center gap-2">
              <Shield className="w-5 h-5" />
              Tag Permissions
            </CardTitle>
          </CardHeader>
          <CardContent className="space-y-4">
            <div>
              <Label htmlFor="tag-creation" className="text-sm">
                Tag Creation Permissions
              </Label>
              <p className="text-xs text-muted-foreground mt-1 mb-2">
                Who can create new tags
              </p>
              <Select value={tagPermissions} onValueChange={setTagPermissions}>
                <SelectTrigger id="tag-creation" className="text-sm">
                  <SelectValue />
                </SelectTrigger>
                <SelectContent>
                  <SelectItem value="admins-only">Administrators Only</SelectItem>
                  <SelectItem value="power-users">Administrators & Power Users</SelectItem>
                  <SelectItem value="all-users">All Users</SelectItem>
                </SelectContent>
              </Select>
            </div>

            <div className="bg-muted/30 rounded p-3 space-y-2">
              <p className="text-xs font-medium text-foreground/90">Permission Levels</p>
              <div className="space-y-2 text-xs">
                <div className="flex items-start gap-2">
                  <Users className="w-3 h-3 mt-0.5 text-muted-foreground" />
                  <div>
                    <p className="font-medium">All Users</p>
                    <p className="text-muted-foreground">Can apply tags, create new tags</p>
                  </div>
                </div>
                <div className="flex items-start gap-2">
                  <Shield className="w-3 h-3 mt-0.5 text-muted-foreground" />
                  <div>
                    <p className="font-medium">Power Users</p>
                    <p className="text-muted-foreground">Can create tags, manage tag hierarchies</p>
                  </div>
                </div>
                <div className="flex items-start gap-2">
                  <Settings className="w-3 h-3 mt-0.5 text-muted-foreground" />
                  <div>
                    <p className="font-medium">Administrators</p>
                    <p className="text-muted-foreground">Full control over all tag operations</p>
                  </div>
                </div>
              </div>
            </div>

            <div className="bg-blue-500/10 border border-blue-500/30 rounded p-3">
              <p className="text-xs text-muted-foreground">
                <strong>Note:</strong> Regardless of creation permissions, all users can apply
                existing tags to their files. Tag deletion always requires administrator privileges.
              </p>
            </div>

            <Button className="w-full">
              <Check className="w-4 h-4 mr-2" />
              Save Permissions
            </Button>
          </CardContent>
        </Card>
      </div>

      {/* Manage Existing Tags */}
      <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
        <CardHeader>
          <CardTitle className="text-foreground/90 flex items-center gap-2">
            <Tag className="w-5 h-5" />
            Manage Tags ({tagDefinitions.length})
          </CardTitle>
        </CardHeader>
        <CardContent>
          <div className="space-y-2 max-h-[400px] overflow-y-auto">
            {tagDefinitions.map((tag) => (
              <div key={tag.id} className="border rounded-lg p-3 bg-background">
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
                        {tagCategories.find((c) => c.value === tag.category)?.label || tag.category}
                      </Badge>
                      {tag.parentTag && (
                        <Badge variant="outline" className="text-xs">
                          Child of {getTagById(tag.parentTag)?.name}
                        </Badge>
                      )}
                    </div>
                    <p className="text-xs text-muted-foreground mb-2">{tag.description}</p>
                    {tag.parentTag && (
                      <p className="text-xs text-muted-foreground mb-2">
                        <span className="font-medium">Hierarchy:</span> {getTagPath(tag)}
                      </p>
                    )}
                    <div className="flex items-center gap-3 text-xs text-muted-foreground">
                      <span>{tag.fileCount.toLocaleString()} files</span>
                      <span>•</span>
                      <span>Created by {tag.createdBy}</span>
                      {tag.childTags && tag.childTags.length > 0 && (
                        <>
                          <span>•</span>
                          <span>{tag.childTags.length} child tag(s)</span>
                        </>
                      )}
                    </div>
                  </div>
                  <div className="flex gap-1">
                    {tag.parentTag && (
                      <Button
                        size="sm"
                        variant="ghost"
                        className="h-8 w-8 p-0"
                        onClick={() => handleUnlinkTag(tag.id)}
                        title="Unlink from parent"
                      >
                        <Unlink className="w-4 h-4" />
                      </Button>
                    )}
                    <Button
                      size="sm"
                      variant="ghost"
                      className="h-8 w-8 p-0"
                      title="Edit tag"
                    >
                      <Edit className="w-4 h-4" />
                    </Button>
                    <Button
                      size="sm"
                      variant="ghost"
                      className="h-8 w-8 p-0"
                      onClick={() => handleDeleteTag(tag.id)}
                      title="Delete tag"
                    >
                      <Trash2 className="w-4 h-4 text-destructive" />
                    </Button>
                  </div>
                </div>
              </div>
            ))}
          </div>
        </CardContent>
      </Card>
    </div>
  );
}
