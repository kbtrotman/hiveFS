import { useState } from 'react';
import { Save } from 'lucide-react';
import { Input } from '../../../ui/input';
import { Button } from '../../../ui/button';
import { ComponentLayoutBuilder, ReusableComponent } from '../../../ComponentLayoutBuilder';

export function ReportsCreatorTab() {
  const [reportName, setReportName] = useState('');

  // Saved components library (would be loaded from API/storage - shared with dashboard)
  const [savedComponents] = useState<ReusableComponent[]>([
    {
      id: '1',
      name: 'Storage Capacity Table',
      type: 'table',
      config: { columns: ['Node', 'Capacity', 'Used', 'Available'], dataSource: 'nodes' },
    },
    {
      id: '2',
      name: 'Performance Chart',
      type: 'chart',
      config: { chartType: 'line', dataSource: 'performance', metrics: ['throughput', 'latency'] },
    },
    {
      id: '3',
      name: 'Cluster Health Metrics',
      type: 'metric',
      config: { metrics: ['uptime', 'errorRate', 'activeNodes'] },
    },
  ]);

  const [workingComponents, setWorkingComponents] = useState<ReusableComponent[]>([]);

  const handleSaveReport = () => {
    // Would save to API/storage
    console.log('Saving report:', {
      name: reportName,
      components: workingComponents,
    });
  };

  return (
    <div className="flex h-full w-full flex-col gap-6 overflow-auto bg-gradient-to-br from-background via-primary/5 to-background p-8">
      {/* Page Header */}
      <div className="flex items-center justify-between">
        <div>
          <h1 className="text-foreground/80">Report Creator</h1>
          <p className="text-muted-foreground text-sm mt-1">
            Build custom reports using reusable components
          </p>
        </div>
        <div className="flex items-center gap-3">
          <Input
            placeholder="Report name"
            value={reportName}
            onChange={(e) => setReportName(e.target.value)}
            className="w-64 text-sm"
          />
          <Button disabled={!reportName.trim() || workingComponents.length === 0} onClick={handleSaveReport}>
            <Save className="w-4 h-4 mr-2" />
            Save Report
          </Button>
        </div>
      </div>

      {/* Component Layout Builder */}
      <ComponentLayoutBuilder
        savedComponents={savedComponents}
        onComponentsChange={setWorkingComponents}
      />
    </div>
  );
}
