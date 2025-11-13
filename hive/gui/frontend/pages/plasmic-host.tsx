// pages/plasmic-host.tsx
import * as React from 'react';
import { PlasmicCanvasHost, registerComponent } from '@plasmicapp/host';

export default function PlasmicHost() {
  React.useEffect(() => {
    registerComponent(
      React.lazy(() => import('@/components/TreeBrowser')),
      {
      name: 'TreeBrowser',
      importPath: '@/components/TreeBrowser',
      props: {
        className: { type: 'class' },     // <-- lets Plasmic style it
        baseUrl: { type: 'string', defaultValue: 'http://localhost:8000/api/v1/tree' },
        parentParam: { type: 'string', defaultValue: 'parent' },
        rootParentValue: { type: 'string', defaultValue: 'root' },
        authToken: { type: 'string', advanced: true },
        extraHeadersJson: { type: 'string', advanced: true },
        fieldNamesJson: { type: 'string', advanced: true },
        height: { type: 'string', defaultValue: '60vh' },
        designTimeSample: { type: 'boolean', defaultValue: true },
        onSelect: {
          type: 'eventHandler',
          description: 'Called when a node is selected',
          argTypes: [{ name: 'node', type: 'object' }],
        },
      },
    });
  }, []);

  return <PlasmicCanvasHost />;
}

