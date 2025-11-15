'use client';
import React, { useCallback, useEffect, useMemo, useState } from 'react';
import { Tree, Spin } from 'antd';
import type { DataNode, EventDataNode } from 'antd/es/tree';
import { usePlasmicCanvasContext } from '@plasmicapp/host'; // for design-time mock (optional)

const { DirectoryTree } = Tree;

type ApiNode = { key: string; title: string; isLeaf?: boolean; hasChildren?: boolean };
type FieldNames = { key: string; title: string; isLeaf?: string; hasChildren?: string };

export interface TreeBrowserProps {
  className?: string;                 // <-- allow Plasmic to style/position root
  baseUrl?: string;
  parentParam?: string;
  rootParentValue?: string;
  authToken?: string;
  extraHeadersJson?: string;
  fieldNamesJson?: string;
  onSelect?: (node: { key: string; title: string; isLeaf?: boolean }) => void;
  height?: string;
  /** Optional: show sample nodes in Studio when API is unavailable */
  designTimeSample?: boolean;
}

function applyFieldMap(n: any, f: FieldNames): ApiNode {
  return {
    key: String(n[f.key]),
    title: String(n[f.title]),
    isLeaf: f.isLeaf ? Boolean(n[f.isLeaf]) : undefined,
    hasChildren: f.hasChildren ? Boolean(n[f.hasChildren]) : undefined,
  };
}

export function TreeBrowser({
  className,
  baseUrl = '/api/v1/tree',
  parentParam = 'parent',
  rootParentValue = 'root',
  authToken,
  extraHeadersJson,
  fieldNamesJson,
  onSelect,
  height = '60vh',
  designTimeSample = true,
}: TreeBrowserProps) {
  const inCanvas = !!(usePlasmicCanvasContext && usePlasmicCanvasContext());
  const [treeData, setTreeData] = useState<DataNode[] | null>(null);
  const [loading, setLoading] = useState(false);

  const fieldNames: FieldNames = useMemo(() => {
    try {
      return { key: 'key', title: 'title', isLeaf: 'isLeaf', hasChildren: 'hasChildren', ...(fieldNamesJson ? JSON.parse(fieldNamesJson) : {}) };
    } catch {
      return { key: 'key', title: 'title', isLeaf: 'isLeaf', hasChildren: 'hasChildren' };
    }
  }, [fieldNamesJson]);

  const extraHeaders = useMemo(() => {
    try { return extraHeadersJson ? JSON.parse(extraHeadersJson) : {}; } catch { return {}; }
  }, [extraHeadersJson]);

  const buildHeaders = useCallback(() => {
    const h: Record<string, string> = { Accept: 'application/json' };
    if (authToken) h.Authorization = `Bearer ${authToken}`;
    return { ...h, ...extraHeaders };
  }, [authToken, extraHeaders]);

  const toDataNodes = (nodes: ApiNode[]): DataNode[] =>
    nodes.map((n) => ({
      key: n.key,
      title: n.title,
      isLeaf: n.isLeaf ?? (n.hasChildren === false ? true : undefined),
      children: n.isLeaf ? undefined : [],
    }));

  const insertChildren = (list: DataNode[], key: React.Key, children: DataNode[]): DataNode[] =>
    list.map((node) =>
      node.key === key
        ? { ...node, children }
        : node.children && node.children.length
        ? { ...node, children: insertChildren(node.children, key, children) }
        : node
    );

  const fetchNodes = useCallback(async (parent?: string): Promise<ApiNode[]> => {
    const url = parent != null ? `${baseUrl}?${encodeURIComponent(parentParam)}=${encodeURIComponent(parent)}` : baseUrl;
    const res = await fetch(url, { headers: buildHeaders(), credentials: 'include' });
    if (!res.ok) throw new Error(`API ${res.status}`);
    const data = await res.json();
    const arr = Array.isArray(data) ? data : data?.results ?? [];
    return arr.map((n: any) => applyFieldMap(n, fieldNames));
  }, [baseUrl, parentParam, fieldNames, buildHeaders]);

  // Design-time mock if enabled and Canvas can’t fetch
  const sampleNodes: DataNode[] = useMemo(
    () => [
      { key: 'vol-1', title: 'Volume 1', children: [{ key: 'file-1', title: 'readme.txt', isLeaf: true }] },
      { key: 'vol-2', title: 'Volume 2', children: [] },
    ],
    []
  );

  useEffect(() => {
    let cancelled = false;
    (async () => {
      setLoading(true);
      try {
        const nodes = await fetchNodes(rootParentValue);
        if (!cancelled) setTreeData(toDataNodes(nodes));
      } catch {
        // If in Studio and allowed, show sample data for design convenience
        if (!cancelled) setTreeData(inCanvas && designTimeSample ? sampleNodes : []);
      } finally {
        if (!cancelled) setLoading(false);
      }
    })();
    return () => { cancelled = true; };
  }, [fetchNodes, rootParentValue, inCanvas, designTimeSample, sampleNodes]);

  const loadData = async (treeNode: EventDataNode): Promise<void> => {
    if (treeNode.children && treeNode.children.length) return;
    const key = String(treeNode.key);
    const children = await fetchNodes(key);
    setTreeData((prev) => (prev ? insertChildren(prev, key, toDataNodes(children)) : toDataNodes(children)));
  };

  return (
    <div className={className} style={{ height, overflow: 'auto', border: '1px solid #eee', borderRadius: 8 }}>
      {loading && !treeData ? (
        <div style={{ padding: 12 }}><Spin /> Loading…</div>
      ) : (treeData?.length ?? 0) === 0 ? (
        <div style={{ padding: 12, color: '#888' }}>No items</div>
      ) : (
        <DirectoryTree
          treeData={treeData ?? []}
          loadData={loadData}
          onSelect={(_, info) => {
            const n = info.node as any;
            onSelect?.({ key: String(n.key), title: String(n.title), isLeaf: !!n.isLeaf });
          }}
          multiple={false}
          expandAction="doubleClick"
          // defaultExpandAll  // uncomment if you want everything expanded by default
        />
      )}
    </div>
  );
}
