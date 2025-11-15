// pages/plasmic-host.tsx
import * as React from "react";
import { PlasmicCanvasHost, registerComponent } from "@plasmicapp/host";
import dynamic from "next/dynamic";


// Lazy-load the **named** export TreeBrowser
const TreeBrowserDynamic = dynamic(
  () => import("@/components/TreeBrowser").then((mod) => mod.TreeBrowser),
  { ssr: false }
);

// Same idea for TableBrowser, assuming it's also a named export:
const TableBrowserDynamic = dynamic(
  () => import("@/components/TableBrowser").then((mod) => mod.TableBrowser),
  { ssr: false }
);

registerComponent(TreeBrowserDynamic, {
  name: "TreeBrowser",
  importPath: "@/components/TreeBrowser",
  importName: "TreeBrowser",   // <-- IMPORTANT: match the named export
  props: {
    className: { type: "class" },
    baseUrl: { type: "string", defaultValue: "/api/v1/tree" },
    parentParam: { type: "string", defaultValue: "parent" },
    rootParentValue: { type: "string", defaultValue: "root" },
    height: { type: "string", defaultValue: "60vh" },
    designTimeSample: { type: "boolean", defaultValue: true },
    onSelect: {
      type: "eventHandler",
      argTypes: [{ name: "node", type: "object" }],
    },
  },
});

registerComponent(TableBrowserDynamic, {
  name: "TableBrowser",
  importPath: "@/components/TableBrowser",
  importName: "TableBrowser",  // <-- if it’s `export function TableBrowser`
  props: {
    className: { type: "class" },
    baseUrl: { type: "string", defaultValue: "/api/v1/filelist" },
    authToken: { type: "string", advanced: true },
    extraHeadersJson: { type: "string", advanced: true },
    columnsJson: {
      type: "string",
      advanced: true,
      description: "JSON array of ProColumns",
    },
    rowKey: { type: "string", defaultValue: "id" },
    height: { type: "string", defaultValue: "60vh" },
    enableSearch: { type: "boolean", defaultValue: false },
    designTimeSample: { type: "boolean", defaultValue: true },
    onRowClick: {
      type: "eventHandler",
      argTypes: [{ name: "payload", type: "object" }],
    },
  },
});

export default function PlasmicHost() {
  return <PlasmicCanvasHost />;
}
