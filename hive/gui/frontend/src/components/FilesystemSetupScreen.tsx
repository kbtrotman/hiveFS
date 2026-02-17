import { useEffect, useState, type CSSProperties } from 'react';
import { Card, CardContent, CardHeader, CardTitle } from './ui/card';
import { Input } from './ui/input';
import { Label } from './ui/label';
import { Button } from './ui/button';
import { Checkbox } from './ui/checkbox';
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from './ui/select';
import { Slider } from './ui/slider';
import { Tabs, TabsContent, TabsList, TabsTrigger } from './ui/tabs';
import { AlertCircle, Save } from 'lucide-react';

const titleGradientStyle: CSSProperties = {
  backgroundImage:
    'linear-gradient(90deg, #0ea5e9 0%, #38bdf8 40%, var(--foreground, #e5e7eb) 75%, #f97316 100%)',
  WebkitBackgroundClip: 'text',
  backgroundClip: 'text',
  WebkitTextFillColor: 'transparent',
  color: 'transparent',
};

const labelGradientStyle: CSSProperties = {
  color: '#ffffff',
};

const DEFAULT_METADATA_TARGETS = ['Tier-0 NVMe', 'Tier-1 SSD', 'Object Archive'];
const DEFAULT_LOG_TARGETS = ['Log Mirror A', 'Log Mirror B', 'Cloud Spillover'];
const FILESYSTEM_API_URL =
  import.meta?.env?.VITE_FILESYSTEM_CONFIG_URL ?? 'http://localhost:8000/api/v1/filesystem';

type RollupSettings = {
  initialMinutes: number;
  dailyMinutes: number;
  weeklyDays: number;
};

type ClusterSettings = {
  allowGuiManagement: boolean;
  encryptInterCluster: boolean;
  allowCliAccess: boolean;
  notificationForwarder: string;
  pkiUsername: string;
  rollup: RollupSettings;
};

type FilesystemSettings = {
  clientTimeoutMs: string;
  encryptClientTraffic: boolean;
  enableTags: boolean;
  metadataTarget: string;
  logTarget: string;
  metadataSpace: number;
  kvSpace: number;
  logSpace: number;
};

type FilesystemTab = 'cluster' | 'filesystem';

export type FilesystemSetupConfiguration = { cluster: ClusterSettings; filesystem: FilesystemSettings };

type FilesystemSetupScreenProps = {
  totalCapacityGiB?: number;
  metadataTargets?: string[];
  logTargets?: string[];
  initialTab?: FilesystemTab;
  onSave?: (config: FilesystemSetupConfiguration) => void;
};

type RollupKey = keyof RollupSettings;

const rollupSliderConfig: Record<
  RollupKey,
  {
    label: string;
    helper: string;
    min: number;
    max: number;
    step: number;
    formatter: (value: number) => string;
  }
> = {
  initialMinutes: {
    label: 'Initial Rollup Interval',
    helper: 'How frequently we summarize metrics right after install.',
    min: 5,
    max: 30,
    step: 5,
    formatter: (value) => `Every ${value} minute${value === 1 ? '' : 's'}`,
  },
  dailyMinutes: {
    label: 'Daily Rollup Interval',
    helper: 'Controls daily compaction from one hour to six hours.',
    min: 60,
    max: 360,
    step: 60,
    formatter: (value) => `Every ${value / 60} hour${value / 60 === 1 ? '' : 's'}`,
  },
  weeklyDays: {
    label: 'Weekly Rollup Interval',
    helper: 'Used for archival stats spanning one to seven days.',
    min: 1,
    max: 7,
    step: 1,
    formatter: (value) => (value === 1 ? 'Every day' : `Every ${value} days`),
  },
};

const allocationDetails = [
  {
    key: 'metadataSpace',
    label: 'Metadata Filesystem',
    helper: 'Namespace, inode, and directory metadata reservation.',
  },
  {
    key: 'kvSpace',
    label: 'Key-Value Store',
    helper: 'Used for placement metadata and node service maps.',
  },
  {
    key: 'logSpace',
    label: 'Log Filesystem',
    helper: 'Change-log, journal, and audit records.',
  },
] as const;

export function FilesystemSetupScreen({
  totalCapacityGiB = 1536,
  metadataTargets = DEFAULT_METADATA_TARGETS,
  logTargets = DEFAULT_LOG_TARGETS,
  initialTab = 'cluster',
  onSave,
}: FilesystemSetupScreenProps) {
  const buildInitialClusterSettings = (): ClusterSettings => ({
    allowGuiManagement: false,
    encryptInterCluster: false,
    allowCliAccess: true,
    notificationForwarder: '',
    pkiUsername: '',
    rollup: {
      initialMinutes: 10,
      dailyMinutes: 120,
      weeklyDays: 3,
    },
  });

  const buildInitialFilesystemSettings = (): FilesystemSettings => {
    const baseMetadata = Math.round(totalCapacityGiB * 0.3);
    const baseKv = Math.round(totalCapacityGiB * 0.4);
    const baseLog = Math.max(totalCapacityGiB - baseMetadata - baseKv, 0);
    return {
      clientTimeoutMs: '30000',
      encryptClientTraffic: true,
      enableTags: true,
      metadataTarget: metadataTargets[0] ?? 'Tier-0 NVMe',
      logTarget: logTargets[0] ?? 'Log Mirror A',
      metadataSpace: baseMetadata,
      kvSpace: baseKv,
      logSpace: baseLog,
    };
  };

  const createBaselineConfig = (overrides?: Partial<FilesystemSetupConfiguration>) => {
    const defaultCluster = buildInitialClusterSettings();
    const mergedCluster: ClusterSettings = {
      ...defaultCluster,
      ...overrides?.cluster,
      rollup: {
        ...defaultCluster.rollup,
        ...(overrides?.cluster?.rollup ?? {}),
      },
    };
    const defaultFilesystem = buildInitialFilesystemSettings();
    const mergedFilesystem: FilesystemSettings = {
      ...defaultFilesystem,
      ...overrides?.filesystem,
    };
    return { cluster: mergedCluster, filesystem: mergedFilesystem };
  };

  const [baselineConfig, setBaselineConfig] = useState<FilesystemSetupConfiguration>(() =>
    createBaselineConfig(),
  );

  const [clusterSettings, setClusterSettings] = useState<ClusterSettings>(() => ({
    ...baselineConfig.cluster,
  }));

  const [filesystemSettings, setFilesystemSettings] = useState<FilesystemSettings>(() => ({
    ...baselineConfig.filesystem,
  }));

  const [statusMessage, setStatusMessage] = useState<string | null>(null);
  const [errorMessage, setErrorMessage] = useState<string | null>(null);
  const [activeTab, setActiveTab] = useState<FilesystemTab>(initialTab);
  const [loadingConfig, setLoadingConfig] = useState(false);
  const [savingConfig, setSavingConfig] = useState(false);

  useEffect(() => {
    setActiveTab(initialTab);
  }, [initialTab]);

  const fetchWithAuth = async (path: string, init: RequestInit = {}) => {
    const token = localStorage.getItem('authToken');
    const headers = new Headers(init.headers ?? undefined);
    if (token && !headers.has('Authorization')) {
      headers.set('Authorization', `Token ${token}`);
    }
    const response = await fetch(path, { ...init, headers });
    if (!response.ok) {
      throw new Error(`${response.status} ${response.statusText}`);
    }
    return response;
  };

  const applyConfiguration = (payload?: Partial<FilesystemSetupConfiguration>) => {
    const normalized = createBaselineConfig(payload);
    setBaselineConfig(normalized);
    setClusterSettings({ ...normalized.cluster });
    setFilesystemSettings({ ...normalized.filesystem });
  };

  useEffect(() => {
    let ignore = false;
    const loadConfig = async () => {
      setLoadingConfig(true);
      setErrorMessage(null);
      try {
        const response = await fetchWithAuth(FILESYSTEM_API_URL);
        const payload = await response.json();
        if (!ignore) {
          applyConfiguration(payload ?? {});
        }
      } catch (err) {
        if (!ignore) {
          console.error('Failed to load filesystem settings', err);
        }
      } finally {
        if (!ignore) {
          setLoadingConfig(false);
        }
      }
    };
    loadConfig();
    return () => {
      ignore = true;
    };
  }, []);

  const totalAllocated =
    filesystemSettings.metadataSpace + filesystemSettings.kvSpace + filesystemSettings.logSpace;
  const availableSpace = Math.max(totalCapacityGiB - totalAllocated, 0);

  const updateCluster = (patch: Partial<ClusterSettings>) => {
    setClusterSettings((prev) => ({
      ...prev,
      ...patch,
      rollup: patch.rollup ? { ...prev.rollup, ...patch.rollup } : prev.rollup,
    }));
  };

  const updateRollup = (key: RollupKey, value: number) => {
    setClusterSettings((prev) => ({
      ...prev,
      rollup: {
        ...prev.rollup,
        [key]: value,
      },
    }));
  };

  const updateFilesystem = (patch: Partial<FilesystemSettings>) => {
    setFilesystemSettings((prev) => ({
      ...prev,
      ...patch,
    }));
  };

  const updateAllocation = (
    key: typeof allocationDetails[number]['key'],
    rawValue: number | undefined,
  ) => {
    setFilesystemSettings((prev) => {
      const requested = Math.max(0, Math.min(totalCapacityGiB, rawValue ?? 0));
      const otherKeys = allocationDetails
        .map((detail) => detail.key)
        .filter((detailKey) => detailKey !== key);
      const otherTotal = otherKeys.reduce((sum, detailKey) => sum + prev[detailKey], 0);
      const maxForKey = totalCapacityGiB - otherTotal;
      const nextValue = Math.min(requested, Math.max(maxForKey, 0));
      return {
        ...prev,
        [key]: nextValue,
      };
    });
  };

  type ClusterAction = 'add' | 'divorce' | 'rename';

  const handleClusterAction = (action: ClusterAction) => {
    const copyMap: Record<ClusterAction, string> = {
      add: 'Add a new Cluster action queued.',
      divorce: 'Divorce existing cluster action queued.',
      rename: 'Change cluster name action queued.',
    };
    setStatusMessage(copyMap[action]);
  };

  const handleReset = () => {
    setClusterSettings({ ...baselineConfig.cluster });
    setFilesystemSettings({ ...baselineConfig.filesystem });
    setStatusMessage('Settings reverted to last loaded values.');
    setErrorMessage(null);
  };

  const handleSave = async () => {
    const payload: FilesystemSetupConfiguration = {
      cluster: { ...clusterSettings },
      filesystem: { ...filesystemSettings },
    };
    setStatusMessage(null);
    setErrorMessage(null);
    setSavingConfig(true);
    try {
      await fetchWithAuth(FILESYSTEM_API_URL, {
        method: 'PUT',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(payload),
      });
      setStatusMessage('Filesystem settings saved.');
      setBaselineConfig(payload);
      onSave?.(payload);
    } catch (err) {
      setErrorMessage('Unable to save filesystem settings.');
    } finally {
      setSavingConfig(false);
    }
  };

  return (
    <div className="flex h-full min-h-0 w-full flex-col bg-background text-foreground">
      <div className="flex flex-col items-center gap-3 px-6 pt-8 pb-4 text-center">
        {statusMessage && <p className="text-xs text-emerald-400">{statusMessage}</p>}
        {errorMessage && <p className="text-xs text-destructive">{errorMessage}</p>}
      </div>

      <div className="flex-1">
        <div className="mx-auto flex w-full max-w-5xl flex-col px-6 pb-12">
          <Tabs
            className="w-full"
            value={activeTab}
            onValueChange={(value) => setActiveTab((value as FilesystemTab) ?? 'cluster')}
          >
            <TabsList className="mx-auto mb-4 flex">
              <TabsTrigger value="cluster">Cluster Guardrails</TabsTrigger>
              <TabsTrigger value="filesystem">Filesystem Layout</TabsTrigger>
            </TabsList>

            <TabsContent value="cluster" className="mt-2">
              <Card>
                <CardHeader>
                  <CardTitle className="text-xl" style={titleGradientStyle}>
                    Cluster Guardrails
                  </CardTitle>
                </CardHeader>
                <CardContent className="space-y-6">
                  <div className="flex flex-wrap gap-3">
                    <Button onClick={() => handleClusterAction('add')}>Add a new Cluster</Button>
                    <Button variant="secondary" onClick={() => handleClusterAction('divorce')}>
                      Divorce existing cluster
                    </Button>
                    <Button variant="outline" onClick={() => handleClusterAction('rename')}>
                      Change Cluster Name
                    </Button>
                  </div>

                  <div className="grid gap-4 sm:grid-cols-2">
                    <div>
                      <Label
                        htmlFor="notification-forwarder"
                        className="text-xs uppercase tracking-wide text-muted-foreground"
                        style={labelGradientStyle}
                      >
                        Notification Email Forwarder
                      </Label>
                      <Input
                        id="notification-forwarder"
                        placeholder="email.example.com"
                        className="mt-1 text-sm"
                        value={clusterSettings.notificationForwarder}
                        onChange={(event) =>
                          updateCluster({ notificationForwarder: event.target.value })
                        }
                      />
                    </div>
                    <div>
                      <Label
                        htmlFor="pki-username"
                        className="text-xs uppercase tracking-wide text-muted-foreground"
                        style={labelGradientStyle}
                      >
                        PKI Request Username
                      </Label>
                      <Input
                        id="pki-username"
                        placeholder="admin@clustername.example.com"
                        className="mt-1 text-sm"
                        value={clusterSettings.pkiUsername}
                        onChange={(event) => updateCluster({ pkiUsername: event.target.value })}
                      />
                    </div>
                  </div>

                  <div className="space-y-4">
                    {(
                      Object.keys(rollupSliderConfig) as Array<keyof typeof rollupSliderConfig>
                    ).map((key) => {
                      const config = rollupSliderConfig[key];
                      const value = clusterSettings.rollup[key];
                      return (
                        <div
                          key={key}
                          className="rounded-xl border border-border/50 bg-background/40 p-4"
                        >
                          <div className="flex items-center justify-between text-sm">
                            <div>
                              <p className="font-semibold">{config.label}</p>
                              <p className="text-xs text-muted-foreground">{config.helper}</p>
                            </div>
                            <span className="text-xs text-primary font-medium">
                              {config.formatter(value)}
                            </span>
                          </div>
                          <Slider
                            min={config.min}
                            max={config.max}
                            step={config.step}
                            className="mt-4"
                            value={[value]}
                            onValueChange={(vals) => updateRollup(key, vals[0] ?? config.min)}
                          />
                          <div className="mt-2 flex justify-between text-xs text-muted-foreground">
                            <span>{config.formatter(config.min)}</span>
                            <span>{config.formatter(config.max)}</span>
                          </div>
                        </div>
                      );
                    })}
                  </div>

                  <div className="grid gap-3">
                    <div className="flex items-center gap-2">
                      <Checkbox
                        id="allow-gui"
                        checked={clusterSettings.allowGuiManagement}
                        onCheckedChange={(checked) =>
                          updateCluster({ allowGuiManagement: checked === true })
                        }
                      />
                      <Label htmlFor="allow-gui" className="text-sm">
                        Allow inter-cluster GUI management
                      </Label>
                    </div>
                    <div className="flex items-center gap-2">
                      <Checkbox
                        id="encrypt-cluster"
                        checked={clusterSettings.encryptInterCluster}
                        onCheckedChange={(checked) =>
                          updateCluster({ encryptInterCluster: checked === true })
                        }
                      />
                      <Label htmlFor="encrypt-cluster" className="text-sm">
                        Encrypt inter-cluster traffic
                      </Label>
                    </div>
                    <div className="flex items-center gap-2">
                      <Checkbox
                        id="allow-cli"
                        checked={clusterSettings.allowCliAccess}
                        onCheckedChange={(checked) =>
                          updateCluster({ allowCliAccess: checked !== false })
                        }
                      />
                      <Label htmlFor="allow-cli" className="text-sm">
                        Allow node command-line access in GUI
                      </Label>
                    </div>
                  </div>
                </CardContent>
              </Card>
            </TabsContent>

            <TabsContent value="filesystem" className="mt-2">
              <Card>
                <CardHeader>
                  <CardTitle className="text-xl" style={titleGradientStyle}>
                    Client & Filesystem Layout
                  </CardTitle>
                </CardHeader>
                <CardContent className="space-y-6">
                  <div className="grid gap-4 sm:grid-cols-3">
                    <div className="sm:col-span-1">
                      <Label
                        htmlFor="client-timeout"
                        className="text-xs uppercase tracking-wide text-muted-foreground"
                        style={labelGradientStyle}
                      >
                        Client Connect Timeout (ms)
                      </Label>
                      <Input
                        id="client-timeout"
                        type="number"
                        className="mt-1 text-sm"
                        value={filesystemSettings.clientTimeoutMs}
                        onChange={(event) =>
                          updateFilesystem({ clientTimeoutMs: event.target.value })
                        }
                      />
                    </div>
                    <div className="sm:col-span-1">
                      <Label
                        className="text-xs uppercase tracking-wide text-muted-foreground"
                        style={labelGradientStyle}
                      >
                        Metadata Filesystem
                      </Label>
                      <Select
                        value={filesystemSettings.metadataTarget}
                        onValueChange={(value) => updateFilesystem({ metadataTarget: value })}
                      >
                        <SelectTrigger className="mt-1">
                          <SelectValue placeholder="Select metadata tier" />
                        </SelectTrigger>
                        <SelectContent>
                          {metadataTargets.map((target) => (
                            <SelectItem key={target} value={target}>
                              {target}
                            </SelectItem>
                          ))}
                        </SelectContent>
                      </Select>
                    </div>
                    <div className="sm:col-span-1">
                      <Label
                        className="text-xs uppercase tracking-wide text-muted-foreground"
                        style={labelGradientStyle}
                      >
                        Log Filesystem
                      </Label>
                      <Select
                        value={filesystemSettings.logTarget}
                        onValueChange={(value) => updateFilesystem({ logTarget: value })}
                      >
                        <SelectTrigger className="mt-1">
                          <SelectValue placeholder="Select log tier" />
                        </SelectTrigger>
                        <SelectContent>
                          {logTargets.map((target) => (
                            <SelectItem key={target} value={target}>
                              {target}
                            </SelectItem>
                          ))}
                        </SelectContent>
                      </Select>
                    </div>
                  </div>

                  <div className="grid gap-3 sm:grid-cols-2">
                    <div className="flex items-center gap-2 rounded-lg border border-border/50 bg-background/40 p-3">
                      <Checkbox
                        id="encrypt-client"
                        checked={filesystemSettings.encryptClientTraffic}
                        onCheckedChange={(checked) =>
                          updateFilesystem({ encryptClientTraffic: checked !== false })
                        }
                      />
                      <Label htmlFor="encrypt-client" className="text-sm">
                        Encrypt all client communications
                      </Label>
                    </div>
                    <div className="flex items-center gap-2 rounded-lg border border-border/50 bg-background/40 p-3">
                      <Checkbox
                        id="enable-tags"
                        checked={filesystemSettings.enableTags}
                        onCheckedChange={(checked) => updateFilesystem({ enableTags: checked !== false })}
                      />
                      <Label htmlFor="enable-tags" className="text-sm">
                        Enable tags for client operations
                      </Label>
                    </div>
                  </div>

                  <div className="rounded-2xl border border-border/60 bg-background/60 p-5">
                    <div className="flex flex-wrap items-center justify-between gap-3">
                      <div>
                        <p className="text-sm font-semibold" style={labelGradientStyle}>
                          Capacity Planner
                        </p>
                        <p className="text-xs text-muted-foreground">
                          Available space updates as you adjust the sliders below.
                        </p>
                      </div>
                      <div className="text-right text-sm">
                        <p className="text-emerald-400 font-semibold">{availableSpace} GiB free</p>
                        <p className="text-xs text-muted-foreground">out of {totalCapacityGiB} GiB</p>
                      </div>
                    </div>

                    <div className="mt-5 space-y-4">
                      {allocationDetails.map((detail) => {
                        const value = filesystemSettings[detail.key];
                        return (
                          <div key={detail.key} className="rounded-xl border border-border/40 p-4">
                            <div className="flex items-center justify-between text-sm">
                              <div>
                                <p className="font-semibold">{detail.label}</p>
                                <p className="text-xs text-muted-foreground">{detail.helper}</p>
                              </div>
                              <span className="text-xs font-semibold text-primary">{value} GiB</span>
                            </div>
                            <Slider
                              className="mt-4"
                              min={0}
                              max={totalCapacityGiB}
                              step={16}
                              value={[value]}
                              onValueChange={(vals) => updateAllocation(detail.key, vals[0])}
                            />
                          </div>
                        );
                      })}
                    </div>
                  </div>
                </CardContent>
              </Card>
            </TabsContent>
          </Tabs>

          <div className="mt-6 flex flex-col gap-3">
            {loadingConfig && (
              <p className="text-xs text-muted-foreground">Loading filesystem configuration…</p>
            )}
            <div className="flex justify-end gap-3">
              <Button variant="ghost" onClick={handleReset} disabled={savingConfig || loadingConfig}>
                Reset
              </Button>
              <Button onClick={handleSave} disabled={savingConfig || loadingConfig}>
                {savingConfig ? (
                  <>
                    <Save className="mr-2 size-4 animate-spin" />
                    Saving…
                  </>
                ) : (
                  'Save Filesystem Settings'
                )}
              </Button>
            </div>
          </div>
        </div>
      </div>
    </div>
  );
}
