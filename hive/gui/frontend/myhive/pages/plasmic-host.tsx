// pages/plasmic-host.tsx  ← or keep your name, but match it in Plasmic settings
import * as React from "react";
import { PlasmicCanvasHost, registerComponent } from "@plasmicapp/host";

import HiveDirectoryTree from "../components/HiveDirectoryTree";

registerComponent(HiveDirectoryTree, {
  name: "DirectoryTree",
  props: {
    endpoint: { type: "string", defaultValue: "http://localhost:8000/api/v1/tree" },
    authToken: { type: "string", defaultValueHint: "Optional Bearer token" },
    rootParentParam: { type: "string", defaultValueHint: "" },
    className: { type: "class" }, // <-- important
  },
});

export default function PlasmicHost() {
  return <PlasmicCanvasHost />;
}