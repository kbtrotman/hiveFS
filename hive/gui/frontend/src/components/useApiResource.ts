import { useCallback, useEffect, useMemo, useRef, useState } from 'react';

export const API_BASE =
  import.meta?.env?.VITE_NODES_API_BASE_URL ?? 'http://localhost:8000/api/v1';

export interface UseApiResourceOptions<T> {
  initialData: T;
  enabled?: boolean;
  pollInterval?: number;
  transform?: (raw: unknown) => T;
}

export interface UseApiResourceResult<T> {
  data: T;
  isLoading: boolean;
  error: string | null;
  refresh: () => Promise<void>;
}

export function useApiResource<T>(
  path: string,
  { initialData, enabled = true, pollInterval = 0, transform }: UseApiResourceOptions<T>,
): UseApiResourceResult<T> {
  const [data, setData] = useState<T>(initialData);
  const [isLoading, setIsLoading] = useState<boolean>(true);
  const [error, setError] = useState<string | null>(null);
  const mountedRef = useRef(true);
  const normalizedPath = path.replace(/^\/+/, '');

  useEffect(() => {
    return () => {
      mountedRef.current = false;
    };
  }, []);

  const fetchResource = useCallback(async () => {
    if (!enabled || !mountedRef.current) {
      return;
    }

    setIsLoading(true);
    setError(null);

    try {
      const response = await fetch(`${API_BASE}/${normalizedPath}`);
      if (!response.ok) {
        throw new Error(`Failed to load ${normalizedPath} (${response.status})`);
      }

      const payload = await response.json();
      if (!mountedRef.current) {
        return;
      }

      setData(transform ? transform(payload) : (payload as T));
    } catch (err) {
      if (!mountedRef.current) {
        return;
      }
      setError(err instanceof Error ? err.message : 'Unable to load data');
    } finally {
      if (mountedRef.current) {
        setIsLoading(false);
      }
    }
  }, [enabled, normalizedPath, transform]);

  useEffect(() => {
    fetchResource();
  }, [fetchResource]);

  useEffect(() => {
    if (!pollInterval || pollInterval <= 0) {
      return;
    }
    const id = window.setInterval(() => {
      fetchResource();
    }, pollInterval);
    return () => window.clearInterval(id);
  }, [fetchResource, pollInterval]);

  return useMemo(
    () => ({
      data,
      isLoading,
      error,
      refresh: fetchResource,
    }),
    [data, error, fetchResource, isLoading],
  );
}
