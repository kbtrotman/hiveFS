import { useEffect, useState, type CSSProperties } from 'react';
import { Card, CardContent, CardHeader, CardTitle } from './ui/card';
import { Input } from './ui/input';
import { Label } from './ui/label';
import { Button } from './ui/button';

const titleGradientStyle: CSSProperties = {
  backgroundImage:
    'linear-gradient(90deg, #0ea5e9 0%, #38bdf8 40%, var(--foreground, #e5e7eb) 75%, #f97316 100%)',
  WebkitBackgroundClip: 'text',
  backgroundClip: 'text',
  WebkitTextFillColor: 'transparent',
  color: 'transparent',
};
const API_BASE =
  import.meta?.env?.VITE_BOOTSTRAP_API_BASE ?? 'http://localhost:8000/api/v1/bootstrap';
const CLUSTER_INIT_URL = `${API_BASE}/init`;
const NODE_JOIN_URL = `${API_BASE}/addnode`;
// Base URL (no channel query string). Example: http://localhost:8000/events/
const EVENTS_BASE = import.meta?.env?.VITE_BOOTSTRAP_EVENTS_URL ?? 'http://localhost:8000/events/';

type ClusterInitializationScreenProps = {
  statusData?: any;
  onActionComplete?: () => void;
  userId?: string | number | null;
};

const formatDisplayValue = (value: unknown) =>
  value === null || value === undefined ? '' : String(value);

export function ClusterInitializationScreen({
  statusData,
  onActionComplete,
  userId,
}: ClusterInitializationScreenProps) {
  const [clusterName, setClusterName] = useState('');
  const [clusterDescription, setClusterDescription] = useState('');
  const [existingClusterNode, setExistingClusterNode] = useState('');
  const [joinToken, setJoinToken] = useState('');
  const [newNodeName, setNewNodeName] = useState('');
  const [submittingAction, setSubmittingAction] = useState<string | null>(null);
  const [feedbackMessage, setFeedbackMessage] = useState<string | null>(null);
  const [errorMessage, setErrorMessage] = useState<string | null>(null);
  const [eventMessages, setEventMessages] = useState<string[]>([]);
  const [eventStreamError, setEventStreamError] = useState<string | null>(null);
  const [activeJobId, setActiveJobId] = useState<string | null>(null);

  const normalizedUserId = userId === undefined || userId === null ? null : String(userId);

  const basePayload = {
    node_id: statusData?.node_id ?? null,
    cluster_id: statusData?.cluster_id ?? null,
    cluster_state: statusData?.cluster_state ?? 'unconfigured',
    database_state: statusData?.database_state ?? 'unconfigured',
    kv_state: statusData?.kv_state ?? 'unconfigured',
    cont1_state: statusData?.cont1_state ?? 'unconfigured',
    cont2_state: statusData?.cont2_state ?? 'unconfigured',
    min_nodes_req: statusData?.min_nodes_req ?? 0,
    bootstrap_token: statusData?.bootstrap_token ?? null,
    first_boot_ts: statusData?.first_boot_ts ?? '',
    hive_version: statusData?.hive_version ?? null,
    hive_patch_level: statusData?.hive_patch_level ?? null,
    config_status: statusData?.config_status ?? 'IDLE',
    config_progress: statusData?.config_progress ?? '0%',
    config_msg: statusData?.config_msg ?? '',
    pub_key: statusData?.pub_key ?? null,
  };

  const buildEventStreamUrl = (channel: string) => {
    const base = String(EVENTS_BASE).split('?')[0];
    const sep = base.includes('?') ? '&' : '?';
    return `${base}${sep}channel=${encodeURIComponent(channel)}`;
  };

  const formatEventLine = (raw: string) => {
    // Supports either plain strings or JSON payloads from send_event.
    // If the payload has a common message field, prefer that.
    try {
      const obj = JSON.parse(raw);
      const msg =
        obj?.msg ??
        obj?.message ??
        obj?.text ??
        obj?.data?.msg ??
        obj?.data?.message;

      if (typeof msg === 'string' && msg.length > 0) {
        const lvl = typeof obj?.level === 'string' ? obj.level.toUpperCase() : null;
        const phase = typeof obj?.phase === 'string' ? obj.phase : null;
        const prefixParts = [lvl, phase].filter(Boolean);
        const prefix = prefixParts.length ? `[${prefixParts.join(':')}] ` : '';
        return `${prefix}${msg}`;
      }

      // Fall back to a compact JSON string.
      return JSON.stringify(obj);
    } catch {
      return raw;
    }
  };

  const sendRequest = async (url: string, command: string, extra: Record<string, unknown>) => {
    setSubmittingAction(command);
    setFeedbackMessage(null);
    setErrorMessage(null);
    try {
      const payload = {
        ...basePayload,
        command,
        user_id: normalizedUserId,
        ...extra,
      };
      const response = await fetch(url, {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
        },
        credentials: 'include',
        body: JSON.stringify(payload),
      });
      const json = await response.json().catch(() => ({}));
      if (!response.ok || json?.ok === false) {
        throw new Error(json?.error || json?.message || 'Request failed');
      }

      // If the backend returns a job id, switch the event stream to a per-job channel.
      // Expected shapes: { job_id: "..." } or { jobId: "..." }.
      const jobIdFromResponse =
        (typeof json?.job_id === 'string' && json.job_id) ||
        (typeof json?.jobId === 'string' && json.jobId) ||
        null;

      if (jobIdFromResponse) {
        setActiveJobId(jobIdFromResponse);
        setEventMessages([]);
        setEventStreamError(null);
      }

      setFeedbackMessage(`${command.replace(/_/g, ' ')} submitted successfully.`);
      onActionComplete?.();
    } catch (err) {
      setErrorMessage(err instanceof Error ? err.message : 'Request failed');
    } finally {
      setSubmittingAction(null);
    }
  };

  const handleClusterInit = () => {
    sendRequest(CLUSTER_INIT_URL, 'cluster_init', {
      cluster_name: clusterName,
      cluster_desc: clusterDescription,
    });
  };

  const handleNodeJoin = () => {
    sendRequest(NODE_JOIN_URL, 'node_join', {
      node_name: newNodeName,
      bootstrap_token: joinToken,
      existing_cluster_node: existingClusterNode,
    });
  };

  const clusterIdDisplay = formatDisplayValue(
    statusData?.cluster_id ?? 'To be Created...'
  ) || 'To be Created...';
  const nodeIdDisplay = formatDisplayValue(
    statusData?.node_id ?? 'To be Created...'
  ) || 'To be Created...';
  const initDisabled = submittingAction === 'cluster_init';
  const joinDisabled = submittingAction === 'node_join';

  // Connect to a per-job channel once the backend returns a job id.
  // This keeps your normal API calls separate from the streaming GET.
  useEffect(() => {
    if (!activeJobId) return;

    const channel = `job-${activeJobId}`;
    const source = new EventSource(buildEventStreamUrl(channel), { withCredentials: true });

    source.onmessage = (event) => {
      const line = formatEventLine(event.data);
      setEventMessages((prev) => {
        const next = [...prev, line];
        if (next.length > 200) next.shift();
        return next;
      });
      setEventStreamError(null);
    };

    // Also handle typed events if you emit types like "log", "status", "progress".
    const typedHandler = (event: MessageEvent) => {
      const line = formatEventLine(event.data);
      setEventMessages((prev) => {
        const next = [...prev, line];
        if (next.length > 200) next.shift();
        return next;
      });
      setEventStreamError(null);
    };
    source.addEventListener('log', typedHandler);
    source.addEventListener('status', typedHandler);
    source.addEventListener('progress', typedHandler);

    source.onerror = () => {
      setEventStreamError('Disconnected from event stream. Retrying…');
    };

    return () => {
      source.close();
    };
  }, [activeJobId]);

  return (
    <div className="w-full h-full bg-background text-foreground flex flex-col items-center px-6 py-6 gap-4">
      <div className="w-full max-w-5xl text-center space-y-2">
        <p className="text-sm uppercase tracking-wide text-muted-foreground">
          Bootstrap Ready
        </p>
        <h1 className="text-3xl font-semibold" style={titleGradientStyle}>
          New Node Detected — Choose an Option to Continue
        </h1>
        <p className="text-base text-muted-foreground">
          Initialize a fresh cluster or join this node to an existing one. Both actions
          require a valid token to complete.
        </p>
        {(feedbackMessage || errorMessage) && (
          <p
            className={`text-sm ${
              errorMessage ? 'text-red-500' : 'text-emerald-400'
            }`}
          >
            {errorMessage ?? feedbackMessage}
          </p>
        )}
      </div>

      <div className="w-full max-w-6xl grid grid-cols-1 gap-6 lg:grid-cols-2 lg:gap-10 items-start">
        <Card className="flex flex-col shadow-lg border-border min-h-[24rem]">
          <CardHeader>
            <CardTitle className="text-xl" style={titleGradientStyle}>
              Initialize a New Cluster
            </CardTitle>
          </CardHeader>
          <CardContent className="flex flex-col gap-6 flex-1">
            <div className="flex gap-4 flex-wrap">
              <div className="flex-1">
                <p className="text-xs uppercase text-white/80">Cluster ID</p>
                <div className="mt-1 text-sm rounded-md border border-border bg-card/60 px-3 py-2 text-white/90">
                  {clusterIdDisplay}
                </div>
              </div>
              <div className="flex-1">
                <p className="text-xs uppercase text-white/80">Node ID</p>
                <div className="mt-1 text-sm rounded-md border border-border bg-card/60 px-3 py-2 text-white/90">
                  {nodeIdDisplay}
                </div>
              </div>
            </div>

            <div>
              <Label htmlFor="cluster-name" className="text-sm font-medium text-white">
                Cluster Name
              </Label>
              <Input
                id="cluster-name"
                placeholder="Enter cluster name"
                className="mt-1.5 text-sm"
                value={clusterName}
                onChange={(event) => setClusterName(event.target.value)}
              />
            </div>

            <div>
              <Label htmlFor="cluster-description" className="text-sm font-medium text-white">
                Cluster Description
              </Label>
              <Input
                id="cluster-description"
                placeholder="Enter cluster description"
                className="mt-1.5 text-sm"
                value={clusterDescription}
                onChange={(event) => setClusterDescription(event.target.value)}
              />
            </div>

            <div className="flex-1" />
            <Button
              className="w-full mt-auto"
              onClick={handleClusterInit}
              disabled={initDisabled}
              type="button"
            >
              {initDisabled ? 'Submitting...' : 'Initialize Cluster'}
            </Button>
          </CardContent>
        </Card>

        <Card className="flex flex-col shadow-lg border-border min-h-[24rem]">
          <CardHeader>
            <CardTitle className="text-xl" style={titleGradientStyle}>
              Add this Node to an Existing Cluster
            </CardTitle>
            <p className="text-[0.7rem] text-muted-foreground italic">
              (Or add this node later from the admin console via “Add Node” in the Nodes view.)
            </p>
          </CardHeader>
          <CardContent className="flex flex-col gap-6 flex-1">
            <div>
              <Label htmlFor="existing-cluster-node" className="text-sm font-medium text-white">
                Existing Cluster UUID
              </Label>
              <Input
                id="existing-cluster-node"
                placeholder="Enter the cluster's UUID the node is to be added to"
                className="mt-1.5 text-sm"
                value={existingClusterNode}
                onChange={(event) => setExistingClusterNode(event.target.value)}
              />
            </div>

            <div>
              <Label htmlFor="token-key" className="text-sm font-medium text-white">
                Token Key
              </Label>
              <Input
                id="token-key"
                placeholder="Enter token key"
                className="mt-1.5 text-sm"
                value={joinToken}
                onChange={(event) => setJoinToken(event.target.value)}
              />
            </div>

            <div>
              <Label htmlFor="new-node-name" className="text-sm font-medium text-white">
                New Node Name
              </Label>
              <Input
                id="new-node-name"
                placeholder="Enter new node name"
                className="mt-1.5 text-sm"
                value={newNodeName}
                onChange={(event) => setNewNodeName(event.target.value)}
              />
            </div>

            <div className="flex-1" />
            <Button
              className="w-full mt-auto"
              onClick={handleNodeJoin}
              disabled={joinDisabled}
              type="button"
            >
              {joinDisabled ? 'Submitting...' : 'Join Cluster'}
            </Button>
          </CardContent>
        </Card>
      </div>

      <Card className="w-full max-w-6xl shadow-lg border-border">
        <CardHeader>
          <CardTitle className="text-xl" style={titleGradientStyle}>
            Action Event Log:
          </CardTitle>
        </CardHeader>
        <CardContent className="space-y-2">
          <Label className="text-sm font-medium text-white">Live Events</Label>
          <div className="h-48 overflow-y-auto rounded-md border border-border bg-card/60 px-3 py-2 text-xs font-mono text-white/90 whitespace-pre-wrap">
            {eventMessages.length === 0
              ? eventStreamError ?? (activeJobId ? 'Waiting for events…' : 'Start an action to begin streaming logs…')
              : eventMessages.join('\n')}
          </div>
          {eventStreamError && eventMessages.length > 0 && (
            <p className="text-xs text-amber-300">{eventStreamError}</p>
          )}
        </CardContent>
      </Card>
    </div>
  );
}
