import React, { useEffect, useState, useCallback } from "react";
import { Tree } from "antd";

type ApiNode = { id: string; label: string; hasChildren?: boolean };
type AntNode = { key: string; title: string; isLeaf?: boolean; children?: AntNode[] };

const { DirectoryTree } = Tree;

export default function HiveDirectoryTree({
  endpoint,
  authToken,
  rootParentParam,   // optional: if your root needs a special parent sentinel
  className,
}: {
  endpoint: string;
  authToken?: string;
  rootParentParam?: string;
  className?: string;
}) {
  const [treeData, setTreeData] = useState<AntNode[]>([]);
  const [loadedKeys, setLoadedKeys] = useState<Set<string>>(new Set());

  const fetchChildren = useCallback(
    async (parentId?: string): Promise<AntNode[]> => {
      const url = new URL(endpoint);
      if (parentId || rootParentParam) {
        url.searchParams.set("parent", parentId ?? (rootParentParam as string));
      }
      const res = await fetch(url.toString(), {
        headers: authToken ? { Authorization: `Bearer ${authToken}` } : undefined,
        credentials: "include", // if using session/cookie auth
      });
      if (!res.ok) throw new Error(`Tree fetch failed: ${res.status}`);
      const items: ApiNode[] = await res.json();
      return items.map((n) => ({
        key: n.id,
        title: n.label,
        isLeaf: !n.hasChildren,
      }));
    },
    [endpoint, authToken, rootParentParam]
  );

  // initial root load
  useEffect(() => {
    fetchChildren(undefined)
      .then((kids) => setTreeData(kids))
      .catch(console.error);
  }, [fetchChildren]);

  // helper to insert children into a node
  const insertChildren = (nodes: AntNode[], key: string, children: AntNode[]): AntNode[] =>
    nodes.map((n) => {
      if (n.key === key) {
        return { ...n, children };
      }
      if (n.children) {
        return { ...n, children: insertChildren(n.children, key, children) };
      }
      return n;
    });

  const onLoadData = async ({ key, children }: any) => {
    if (children || loadedKeys.has(key)) return; // already loaded
    const kids = await fetchChildren(String(key));
    setTreeData((td) => insertChildren(td, String(key), kids));
    setLoadedKeys((s) => new Set(s).add(String(key)));
  };

  return (
    <div className={className}>
      <DirectoryTree
        treeData={treeData}
        loadData={onLoadData}           // lazy-load children on expand
        multiple={false}
        expandAction="doubleClick"
      />
    </div>
  );
}
