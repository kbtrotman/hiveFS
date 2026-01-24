import { useState, type CSSProperties } from 'react';
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
const labelGradientStyle: CSSProperties = {
  backgroundImage:
    'linear-gradient(90deg, #f97316 0%, var(--foreground, #e5e7eb) 35%, #38bdf8 80%, #0ea5e9 100%)',
  WebkitBackgroundClip: 'text',
  backgroundClip: 'text',
  WebkitTextFillColor: 'transparent',
  color: 'transparent',
};

const API_BASE =
  import.meta?.env?.VITE_BOOTSTRAP_API_BASE ?? 'http://localhost:8000/api/v1/bootstrap';
const CLUSTER_INIT_URL = `${API_BASE}/init`;
const NODE_JOIN_URL = `${API_BASE}/addnode`;
const FOREIGNER_URL = `${API_BASE}/addforeigner`;

type ClusterInitializationScreenProps = {
  statusData?: any;
  onActionComplete?: () => void;
};

const formatDisplayValue = (value: unknown) =>
  value === null || value === undefined ? '' : String(value);

export function ClusterInitializationScreen({
  statusData,
  onActionComplete,
}: ClusterInitializationScreenProps) {
  const [clusterName, setClusterName] = useState('');
  const [clusterDescription, setClusterDescription] = useState('');
  const [existingClusterNode, setExistingClusterNode] = useState('');
  const [joinToken, setJoinToken] = useState('');
  const [newNodeName, setNewNodeName] = useState('');
  const [foreignNode, setForeignNode] = useState('');
  const [foreignToken, setForeignToken] = useState('');
  const [submittingAction, setSubmittingAction] = useState<string | null>(null);
  const [feedbackMessage, setFeedbackMessage] = useState<string | null>(null);
  const [errorMessage, setErrorMessage] = useState<string | null>(null);

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

  const sendRequest = async (url: string, command: string, extra: Record<string, unknown>) => {
    setSubmittingAction(command);
    setFeedbackMessage(null);
    setErrorMessage(null);
    try {
      const payload = {
        ...basePayload,
        command,
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

  const handleAddForeigner = () => {
    sendRequest(FOREIGNER_URL, 'add_foreigner', {
      foreign_node: foreignNode,
      foreign_token: foreignToken,
    });
  };

  const clusterIdDisplay = formatDisplayValue(statusData?.cluster_id);
  const nodeIdDisplay = formatDisplayValue(statusData?.node_id);
  const initDisabled = submittingAction === 'cluster_init';
  const joinDisabled = submittingAction === 'node_join';
  const foreignDisabled = submittingAction === 'add_foreigner';

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

      <div className="w-full max-w-6xl grid grid-cols-1 gap-6 lg:grid-cols-2 lg:gap-10">
        <Card className="flex flex-col shadow-lg border-border min-h-[20rem]">
          <CardHeader>
            <CardTitle className="text-xl" style={titleGradientStyle}>
              Initialize a New Cluster
            </CardTitle>
          </CardHeader>
          <CardContent className="flex flex-col gap-5 flex-1">
            <div className="flex gap-4 flex-wrap">
              <div className="flex-1">
                <Label
                  htmlFor="cluster-id"
                  className="text-xs uppercase text-muted-foreground"
                  style={labelGradientStyle}
                >
                  Cluster ID
                </Label>
                <Input
                  id="cluster-id"
                  value={clusterIdDisplay}
                  disabled
                  className="mt-1 text-sm"
                />
              </div>
              <div className="flex-1">
                <Label
                  htmlFor="node-id"
                  className="text-xs uppercase text-muted-foreground"
                  style={labelGradientStyle}
                >
                  Node ID
                </Label>
                <Input
                  id="node-id"
                  value={nodeIdDisplay}
                  disabled
                  className="mt-1 text-sm"
                />
              </div>
            </div>

            <div>
              <Label htmlFor="cluster-name" className="text-sm font-medium" style={labelGradientStyle}>
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
              <Label htmlFor="cluster-description" className="text-sm font-medium" style={labelGradientStyle}>
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
            >
              {initDisabled ? 'Submitting...' : 'Initialize Cluster'}
            </Button>
          </CardContent>
        </Card>

        <Card className="flex flex-col shadow-lg border-border min-h-[20rem]">
          <CardHeader>
            <CardTitle className="text-xl" style={titleGradientStyle}>
              Add this Node to an Existing Cluster
            </CardTitle>
          </CardHeader>
          <CardContent className="flex flex-col gap-5 flex-1">
            <div>
              <Label htmlFor="existing-cluster-node" className="text-sm font-medium" style={labelGradientStyle}>
                Existing Cluster Node
              </Label>
              <Input
                id="existing-cluster-node"
                placeholder="Enter existing cluster node"
                className="mt-1.5 text-sm"
                value={existingClusterNode}
                onChange={(event) => setExistingClusterNode(event.target.value)}
              />
            </div>

            <div>
              <Label htmlFor="token-key" className="text-sm font-medium" style={labelGradientStyle}>
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
              <Label htmlFor="new-node-name" className="text-sm font-medium" style={labelGradientStyle}>
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

            <p className="text-[0.7rem] text-muted-foreground">
              Or add this node later from the admin console via “Add Node” in the Nodes view.
            </p>

            <Button
              className="w-full mt-auto"
              onClick={handleNodeJoin}
              disabled={joinDisabled}
            >
              {joinDisabled ? 'Submitting...' : 'Join Cluster'}
            </Button>
          </CardContent>
        </Card>
      </div>

      <div className="w-full max-w-6xl">
        <Card className="shadow-lg border-border">
          <CardHeader>
            <CardTitle className="text-lg" style={titleGradientStyle}>
              Optional: Add This Cluster to a Foreign Console
            </CardTitle>
          </CardHeader>
          <CardContent className="flex flex-col gap-5">
            <p className="text-sm text-muted-foreground">
              Expose this cluster to others so admin consoles can manage a multi-cluster environment.
            </p>

            <div className="grid grid-cols-1 gap-4 md:grid-cols-2">
              <div>
                <Label htmlFor="foreign-node" className="text-sm font-medium" style={labelGradientStyle}>
                  Foreign Node or IP
                </Label>
                <Input
                  id="foreign-node"
                  placeholder="Enter foreign node name or IP"
                  className="mt-1.5 text-sm"
                  value={foreignNode}
                  onChange={(event) => setForeignNode(event.target.value)}
                />
              </div>
              <div>
                <Label htmlFor="foreign-token" className="text-sm font-medium" style={labelGradientStyle}>
                  Token from Foreign Cluster
                </Label>
                <Input
                  id="foreign-token"
                  placeholder="Enter token"
                  className="mt-1.5 text-sm"
                  value={foreignToken}
                  onChange={(event) => setForeignToken(event.target.value)}
                />
              </div>
            </div>
            <p className="text-[0.7rem] text-muted-foreground">
              Or add this cluster later from the admin console via “Add Foreign Cluster” in the Clusters view.
            </p>
            <Button
              className="w-full md:w-auto self-start"
              onClick={handleAddForeigner}
              disabled={foreignDisabled}
            >
              {foreignDisabled ? 'Submitting...' : 'Add Foreign Cluster'}
            </Button>
          </CardContent>
        </Card>
      </div>
    </div>
  );
}
