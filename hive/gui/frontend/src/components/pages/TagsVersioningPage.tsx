import { Card, CardContent, CardDescription, CardHeader, CardTitle } from '../ui/card';
import { Button } from '../ui/button';
import { Badge } from '../ui/badge';
import { Input } from '../ui/input';
import { Tag, GitBranch, History, Plus } from 'lucide-react';

const tags = [
  { name: 'production', color: 'bg-green-500', files: 1234, created: '2024-01-15' },
  { name: 'staging', color: 'bg-blue-500', files: 856, created: '2024-02-20' },
  { name: 'development', color: 'bg-purple-500', files: 2341, created: '2024-03-10' },
  { name: 'archive', color: 'bg-gray-500', files: 5678, created: '2023-12-01' },
  { name: 'backup', color: 'bg-yellow-500', files: 3421, created: '2024-01-01' },
];

const versions = [
  { file: '/data/config/system.json', version: 'v3.2.1', size: '2.4 KB', modified: '2 hours ago', user: 'admin' },
  { file: '/data/schemas/user.schema', version: 'v1.8.0', size: '5.1 KB', modified: '5 hours ago', user: 'operator1' },
  { file: '/data/config/database.conf', version: 'v2.1.5', size: '1.2 KB', modified: '1 day ago', user: 'admin' },
  { file: '/data/scripts/backup.sh', version: 'v1.3.2', size: '8.7 KB', modified: '2 days ago', user: 'operator1' },
];

export function TagsVersioningPage() {
  return (
    <div className="space-y-6">
      <div className="flex justify-between items-center">
        <div>
          <h2>Tags & Versioning</h2>
          <p className="text-muted-foreground">Organize files with tags and track version history</p>
        </div>
        <Button>
          <Plus className="w-4 h-4 mr-2" />
          Create Tag
        </Button>
      </div>

      <div className="grid grid-cols-1 md:grid-cols-4 gap-4">
        <Card>
          <CardContent className="pt-6">
            <div className="flex items-center justify-between">
              <div>
                <p className="text-sm text-muted-foreground">Total Tags</p>
                <p className="mt-2">5</p>
              </div>
              <Tag className="w-5 h-5 text-blue-500" />
            </div>
          </CardContent>
        </Card>
        <Card>
          <CardContent className="pt-6">
            <div className="flex items-center justify-between">
              <div>
                <p className="text-sm text-muted-foreground">Tagged Files</p>
                <p className="mt-2">13,530</p>
              </div>
              <GitBranch className="w-5 h-5 text-green-500" />
            </div>
          </CardContent>
        </Card>
        <Card>
          <CardContent className="pt-6">
            <div className="flex items-center justify-between">
              <div>
                <p className="text-sm text-muted-foreground">Version History</p>
                <p className="mt-2">2,841</p>
              </div>
              <History className="w-5 h-5 text-purple-500" />
            </div>
          </CardContent>
        </Card>
        <Card>
          <CardContent className="pt-6">
            <p className="text-sm text-muted-foreground">Storage Used</p>
            <p className="mt-2">3.2 GB</p>
          </CardContent>
        </Card>
      </div>

      <Card>
        <CardHeader>
          <CardTitle>File Tags</CardTitle>
          <CardDescription>Organize and categorize your files</CardDescription>
        </CardHeader>
        <CardContent>
          <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
            {tags.map((tag, index) => (
              <div key={index} className="flex items-center justify-between p-4 border border-border rounded-lg">
                <div className="flex items-center gap-3">
                  <div className={`w-3 h-3 rounded-full ${tag.color}`} />
                  <div>
                    <p>{tag.name}</p>
                    <p className="text-sm text-muted-foreground">{tag.files} files</p>
                  </div>
                </div>
                <div className="text-right">
                  <p className="text-sm text-muted-foreground">Created</p>
                  <p className="text-sm">{tag.created}</p>
                </div>
              </div>
            ))}
          </div>
        </CardContent>
      </Card>

      <Card>
        <CardHeader>
          <div className="flex justify-between items-center">
            <div>
              <CardTitle>Version History</CardTitle>
              <CardDescription>Recent file versions and changes</CardDescription>
            </div>
            <Input placeholder="Search files..." className="max-w-xs" />
          </div>
        </CardHeader>
        <CardContent>
          <div className="space-y-4">
            {versions.map((version, index) => (
              <div key={index} className="flex items-center justify-between py-3 border-b border-border last:border-0">
                <div className="flex-1">
                  <p>{version.file}</p>
                  <p className="text-sm text-muted-foreground">
                    Modified {version.modified} by {version.user}
                  </p>
                </div>
                <div className="flex items-center gap-4">
                  <Badge variant="outline">{version.version}</Badge>
                  <span className="text-sm text-muted-foreground">{version.size}</span>
                  <Button variant="ghost" size="sm">View</Button>
                  <Button variant="ghost" size="sm">Restore</Button>
                </div>
              </div>
            ))}
          </div>
        </CardContent>
      </Card>

      <Card>
        <CardHeader>
          <CardTitle>Version Control Settings</CardTitle>
          <CardDescription>Configure automatic versioning and retention</CardDescription>
        </CardHeader>
        <CardContent className="space-y-4">
          <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
            <div className="space-y-2">
              <p className="text-sm">Auto-versioning</p>
              <p className="text-xs text-muted-foreground">Automatically create versions on save</p>
              <Badge variant="default">Enabled</Badge>
            </div>
            <div className="space-y-2">
              <p className="text-sm">Retention Period</p>
              <p className="text-xs text-muted-foreground">How long to keep old versions</p>
              <Badge variant="outline">90 days</Badge>
            </div>
            <div className="space-y-2">
              <p className="text-sm">Max Versions</p>
              <p className="text-xs text-muted-foreground">Maximum versions per file</p>
              <Badge variant="outline">50</Badge>
            </div>
            <div className="space-y-2">
              <p className="text-sm">Compression</p>
              <p className="text-xs text-muted-foreground">Compress old versions</p>
              <Badge variant="default">Enabled</Badge>
            </div>
          </div>
        </CardContent>
      </Card>
    </div>
  );
}
