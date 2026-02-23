import { useEffect, useState } from 'react';
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from '../../../ui/card';
import { Button } from '../../../ui/button';
import { Badge } from '../../../ui/badge';
import {
  HardDrive,
  Database,
  Archive,
} from 'lucide-react';

type FilesystemStat = {
  node_id: number;
  fs_ts: string;
  fs_name: string;
  fs_path: string;
  fs_type: string;
  fs_total_bytes: number;
  fs_used_bytes: number;
  fs_avail_bytes: number;
  fs_used_pct: number;
  in_total_bytes: number;
  in_used_bytes: number;
  in_avail_bytes: number;
  in_used_pct: number;
  health: string;
};

type DiskStat = {
  node_id: number;
  disk_ts: string;
  disk_name: string;
  disk_path: string;
  disk_size_bytes: number;
  disk_rotational: boolean;
  reads_completed: number;
  writes_completed: number;
  read_bytes: number;
  write_bytes: number;
  io_in_progress: number;
  io_ms: number;
  fs_path: string | null;
  health: string;
};

export function DiskManageTab() {
  const [fsStats, setFsStats] = useState<FilesystemStat[]>([]);
  const [diskStats, setDiskStats] = useState<DiskStat[]>([]);
  const [fsError, setFsError] = useState<string | null>(null);
  const [diskError, setDiskError] = useState<string | null>(null);

  const formatBytes = (bytes: number) => {
    if (!Number.isFinite(bytes)) return '—';
    const units = ['B', 'KB', 'MB', 'GB', 'TB', 'PB'];
    let value = bytes;
    let unit = 0;
    while (value >= 1024 && unit < units.length - 1) {
      value /= 1024;
      unit++;
    }
    return `${value.toFixed(1)} ${units[unit]}`;
  };

  useEffect(() => {
    const loadFsStats = async () => {
      try {
        const resp = await fetch('http://localhost:8000/api/v1/health/fs');
        if (!resp.ok) throw new Error(`Failed to load fs stats (${resp.status})`);
        const payload = await resp.json();
        setFsStats(Array.isArray(payload) ? payload : payload?.results ?? []);
        setFsError(null);
      } catch (err) {
        console.error('Failed to fetch filesystem health', err);
        setFsError('Filesystem metrics unavailable');
      }
    };

    const loadDiskStats = async () => {
      try {
        const resp = await fetch('http://localhost:8000/api/v1/health/disks');
        if (!resp.ok) throw new Error(`Failed to load disk stats (${resp.status})`);
        const payload = await resp.json();
        setDiskStats(Array.isArray(payload) ? payload : payload?.results ?? []);
        setDiskError(null);
      } catch (err) {
        console.error('Failed to fetch disk health', err);
        setDiskError('Disk metrics unavailable');
      }
    };

    loadFsStats();
    loadDiskStats();
  }, []);

  return (
    <div className="space-y-6">
      <div className="flex justify-between items-center">
        <div>
          <h2>Disk Management</h2>
          <p className="text-muted-foreground">Monitor storage volumes and manage filesystem paths</p>
        </div>
        <div className="flex items-center gap-2">
          <Button variant="outline">Offline Disk</Button>
          <Button variant="outline">Online Disk</Button>
          <Button variant="outline">Upgrade Firmware</Button>
          <Button>Add Disk</Button>
        </div>
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

      {/* Node health metrics */}
      <div className="grid grid-cols-1 lg:grid-cols-2 gap-6">
        <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
          <CardHeader>
            <CardTitle>Node Physical Space Metrics</CardTitle>
            <CardDescription>Filesystem capacity and inode consumption per node</CardDescription>
          </CardHeader>
          <CardContent className="space-y-3">
            {fsError && <p className="text-sm text-destructive">{fsError}</p>}
            {!fsError && (
              <div className="space-y-3">
                {(fsStats ?? []).slice(0, 4).map((fs) => (
                  <div key={`${fs.node_id}-${fs.fs_path}`} className="rounded-lg border border-border/60 p-3">
                    <div className="flex items-center justify-between">
                      <div>
                        <p className="text-sm font-medium">
                          Node {fs.node_id} • {fs.fs_name}
                        </p>
                        <p className="text-xs text-muted-foreground">{fs.fs_path}</p>
                      </div>
                      <Badge className={Number(fs.fs_used_pct) > 80 ? 'bg-amber-500' : 'bg-emerald-500'}>
                        {fs.health}
                      </Badge>
                    </div>
                    <div className="mt-2 flex items-center justify-between text-xs text-muted-foreground">
                      <span>{formatBytes(fs.fs_used_bytes)} used</span>
                      <span>{formatBytes(fs.fs_total_bytes)} total</span>
                    </div>
                    <div className="mt-1 h-1.5 rounded-full bg-muted">
                      <div
                        className="h-full rounded-full bg-gradient-to-r from-sky-400 via-blue-500 to-teal-400"
                        style={{ width: `${fs.fs_used_pct}%` }}
                      />
                    </div>
                    <div className="mt-2 grid grid-cols-2 gap-2 text-xs">
                      <div className="rounded border border-border/60 p-2">
                        <p className="text-muted-foreground">Inodes used</p>
                        <p className="font-medium">{Number(fs.in_used_pct).toFixed(1)}%</p>
                      </div>
                      <div className="rounded border border-border/60 p-2">
                        <p className="text-muted-foreground">Free bytes</p>
                        <p className="font-medium">{formatBytes(fs.fs_avail_bytes)}</p>
                      </div>
                    </div>
                  </div>
                ))}
                {!fsStats.length && (
                  <p className="text-sm text-muted-foreground">No filesystem metrics available.</p>
                )}
              </div>
            )}
          </CardContent>
        </Card>

        <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
          <CardHeader>
            <CardTitle>Node Physical Disk Metrics</CardTitle>
            <CardDescription>Drive throughput and SMART data per node</CardDescription>
          </CardHeader>
          <CardContent className="space-y-3">
            {diskError && <p className="text-sm text-destructive">{diskError}</p>}
            {!diskError && (
              <div className="space-y-3">
                {(diskStats ?? []).slice(0, 5).map((disk) => (
                  <div key={`${disk.node_id}-${disk.disk_name}`} className="rounded-lg border border-border/60 p-3">
                    <div className="flex items-center justify-between">
                      <div>
                        <p className="text-sm font-medium">
                          Node {disk.node_id} • {disk.disk_name}
                        </p>
                        <p className="text-xs text-muted-foreground">{disk.disk_path}</p>
                      </div>
                      <Badge className={disk.health === 'OK' ? 'bg-emerald-500' : 'bg-destructive'}>
                        {disk.health}
                      </Badge>
                    </div>
                    <div className="mt-2 grid grid-cols-2 gap-2 text-xs">
                      <div className="rounded border border-border/60 p-2">
                        <p className="text-muted-foreground">Capacity</p>
                        <p className="font-medium">{formatBytes(disk.disk_size_bytes)}</p>
                      </div>
                      <div className="rounded border border-border/60 p-2">
                        <p className="text-muted-foreground">Rotation</p>
                        <p className="font-medium">{disk.disk_rotational ? 'HDD' : 'SSD'}</p>
                      </div>
                      <div className="rounded border border-border/60 p-2">
                        <p className="text-muted-foreground">Reads</p>
                        <p className="font-medium">
                          {formatBytes(disk.read_bytes)} / {disk.reads_completed} ops
                        </p>
                      </div>
                      <div className="rounded border border-border/60 p-2">
                        <p className="text-muted-foreground">Writes</p>
                        <p className="font-medium">
                          {formatBytes(disk.write_bytes)} / {disk.writes_completed} ops
                        </p>
                      </div>
                    </div>
                    <p className="mt-2 text-xs text-muted-foreground">
                      IO queue: {disk.io_in_progress} • Active {disk.io_ms} ms
                    </p>
                  </div>
                ))}
                {!diskStats.length && (
                  <p className="text-sm text-muted-foreground">No disk metrics available.</p>
                )}
              </div>
            )}
          </CardContent>
        </Card>
      </div>
    </div>
  );
}
