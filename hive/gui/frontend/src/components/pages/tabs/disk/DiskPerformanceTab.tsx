import { useEffect, useMemo, useState } from 'react';
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from '../../../ui/card';
import { Checkbox } from '../../../ui/checkbox';
import { PerformanceChart } from '../../../PerformanceChart';
import { Activity, HardDrive, TrendingUp, TrendingDown } from 'lucide-react';
import { useApiResource } from '../../../useApiResource';

interface DiskPerformanceRecord {
  node_id?: number | null;
  disk_name?: string | null;
  disk_path?: string | null;
  disk_size_bytes?: number | null;
  disk_rotational?: number | null;
  disk_ts?: string | null;
  reads_completed?: number | null;
  writes_completed?: number | null;
  read_bytes?: number | null;
  write_bytes?: number | null;
  read_ms?: number | null;
  write_ms?: number | null;
  io_in_progress?: number | null;
  io_ms?: number | null;
  weighted_io_ms?: number | null;
}

const MAX_POINTS = 50;

const ensureDiskArray = (raw: unknown): DiskPerformanceRecord[] =>
  Array.isArray(raw) ? (raw as DiskPerformanceRecord[]) : [];

const safeNumber = (value: unknown, fallback = 0) => {
  const num = Number(value);
  return Number.isFinite(num) ? num : fallback;
};

const formatNumber = (value: number, options?: Intl.NumberFormatOptions) =>
  Number.isFinite(value) ? new Intl.NumberFormat(undefined, options).format(value) : '—';

const formatBytes = (value: number) => {
  if (!Number.isFinite(value) || value <= 0) return '—';
  const units = ['B', 'KB', 'MB', 'GB', 'TB', 'PB'];
  let idx = 0;
  let current = value;
  while (current >= 1024 && idx < units.length - 1) {
    current /= 1024;
    idx += 1;
  }
  const digits = idx === 0 ? 0 : 1;
  return `${current.toFixed(digits)} ${units[idx]}`;
};

const sumDiskField = (records: DiskPerformanceRecord[], field: keyof DiskPerformanceRecord) =>
  records.reduce((sum, record) => sum + safeNumber(record[field]), 0);

interface AggregateFieldConfig {
  field: keyof DiskPerformanceRecord;
  alias: string;
  label: string;
  color: string;
}

function buildAggregateSeries(records: DiskPerformanceRecord[], fields: AggregateFieldConfig[]) {
  const dataMap = new Map<
    string,
    {
      sortKey: number;
      time: string;
      [key: string]: number | string;
    }
  >();

  records.forEach((record, index) => {
    const { mapKey, sortKey, label } = createTimePoint(record.disk_ts, index);
    const entry = dataMap.get(mapKey) ?? { sortKey, time: label };
    fields.forEach(({ field, alias }) => {
      entry[alias] = safeNumber(entry[alias], 0) + safeNumber(record[field]);
    });
    dataMap.set(mapKey, entry);
  });

  const data = Array.from(dataMap.values())
    .sort((a, b) => a.sortKey - b.sortKey)
    .slice(-MAX_POINTS)
    .map(({ sortKey, ...rest }) => rest);

  const series = fields.map(({ alias, color, label }) => ({
    dataKey: alias,
    color,
    label,
  }));

  return { data, series };
}

function createTimePoint(timestamp?: string | null, fallbackIndex = 0) {
  if (timestamp) {
    const date = new Date(timestamp);
    if (!Number.isNaN(date.getTime())) {
      return {
        mapKey: date.getTime().toString(),
        sortKey: date.getTime(),
        label: date.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' }),
      };
    }
  }
  return {
    mapKey: `sample-${fallbackIndex}`,
    sortKey: fallbackIndex,
    label: `Sample ${fallbackIndex + 1}`,
  };
}

export function DiskPerformanceTab() {
  const { data: diskStats, isLoading, error } = useApiResource<DiskPerformanceRecord[]>(
    'health/disks',
    {
      initialData: [],
      pollInterval: 15000,
      transform: ensureDiskArray,
    },
  );

  const totals = useMemo(
    () => ({
      readsCompleted: sumDiskField(diskStats, 'reads_completed'),
      writesCompleted: sumDiskField(diskStats, 'writes_completed'),
      readBytes: sumDiskField(diskStats, 'read_bytes'),
      writeBytes: sumDiskField(diskStats, 'write_bytes'),
    }),
    [diskStats],
  );

  const diskRows = useMemo(
    () =>
      diskStats.map((record, index) => {
        const id = record.disk_path ?? `${record.node_id ?? 'node'}-${record.disk_name ?? index}`;
        return {
          id,
          nodeId: record.node_id ?? null,
          diskName: record.disk_name ?? 'Unknown disk',
          diskPath: record.disk_path ?? '—',
          sizeBytes: safeNumber(record.disk_size_bytes),
          rotational: record.disk_rotational ?? null,
        };
      }),
    [diskStats],
  );

  const [selectedDisks, setSelectedDisks] = useState<Record<string, boolean>>({});

  useEffect(() => {
    setSelectedDisks((prev) => {
      const validIds = new Set(diskRows.map((row) => row.id));
      const next: Record<string, boolean> = {};
      validIds.forEach((id) => {
        if (prev[id]) {
          next[id] = true;
        }
      });
      return next;
    });
  }, [diskRows]);

  const allSelected = diskRows.length > 0 && diskRows.every((row) => selectedDisks[row.id]);

  const toggleAll = (checked: boolean) => {
    if (checked) {
      const next: Record<string, boolean> = {};
      diskRows.forEach((row) => {
        next[row.id] = true;
      });
      setSelectedDisks(next);
    } else {
      setSelectedDisks({});
    }
  };

  const toggleRow = (id: string, checked: boolean) => {
    setSelectedDisks((prev) => {
      const next = { ...prev };
      if (checked) {
        next[id] = true;
      } else {
        delete next[id];
      }
      return next;
    });
  };

  const operationsSeries = useMemo(
    () =>
      buildAggregateSeries(diskStats, [
        { field: 'reads_completed', alias: 'reads', label: 'Reads Completed', color: '#2563eb' },
        { field: 'writes_completed', alias: 'writes', label: 'Writes Completed', color: '#f97316' },
      ]),
    [diskStats],
  );

  const bytesSeries = useMemo(
    () =>
      buildAggregateSeries(diskStats, [
        { field: 'read_bytes', alias: 'readBytes', label: 'Read Bytes', color: '#7c3aed' },
        { field: 'write_bytes', alias: 'writeBytes', label: 'Write Bytes', color: '#059669' },
      ]),
    [diskStats],
  );

  const serviceSeries = useMemo(
    () =>
      buildAggregateSeries(diskStats, [
        { field: 'read_ms', alias: 'readMs', label: 'Read ms', color: '#0ea5e9' },
        { field: 'write_ms', alias: 'writeMs', label: 'Write ms', color: '#ec4899' },
        {
          field: 'io_in_progress',
          alias: 'ioInProgress',
          label: 'IO In Progress',
          color: '#facc15',
        },
      ]),
    [diskStats],
  );

  const queueSeries = useMemo(
    () =>
      buildAggregateSeries(diskStats, [
        { field: 'io_ms', alias: 'ioMs', label: 'IO Active ms', color: '#22d3ee' },
        {
          field: 'weighted_io_ms',
          alias: 'weightedIoMs',
          label: 'Weighted IO ms',
          color: '#d97706',
        },
      ]),
    [diskStats],
  );

  const cards = [
    {
      label: 'Reads Completed',
      value: formatNumber(totals.readsCompleted),
      icon: Activity,
      color: 'text-blue-500',
    },
    {
      label: 'Writes Completed',
      value: formatNumber(totals.writesCompleted),
      icon: TrendingDown,
      color: 'text-purple-500',
    },
    {
      label: 'Read Volume',
      value: formatBytes(totals.readBytes),
      icon: TrendingUp,
      color: 'text-emerald-500',
    },
    {
      label: 'Write Volume',
      value: formatBytes(totals.writeBytes),
      icon: HardDrive,
      color: 'text-orange-500',
    },
  ];

  return (
    <div className="space-y-6">
      <div>
        <h2>Disk Performance Metrics</h2>
        <p className="text-muted-foreground">Detailed performance statistics for all disks in each node</p>
      </div>

      <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-4 gap-4">
        {cards.map((card, index) => {
          const Icon = card.icon;
          return (
            <Card key={card.label + index}>
              <CardContent className="pt-6">
                <div className="flex items-center justify-between">
                  <div>
                    <p className="text-sm text-muted-foreground">{card.label}</p>
                    <p className="mt-2">{card.value}</p>
                  </div>
                  <Icon className={`w-5 h-5 ${card.color}`} />
                </div>
              </CardContent>
            </Card>
          );
        })}
      </div>

      {error && (
        <div className="rounded-md border border-destructive/40 bg-destructive/10 px-4 py-3 text-sm text-destructive">
          {error}
        </div>
      )}

      <Card>
        <CardHeader>
          <CardTitle>System Disks</CardTitle>
          <CardDescription>All disks attached to storage nodes</CardDescription>
        </CardHeader>
        <CardContent>
          {isLoading ? (
            <p className="text-sm text-muted-foreground">Loading disk inventory…</p>
          ) : (
            <div className="overflow-x-auto">
              <table className="w-full text-sm">
                <thead className="text-left text-muted-foreground">
                  <tr>
                    <th className="px-3 py-2">
                      <Checkbox
                        checked={allSelected}
                        onCheckedChange={(checked) => toggleAll(Boolean(checked))}
                        aria-label="Select all disks"
                      />
                    </th>
                    <th className="px-3 py-2">Node</th>
                    <th className="px-3 py-2">Disk</th>
                    <th className="px-3 py-2">Path</th>
                    <th className="px-3 py-2">Capacity</th>
                    <th className="px-3 py-2">Type</th>
                  </tr>
                </thead>
                <tbody>
                  {diskRows.length === 0 ? (
                    <tr>
                      <td colSpan={6} className="px-3 py-4 text-center text-muted-foreground">
                        No disk statistics reported yet.
                      </td>
                    </tr>
                  ) : (
                    diskRows.map((row) => (
                      <tr key={row.id} className="border-t border-border/60">
                        <td className="px-3 py-2">
                          <Checkbox
                            checked={!!selectedDisks[row.id]}
                            onCheckedChange={(checked) => toggleRow(row.id, Boolean(checked))}
                            aria-label={`Select ${row.diskName}`}
                          />
                        </td>
                        <td className="px-3 py-2">{row.nodeId ?? '—'}</td>
                        <td className="px-3 py-2">{row.diskName}</td>
                        <td className="px-3 py-2 font-mono text-xs">{row.diskPath}</td>
                        <td className="px-3 py-2">{formatBytes(row.sizeBytes)}</td>
                        <td className="px-3 py-2">
                          {row.rotational === null ? '—' : row.rotational === 0 ? 'SSD' : 'HDD'}
                        </td>
                      </tr>
                    ))
                  )}
                </tbody>
              </table>
            </div>
          )}
        </CardContent>
      </Card>

      <div className="grid grid-cols-1 lg:grid-cols-2 gap-6">
        <PerformanceChart
          title="Reads vs Writes Completed"
          description="Total IO operations reported across all disks"
          data={operationsSeries.data}
          series={operationsSeries.series}
          isLoading={isLoading}
        />
        <PerformanceChart
          title="Read/Write Bytes"
          description="Bytes processed across disks"
          data={bytesSeries.data}
          series={bytesSeries.series}
          isLoading={isLoading}
        />
        <PerformanceChart
          title="IO Service Time"
          description="Read/write service ms and in-flight operations"
          data={serviceSeries.data}
          series={serviceSeries.series}
          isLoading={isLoading}
        />
        <PerformanceChart
          title="IO Activity"
          description="Active IO window vs weighted IO time"
          data={queueSeries.data}
          series={queueSeries.series}
          isLoading={isLoading}
        />
      </div>
    </div>
  );
}
