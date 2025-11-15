'use client';
import React, { useMemo } from 'react';
import type { ProColumns, ProTableProps } from '@ant-design/pro-components';
import { ProTable } from '@ant-design/pro-components';
import { usePlasmicCanvasContext } from '@plasmicapp/host';

type AnyRec = Record<string, any>;

export interface ProTableBrowserProps<T extends AnyRec = AnyRec> {
  /** Let Plasmic style/position the root */
  className?: string;

  /** Your list endpoint; should return array (or {results,total}) */
  baseUrl?: string; // e.g. "/api/v1/files"

  /** Optional bearer token */
  authToken?: string;

  /** Extra headers as JSON (e.g. {"X-Tenant":"acme"}) */
  extraHeadersJson?: string;

  /** Columns as JSON — an array of ProColumns */
  columnsJson?: string;

  /** Record key field (default 'id' then 'key') */
  rowKey?: string;

  /** Table height (for vertical scroll) */
  height?: string;

  /** Show canned data in Studio if API isn’t reachable */
  designTimeSample?: boolean;

  /** Called when a row is clicked */
  onRowClick?: (payload: { record: AnyRec }) => void;

  /** Whether to show the toolbar search (off by default) */
  enableSearch?: boolean;
}

function parseJson<T>(s?: string, fallback: T): T {
  if (!s) return fallback;
  try {
    return JSON.parse(s) as T;
  } catch {
    return fallback;
  }
}

export function TableBrowser({
  className,
  baseUrl = '/api/v1/filelist',
  authToken,
  extraHeadersJson,
  columnsJson,
  rowKey = 'id',
  height = '60vh',
  designTimeSample = true,
  onRowClick,
  enableSearch = false,
}: ProTableBrowserProps) {
  const inCanvas = !!(usePlasmicCanvasContext && usePlasmicCanvasContext());

  const extraHeaders = useMemo(
    () => parseJson<Record<string, string>>(extraHeadersJson, {}),
    [extraHeadersJson]
  );

  const headers = useMemo(() => {
    const h: Record<string, string> = { Accept: 'application/json' };
    if (authToken) h.Authorization = `Bearer ${authToken}`;
    return { ...h, ...extraHeaders };
  }, [authToken, extraHeaders]);

  // Default sample when designing in Studio (so you can see columns/rows)
  const sampleData: AnyRec[] = useMemo(
    () => [
      { id: '1', name: 'readme.txt', size: 1024, type: 'file' },
      { id: '2', name: 'photos', size: 0, type: 'dir' },
      { id: '3', name: 'notes.md', size: 2048, type: 'file' },
    ],
    []
  );

  // Columns: either from JSON prop or inferred from sample
  const parsedColumns = useMemo(() => {
    const fromProp = parseJson<ProColumns<AnyRec>[]>(columnsJson, []);
    if (fromProp.length) return fromProp;
    // fallback: infer from sample fields
    const keys = Object.keys(sampleData[0] ?? { id: 'id', name: 'name' });
    return keys.map((k) => ({
      title: k,
      dataIndex: k,
      ellipsis: true,
    })) as ProColumns<AnyRec>[];
  }, [columnsJson, sampleData]);

  // ProTable expects a request() that returns { data, total, success }
  const request: ProTableProps<AnyRec, AnyRec>['request'] = async (
    params,
    sort,
    filter
  ) => {
    // Map ProTable params to your API. Common patterns:
    // pageSize -> ?page_size=, current -> ?page=, keyword -> ?q=
    const qs = new URLSearchParams();
    if (params?.pageSize != null) qs.set('page_size', String(params.pageSize));
    if (params?.current != null) qs.set('page', String(params.current));
    if (params?.keyword) qs.set('q', String(params.keyword));

    // Flatten sort into ?sort=field&order=asc|desc (customize for your API)
    const [[sortField, sortOrder]] = Object.entries(sort || {});
    if (sortField && sortOrder) {
      qs.set('sort', sortField);
      qs.set('order', sortOrder === 'ascend' ? 'asc' : 'desc');
    }

    // You can also append filter if your API supports it
    // Object.entries(filter || {}).forEach(([k, v]) => { ... });

    const url = qs.toString() ? `${baseUrl}?${qs.toString()}` : baseUrl;

    try {
      const res = await fetch(url, { headers, credentials: 'include' });
      if (!res.ok) throw new Error(`HTTP ${res.status}`);
      const body = await res.json();

      // Accept either array or {results, total}
      const data = Array.isArray(body) ? body : body.results ?? [];
      const total = Array.isArray(body) ? data.length : body.total ?? data.length;

      return { data, total, success: true };
    } catch (e) {
      // In Studio, show sample so the designer sees something
      if (inCanvas && designTimeSample) {
        return { data: sampleData, total: sampleData.length, success: true };
      }
      return { data: [], total: 0, success: false };
    }
  };

  // Derive rowKey (prefer provided rowKey, but fall back gracefully)
  const getRowKey = (rec: AnyRec) =>
    rec?.[rowKey] ?? rec?.key ?? rec?.id ?? String(Math.random());

  return (
    <div className={className} style={{ height }}>
      <ProTable<AnyRec>
        rowKey={getRowKey}
        className="pro-table-browser"
        columns={parsedColumns}
        request={request}
        search={enableSearch ? {} : false}
        pagination={{ pageSize: 10 }}
        scroll={{ y: `calc(${height} - 56px)` }} // keep header visible
        dateFormatter="string"
        onRow={(record) => ({
          onClick: () => onRowClick?.({ record }),
        })}
        toolBarRender={false}
      />
    </div>
  );
}
