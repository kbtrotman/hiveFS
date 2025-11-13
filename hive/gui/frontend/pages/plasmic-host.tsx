import * as React from 'react';
import { PlasmicCanvasHost, registerComponent } from '@plasmicapp/host';

export default function PlasmicHost() {
  React.useEffect(() => {
    // Existing TreeBrowser registration (keep as you have it)
    registerComponent(
      React.lazy(() => import('@/components/TreeBrowser')),
      {
        name: 'TreeBrowser',
        importPath: '@/components/TreeBrowser',
        importName: 'default',
        props: {
          className: { type: 'class' },
          baseUrl: { type: 'string', defaultValue: '/api/v1/tree' },
          parentParam: { type: 'string', defaultValue: 'parent' },
          rootParentValue: { type: 'string', defaultValue: 'root' },
          height: { type: 'string', defaultValue: '60vh' },
          designTimeSample: { type: 'boolean', defaultValue: true },
          onSelect: { type: 'eventHandler', argTypes: [{ name: 'node', type: 'object' }] },
        },
      }
    );

    // New ProTableBrowser registration
    registerComponent(
      React.lazy(() => import('@/components/TableBrowser')),
      {
        name: 'TableBrowser',
        importPath: '@/components/TableBrowser',
        importName: 'default',
        props: {
          className: { type: 'class' },
          baseUrl: { type: 'string', defaultValue: 'http://localhost:8000/api/v1/filelist' },
          authToken: { type: 'string', advanced: true },
          extraHeadersJson: { type: 'string', advanced: true },
          columnsJson: { type: 'string', advanced: true, description: 'JSON array of ProColumns' },
          rowKey: { type: 'string', defaultValue: 'id' },
          height: { type: 'string', defaultValue: '60vh' },
          enableSearch: { type: 'boolean', defaultValue: false },
          designTimeSample: { type: 'boolean', defaultValue: true },
          onRowClick: {
            type: 'eventHandler',
            argTypes: [{ name: 'payload', type: 'object' }], // { record }
          },
        },
      }
    );
  }, []);

  return <PlasmicCanvasHost />;
}
