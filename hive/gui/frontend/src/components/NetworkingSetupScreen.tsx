import { Fragment, useEffect, useState, type CSSProperties } from 'react';
import { Card, CardContent, CardHeader, CardTitle } from './ui/card';
import { Input } from './ui/input';
import { Label } from './ui/label';
import { Button } from './ui/button';
import { Checkbox } from './ui/checkbox';
import { Textarea } from './ui/textarea';
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from './ui/select';
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
  backgroundImage:
    'linear-gradient(90deg, #f97316 0%, var(--foreground, #e5e7eb) 35%, #38bdf8 80%, #0ea5e9 100%)',
  WebkitBackgroundClip: 'text',
  backgroundClip: 'text',
  WebkitTextFillColor: 'transparent',
  color: 'transparent',
};

const FALLBACK_INTERFACES = ['bond0', 'eth0', 'eth1', 'enp0s31f6'];

const DEFAULT_TIME_CONFIG = [
  '# Example chrony settings',
  'server time.google.com iburst',
  'server 1.pool.ntp.org iburst',
  'driftfile /var/lib/chrony/drift',
  'makestep 0.1 3',
].join('\n');

const NETWORKING_API_URL =
  import.meta?.env?.VITE_NETWORKING_CONFIG_URL ?? 'http://localhost:8000/api/v1/networking';

type InterfaceConfiguration = {
  interfaceName: string;
  autoDhcp: boolean;
  ipAddress: string;
  netmask: string;
};

export type NetworkingConfiguration = {
  clientInterface: InterfaceConfiguration;
  clusterInterface: InterfaceConfiguration;
  domainName: string;
  dnsServer: string;
  dnsSecondary: string;
  timeConfig: string;
};

type PartialNetworkingConfiguration = Partial<
  Omit<NetworkingConfiguration, 'clientInterface' | 'clusterInterface'>
> & {
  clientInterface?: Partial<InterfaceConfiguration>;
  clusterInterface?: Partial<InterfaceConfiguration>;
};

type InterfaceRole = 'client' | 'cluster';

type NetworkingSetupScreenProps = {
  interfaces?: string[];
  initialValues?: PartialNetworkingConfiguration;
  initialSection?: 'network' | 'dns' | 'time';
  isSaving?: boolean;
  onSave?: (configuration: NetworkingConfiguration) => void;
  onCancel?: () => void;
};

type NetworkingTab = 'network' | 'dns' | 'time';

const interfaceDetails: Record<InterfaceRole, { label: string; helper: string }> = {
  client: {
    label: 'Client-Facing Interface',
    helper: 'Handles ingress traffic from clients and front-end services.',
  },
  cluster: {
    label: 'Inter-cluster Network Interface',
    helper: 'Carries replication and node-to-node control traffic.',
  },
};

export function NetworkingSetupScreen({
  interfaces,
  initialValues,
  initialSection,
  isSaving = false,
  onSave,
  onCancel,
}: NetworkingSetupScreenProps) {
  const resolvedInterfaces = interfaces?.length ? interfaces : FALLBACK_INTERFACES;

  const buildInterfaceState = (defaults?: Partial<InterfaceConfiguration>): InterfaceConfiguration => ({
    interfaceName: defaults?.interfaceName ?? resolvedInterfaces[0] ?? '',
    autoDhcp: defaults?.autoDhcp ?? true,
    ipAddress: defaults?.ipAddress ?? '',
    netmask: defaults?.netmask ?? '',
  });

  const buildSnapshot = (source?: PartialNetworkingConfiguration): NetworkingConfiguration => ({
    clientInterface: buildInterfaceState(source?.clientInterface),
    clusterInterface: buildInterfaceState(source?.clusterInterface),
    domainName: source?.domainName ?? '',
    dnsServer: source?.dnsServer ?? '',
    dnsSecondary: source?.dnsSecondary ?? '',
    timeConfig: source?.timeConfig ?? DEFAULT_TIME_CONFIG,
  });

  const [initialSnapshot, setInitialSnapshot] = useState<NetworkingConfiguration>(() =>
    buildSnapshot(initialValues),
  );

  const [interfaceConfig, setInterfaceConfig] = useState<Record<InterfaceRole, InterfaceConfiguration>>({
    client: { ...initialSnapshot.clientInterface },
    cluster: { ...initialSnapshot.clusterInterface },
  });
  const [dnsConfig, setDnsConfig] = useState({
    domainName: initialSnapshot.domainName,
    dnsServer: initialSnapshot.dnsServer,
    dnsSecondary: initialSnapshot.dnsSecondary,
  });
  const [timeConfig, setTimeConfig] = useState(initialSnapshot.timeConfig);

  const [activeTab, setActiveTab] = useState<NetworkingTab>(initialSection ?? 'network');
  const [loadingConfig, setLoadingConfig] = useState(false);
  const [loadError, setLoadError] = useState<string | null>(null);
  const [savingConfig, setSavingConfig] = useState(false);
  const [saveMessage, setSaveMessage] = useState<string | null>(null);

  useEffect(() => {
    if (initialSection)
      setActiveTab(initialSection);
  }, [initialSection]);

  const updateInterfaceConfig = (role: InterfaceRole, patch: Partial<InterfaceConfiguration>) => {
    setInterfaceConfig((prev) => ({
      ...prev,
      [role]: {
        ...prev[role],
        ...patch,
      },
    }));
  };

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

  const hydrateFromApi = (payload?: Partial<NetworkingConfiguration>) => {
    const snapshot = buildSnapshot(payload ?? {});
    setInitialSnapshot(snapshot);
    setInterfaceConfig({
      client: { ...snapshot.clientInterface },
      cluster: { ...snapshot.clusterInterface },
    });
    setDnsConfig({
      domainName: snapshot.domainName,
      dnsServer: snapshot.dnsServer,
      dnsSecondary: snapshot.dnsSecondary,
    });
    setTimeConfig(snapshot.timeConfig);
  };

  useEffect(() => {
    let ignore = false;
    const load = async () => {
      setLoadingConfig(true);
      setLoadError(null);
      try {
        const response = await fetchWithAuth(NETWORKING_API_URL);
        const payload = await response.json();
        if (!ignore) {
          hydrateFromApi(payload ?? {});
        }
      } catch (err) {
        if (!ignore) {
          setLoadError('Unable to load networking settings.');
        }
      } finally {
        if (!ignore) {
          setLoadingConfig(false);
        }
      }
    };
    load();
    return () => {
      ignore = true;
    };
  }, []);

  const handleSave = async () => {
    const configuration: NetworkingConfiguration = {
      clientInterface: { ...interfaceConfig.client },
      clusterInterface: { ...interfaceConfig.cluster },
      domainName: dnsConfig.domainName,
      dnsServer: dnsConfig.dnsServer,
      dnsSecondary: dnsConfig.dnsSecondary,
      timeConfig,
    };
    setSaveMessage(null);
    setSavingConfig(true);
    try {
      await fetchWithAuth(NETWORKING_API_URL, {
        method: 'PUT',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(configuration),
      });
      setSaveMessage('Networking settings saved.');
      setInitialSnapshot(configuration);
      onSave?.(configuration);
    } catch (err) {
      setSaveMessage('Unable to save networking settings.');
    } finally {
      setSavingConfig(false);
    }
  };

  const handleCancel = () => {
    hydrateFromApi(initialSnapshot);
    onCancel?.();
  };

  const renderInterfaceSection = (role: InterfaceRole) => {
    const config = interfaceConfig[role];
    const meta = interfaceDetails[role];

    return (
      <div className="rounded-xl border border-border/50 bg-background/40 p-4">
        <div>
          <Label className="text-sm font-semibold" style={labelGradientStyle}>
            {meta.label}
          </Label>
          <p className="text-xs text-muted-foreground mt-1">{meta.helper}</p>
        </div>

        <Select
          value={config.interfaceName}
          onValueChange={(value) => updateInterfaceConfig(role, { interfaceName: value })}
        >
          <SelectTrigger className="mt-4">
            <SelectValue placeholder="Select interface" />
          </SelectTrigger>
          <SelectContent>
            {resolvedInterfaces.map((iface) => (
              <SelectItem key={`${role}-${iface}`} value={iface}>
                {iface}
              </SelectItem>
            ))}
          </SelectContent>
        </Select>

        <div className="mt-4 flex items-center gap-2">
          <Checkbox
            id={`${role}-dhcp`}
            checked={config.autoDhcp}
            onCheckedChange={(checked) => updateInterfaceConfig(role, { autoDhcp: checked === true })}
          />
          <Label htmlFor={`${role}-dhcp`} className="text-sm text-muted-foreground">
            IP / DHCP Auto
          </Label>
        </div>

        <div className="mt-4 grid gap-4 sm:grid-cols-2">
          <div>
            <Label
              htmlFor={`${role}-ip`}
              className="text-xs uppercase tracking-wide text-muted-foreground"
              style={labelGradientStyle}
            >
              IP Address
            </Label>
            <Input
              id={`${role}-ip`}
              placeholder="192.168.0.10"
              className="mt-1 text-sm"
              value={config.ipAddress}
              onChange={(event) => updateInterfaceConfig(role, { ipAddress: event.target.value })}
              disabled={config.autoDhcp}
            />
          </div>
          <div>
            <Label
              htmlFor={`${role}-mask`}
              className="text-xs uppercase tracking-wide text-muted-foreground"
              style={labelGradientStyle}
            >
              Netmask
            </Label>
            <Input
              id={`${role}-mask`}
              placeholder="255.255.255.0"
              className="mt-1 text-sm"
              value={config.netmask}
              onChange={(event) => updateInterfaceConfig(role, { netmask: event.target.value })}
              disabled={config.autoDhcp}
            />
          </div>
        </div>
      </div>
    );
  };

  return (
    <div className="flex h-full min-h-0 w-full flex-col bg-background text-foreground">
      <div className="flex flex-col items-center gap-3 px-6 pt-8 pb-4 text-center">
        <p className="text-xs uppercase tracking-[0.3em] text-muted-foreground">Bootstrap</p>
        <h1 className="text-3xl font-semibold" style={titleGradientStyle}>
          Global Network Settings
        </h1>
        <p className="text-sm text-muted-foreground">
          Configure connectivity, DNS, and network time protocol parameters for this appliance.
        </p>
      </div>

      <div className="flex-1">
        <div className="mx-auto flex w-full max-w-5xl flex-col px-6 pb-12">
          <Tabs
            className="w-full"
            value={activeTab}
            onValueChange={(value) => setActiveTab((value as NetworkingTab) ?? 'network')}
          >
            <TabsList className="mx-auto mb-4 flex">
              <TabsTrigger value="network">Network</TabsTrigger>
              <TabsTrigger value="dns">DNS</TabsTrigger>
              <TabsTrigger value="time">Time</TabsTrigger>
            </TabsList>

            <TabsContent value="network" className="mt-2">
              <Card>
                <CardHeader>
                  <CardTitle className="text-xl" style={titleGradientStyle}>
                    Network Configuration
                  </CardTitle>
                </CardHeader>
                <CardContent className="flex flex-col gap-6">
                  <p className="text-sm text-muted-foreground">
                    Assign physical or bonded interfaces to handle front-end traffic and inter-node
                    communications. DHCP automatically disables manual address entry.
                  </p>
                  <div className="grid gap-6 lg:grid-cols-2">
                    {(['client', 'cluster'] as InterfaceRole[]).map((role) => (
                      <Fragment key={role}>{renderInterfaceSection(role)}</Fragment>
                    ))}
                  </div>
                </CardContent>
              </Card>
            </TabsContent>

            <TabsContent value="dns" className="mt-2">
              <Card>
                <CardHeader>
                  <CardTitle className="text-xl" style={titleGradientStyle}>
                    DNS Configuration
                  </CardTitle>
                </CardHeader>
                <CardContent className="flex flex-col gap-4">
                  <p className="text-sm text-muted-foreground">
                    Provide the domain suffix and upstream DNS resolvers used by the cluster.
                  </p>
                  <div className="grid gap-4 md:grid-cols-2">
                    <div className="md:col-span-2">
                      <Label
                        htmlFor="domain-name"
                        className="text-sm font-medium"
                        style={labelGradientStyle}
                      >
                        Domain Name
                      </Label>
                      <Input
                        id="domain-name"
                        placeholder="cluster.example.com"
                        className="mt-1.5 text-sm"
                        value={dnsConfig.domainName}
                        onChange={(event) =>
                          setDnsConfig((prev) => ({ ...prev, domainName: event.target.value }))
                        }
                      />
                    </div>
                    <div>
                      <Label
                        htmlFor="dns-server"
                        className="text-sm font-medium"
                        style={labelGradientStyle}
                      >
                        DNS Server
                      </Label>
                      <Input
                        id="dns-server"
                        placeholder="10.0.0.10"
                        className="mt-1.5 text-sm"
                        value={dnsConfig.dnsServer}
                        onChange={(event) =>
                          setDnsConfig((prev) => ({ ...prev, dnsServer: event.target.value }))
                        }
                      />
                    </div>
                    <div>
                      <Label
                        htmlFor="dns-secondary"
                        className="text-sm font-medium"
                        style={labelGradientStyle}
                      >
                        DNS Secondary Server
                      </Label>
                      <Input
                        id="dns-secondary"
                        placeholder="10.0.0.11"
                        className="mt-1.5 text-sm"
                        value={dnsConfig.dnsSecondary}
                        onChange={(event) =>
                          setDnsConfig((prev) => ({ ...prev, dnsSecondary: event.target.value }))
                        }
                      />
                    </div>
                  </div>
                </CardContent>
              </Card>
            </TabsContent>

            <TabsContent value="time" className="mt-2">
              <Card>
                <CardHeader>
                  <CardTitle className="text-xl" style={titleGradientStyle}>
                    Time Protocol Configuration
                  </CardTitle>
                </CardHeader>
                <CardContent className="flex flex-col gap-4">
                  <p className="text-sm text-muted-foreground">
                    Paste the Chrony/NTP configuration snippet applied to all nodes. Each line is saved
                    verbatim.
                  </p>
                  <div>
                    <Label
                      htmlFor="time-config"
                      className="text-sm font-medium"
                      style={labelGradientStyle}
                    >
                      Time Config
                    </Label>
                    <Textarea
                      id="time-config"
                      rows={8}
                      className="mt-1.5 text-sm font-mono"
                      value={timeConfig}
                      onChange={(event) => setTimeConfig(event.target.value)}
                      placeholder={DEFAULT_TIME_CONFIG}
                    />
                  </div>
                </CardContent>
              </Card>
            </TabsContent>
          </Tabs>
        </div>
      </div>

      <div className="border-t border-border bg-background/80 px-6 py-4 backdrop-blur-sm">
        <div className="mx-auto flex w-full max-w-5xl flex-col gap-3">
          {loadError && (
            <div className="flex items-center gap-2 rounded-md bg-destructive/10 px-3 py-2 text-sm text-destructive">
              <AlertCircle className="size-4" />
              <span>{loadError}</span>
            </div>
          )}
          {saveMessage && (
            <div className="text-sm text-muted-foreground">{saveMessage}</div>
          )}
          <div className="flex justify-end gap-3">
            <Button
              variant="outline"
              onClick={handleCancel}
              disabled={isSaving || savingConfig || loadingConfig}
            >
              Cancel
            </Button>
            <Button
              onClick={handleSave}
              disabled={isSaving || savingConfig || loadingConfig}
            >
              {savingConfig ? (
                <>
                  <Save className="mr-2 size-4 animate-spin" />
                  Saving…
                </>
              ) : (
                'Save Settings'
              )}
            </Button>
          </div>
        </div>
      </div>
    </div>
  );
}
