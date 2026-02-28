import { useState, useEffect, FormEvent } from 'react';
import { Loader2, ShieldPlus } from 'lucide-react';
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogFooter,
  DialogHeader,
  DialogTitle,
} from './ui/dialog';
import { Button } from './ui/button';
import { Input } from './ui/input';
import { Label } from './ui/label';
import { Alert, AlertDescription, AlertTitle } from './ui/alert';

const API_BASE = import.meta?.env?.VITE_NODES_API_BASE_URL ?? 'http://localhost:8000/api/v1';
const NEW_TOKEN_ENDPOINT = `${API_BASE}/new_token`;

interface AddClientDialogProps {
  open: boolean;
  onClose: () => void;
  onTokenCreated?: () => void;
}

type TokenResponse = {
  bootstrap_token: string;
  t_type?: string;
  expires_at?: string | null;
  host_mid?: string;
};

const resolveApprovedBy = () => {
  try {
    const stored = localStorage.getItem('user');
    if (!stored) return 'webui';
    const user = JSON.parse(stored);
    return (
      user?.username ||
      user?.email ||
      user?.user?.email ||
      user?.id?.toString() ||
      user?.user_id?.toString() ||
      'webui'
    );
  } catch {
    return 'webui';
  }
};

export function AddClientDialog({ open, onClose, onTokenCreated }: AddClientDialogProps) {
  const [recipientEmail, setRecipientEmail] = useState('');
  const [machineUuid, setMachineUuid] = useState('');
  const [isSubmitting, setIsSubmitting] = useState(false);
  const [errorMessage, setErrorMessage] = useState<string | null>(null);
  const [tokenResponse, setTokenResponse] = useState<TokenResponse | null>(null);

  useEffect(() => {
    if (!open) {
      setRecipientEmail('');
      setMachineUuid('');
      setErrorMessage(null);
      setTokenResponse(null);
      setIsSubmitting(false);
    }
  }, [open]);

  const handleSubmit = async (event: FormEvent<HTMLFormElement>) => {
    event.preventDefault();
    if (!recipientEmail || !machineUuid) {
      setErrorMessage('Email and machine UUID are required.');
      return;
    }
    setIsSubmitting(true);
    setErrorMessage(null);
    setTokenResponse(null);
    try {
      const token = localStorage.getItem('authToken');
      const response = await fetch(NEW_TOKEN_ENDPOINT, {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
          ...(token ? { Authorization: `Token ${token}` } : {}),
        },
        credentials: 'include',
        body: JSON.stringify({
          command: 'newtoken',
          rec_email: recipientEmail.trim(),
          host_mid: machineUuid.trim(),
          t_type: 'client_join',
          approved_by: resolveApprovedBy(),
        }),
      });

      if (!response.ok) {
        const errData = await response.json().catch(() => ({}));
        throw new Error(errData?.error || errData?.detail || response.statusText);
      }

      const data = (await response.json()) as TokenResponse;
      setTokenResponse(data);
      onTokenCreated?.();
    } catch (err) {
      setErrorMessage(err instanceof Error ? err.message : 'Failed to generate token.');
    } finally {
      setIsSubmitting(false);
    }
  };

  return (
    <Dialog
      open={open}
      onOpenChange={(nextOpen) => {
        if (!nextOpen) {
          onClose();
        }
      }}
    >
      <DialogContent className="sm:max-w-lg">
        <DialogHeader>
          <DialogTitle className="flex items-center gap-2">
            <ShieldPlus className="h-5 w-5 text-primary" />
            Add Client
          </DialogTitle>
          <DialogDescription>
            Generate a one-time bootstrap token and email it to the client administrator.
          </DialogDescription>
        </DialogHeader>
        <form className="space-y-4" onSubmit={handleSubmit}>
          <div className="space-y-2">
            <Label htmlFor="client-email">Recipient Email</Label>
            <Input
              id="client-email"
              type="email"
              placeholder="admin@example.com"
              value={recipientEmail}
              onChange={(event) => setRecipientEmail(event.target.value)}
              required
              autoComplete="email"
            />
          </div>
          <div className="space-y-2">
            <Label htmlFor="client-machine">Client Machine UUID</Label>
            <Input
              id="client-machine"
              placeholder="00000000-0000-0000-0000-000000000000"
              value={machineUuid}
              onChange={(event) => setMachineUuid(event.target.value)}
              required
            />
            <p className="text-xs text-muted-foreground">
              Must match the client&apos;s machine_uuid reported by the Hive agent.
            </p>
          </div>

          {errorMessage && (
            <Alert variant="destructive">
              <AlertTitle>Request Failed</AlertTitle>
              <AlertDescription>{errorMessage}</AlertDescription>
            </Alert>
          )}

          {tokenResponse && (
            <Alert>
              <AlertTitle>Client Token Generated</AlertTitle>
              <AlertDescription>
                <div className="space-y-1 text-xs">
                  <div className="font-semibold text-muted-foreground">Token</div>
                  <div className="rounded border bg-muted px-2 py-1 font-mono text-sm break-all">
                    {tokenResponse.bootstrap_token}
                  </div>
                  {tokenResponse.expires_at && (
                    <p>Expires at: {tokenResponse.expires_at}</p>
                  )}
                </div>
              </AlertDescription>
            </Alert>
          )}

          <DialogFooter>
            <Button type="button" variant="outline" disabled={isSubmitting} onClick={onClose}>
              Cancel
            </Button>
            <Button type="submit" disabled={isSubmitting}>
              {isSubmitting ? (
                <>
                  <Loader2 className="mr-2 h-4 w-4 animate-spin" />
                  Generating…
                </>
              ) : (
                'Generate Token'
              )}
            </Button>
          </DialogFooter>
        </form>
      </DialogContent>
    </Dialog>
  );
}
