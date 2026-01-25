import { Card, CardContent, CardHeader, CardTitle } from './ui/card';
import { Input } from './ui/input';
import { Label } from './ui/label';
import { Button } from './ui/button';
import { Separator } from './ui/separator';
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from './ui/select';
import { Badge } from './ui/badge';
import { Textarea } from './ui/textarea';
import {
  FileText,
  BarChart3,
  Table2,
  Type,
  AlignLeft,
  Plus,
  Trash2,
  Grid3x3,
  Copy,
} from 'lucide-react';
import { useState } from 'react';

export interface ReusableComponent {
  id: string;
  name: string;
  type: 'table' | 'chart' | 'title' | 'description' | 'metric';
  config: any; // JSON configuration
}

interface LayoutSlot {
  id: string;
  componentId: string | null;
  size: 'full' | 'half' | 'third';
}

interface ComponentLayoutBuilderProps {
  savedComponents: ReusableComponent[];
  onComponentsChange?: (components: ReusableComponent[]) => void;
  onLayoutChange?: (layout: LayoutSlot[]) => void;
}

export function ComponentLayoutBuilder({
  savedComponents,
  onComponentsChange,
  onLayoutChange,
}: ComponentLayoutBuilderProps) {
  const [componentMode, setComponentMode] = useState<'new' | 'existing'>('new');
  const [selectedComponentType, setSelectedComponentType] = useState<string>('table');
  const [componentName, setComponentName] = useState('');

  // Components in the current working set
  const [workingComponents, setWorkingComponents] = useState<ReusableComponent[]>([]);

  // Layout slots
  const [layoutSlots, setLayoutSlots] = useState<LayoutSlot[]>([
    { id: '1', componentId: null, size: 'full' },
    { id: '2', componentId: null, size: 'half' },
    { id: '3', componentId: null, size: 'half' },
    { id: '4', componentId: null, size: 'full' },
  ]);

  const addComponentToWorking = (component: ReusableComponent) => {
    const updated = [...workingComponents, component];
    setWorkingComponents(updated);
    onComponentsChange?.(updated);
  };

  const createNewComponent = () => {
    if (!componentName.trim()) return;

    const newComponent: ReusableComponent = {
      id: Date.now().toString(),
      name: componentName,
      type: selectedComponentType as any,
      config: {}, // Would be filled from the configuration form
    };

    addComponentToWorking(newComponent);
    setComponentName('');
  };

  const removeComponentFromWorking = (id: string) => {
    const updated = workingComponents.filter((c) => c.id !== id);
    setWorkingComponents(updated);
    onComponentsChange?.(updated);

    // Also remove from layout slots
    const updatedLayout = layoutSlots.map((slot) =>
      slot.componentId === id ? { ...slot, componentId: null } : slot
    );
    setLayoutSlots(updatedLayout);
    onLayoutChange?.(updatedLayout);
  };

  const assignComponentToSlot = (slotId: string, componentId: string) => {
    const updated = layoutSlots.map((slot) =>
      slot.id === slotId ? { ...slot, componentId } : slot
    );
    setLayoutSlots(updated);
    onLayoutChange?.(updated);
  };

  const clearSlot = (slotId: string) => {
    const updated = layoutSlots.map((slot) =>
      slot.id === slotId ? { ...slot, componentId: null } : slot
    );
    setLayoutSlots(updated);
    onLayoutChange?.(updated);
  };

  const addNewSlot = (size: 'full' | 'half' | 'third') => {
    const newSlot: LayoutSlot = {
      id: Date.now().toString(),
      componentId: null,
      size,
    };
    const updated = [...layoutSlots, newSlot];
    setLayoutSlots(updated);
    onLayoutChange?.(updated);
  };

  const getComponentById = (id: string | null) => {
    if (!id) return null;
    return workingComponents.find((c) => c.id === id);
  };

  const getComponentIcon = (type: string) => {
    switch (type) {
      case 'table':
        return <Table2 className="w-4 h-4" />;
      case 'chart':
        return <BarChart3 className="w-4 h-4" />;
      case 'title':
        return <Type className="w-4 h-4" />;
      case 'description':
        return <AlignLeft className="w-4 h-4" />;
      case 'metric':
        return <FileText className="w-4 h-4" />;
      default:
        return <FileText className="w-4 h-4" />;
    }
  };

  return (
    <>
      {/* Component Builder Section */}
      <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
        <CardHeader>
          <div className="flex items-center justify-between">
            <CardTitle className="text-foreground/90 flex items-center gap-2">
              <Grid3x3 className="w-5 h-5" />
              Component Builder
            </CardTitle>
            <div className="flex gap-2">
              <Button
                size="sm"
                variant={componentMode === 'new' ? 'default' : 'outline'}
                className="text-xs h-8"
                onClick={() => setComponentMode('new')}
              >
                <Plus className="w-3 h-3 mr-1" />
                Create New
              </Button>
              <Button
                size="sm"
                variant={componentMode === 'existing' ? 'default' : 'outline'}
                className="text-xs h-8"
                onClick={() => setComponentMode('existing')}
              >
                <Copy className="w-3 h-3 mr-1" />
                Use Saved
              </Button>
            </div>
          </div>
        </CardHeader>
        <CardContent>
          {componentMode === 'new' ? (
            <div className="space-y-4">
              <div className="grid grid-cols-3 gap-4">
                <div>
                  <Label htmlFor="component-name" className="text-sm">
                    Component Name
                  </Label>
                  <Input
                    id="component-name"
                    placeholder="e.g., Storage Usage Table"
                    value={componentName}
                    onChange={(e) => setComponentName(e.target.value)}
                    className="mt-1.5 text-sm"
                  />
                </div>
                <div>
                  <Label htmlFor="component-type" className="text-sm">
                    Component Type
                  </Label>
                  <Select
                    value={selectedComponentType}
                    onValueChange={setSelectedComponentType}
                  >
                    <SelectTrigger id="component-type" className="mt-1.5 text-sm">
                      <SelectValue />
                    </SelectTrigger>
                    <SelectContent>
                      <SelectItem value="table">
                        <div className="flex items-center gap-2">
                          <Table2 className="w-4 h-4" />
                          Table
                        </div>
                      </SelectItem>
                      <SelectItem value="chart">
                        <div className="flex items-center gap-2">
                          <BarChart3 className="w-4 h-4" />
                          Chart
                        </div>
                      </SelectItem>
                      <SelectItem value="title">
                        <div className="flex items-center gap-2">
                          <Type className="w-4 h-4" />
                          Title
                        </div>
                      </SelectItem>
                      <SelectItem value="description">
                        <div className="flex items-center gap-2">
                          <AlignLeft className="w-4 h-4" />
                          Description
                        </div>
                      </SelectItem>
                      <SelectItem value="metric">
                        <div className="flex items-center gap-2">
                          <FileText className="w-4 h-4" />
                          Metric Card
                        </div>
                      </SelectItem>
                    </SelectContent>
                  </Select>
                </div>
                <div className="flex items-end">
                  <Button
                    className="w-full"
                    onClick={createNewComponent}
                    disabled={!componentName.trim()}
                  >
                    <Plus className="w-4 h-4 mr-2" />
                    Add Component
                  </Button>
                </div>
              </div>

              <Separator />

              {/* Component Configuration (varies by type) */}
              <div className="bg-muted/30 rounded-lg p-4 border border-border">
                <p className="text-sm font-medium mb-3">
                  {selectedComponentType.charAt(0).toUpperCase() +
                    selectedComponentType.slice(1)}{' '}
                  Configuration
                </p>

                {selectedComponentType === 'table' && (
                  <div className="grid grid-cols-2 gap-4">
                    <div>
                      <Label htmlFor="data-source" className="text-xs text-muted-foreground">
                        Data Source
                      </Label>
                      <Select defaultValue="nodes">
                        <SelectTrigger id="data-source" className="mt-1 text-sm h-8">
                          <SelectValue />
                        </SelectTrigger>
                        <SelectContent>
                          <SelectItem value="nodes">Cluster Nodes</SelectItem>
                          <SelectItem value="volumes">Storage Volumes</SelectItem>
                          <SelectItem value="alerts">Active Alerts</SelectItem>
                          <SelectItem value="performance">Performance Metrics</SelectItem>
                        </SelectContent>
                      </Select>
                    </div>
                    <div>
                      <Label htmlFor="columns" className="text-xs text-muted-foreground">
                        Columns (comma-separated)
                      </Label>
                      <Input
                        id="columns"
                        placeholder="Name, Status, Capacity"
                        className="mt-1 text-sm h-8"
                      />
                    </div>
                  </div>
                )}

                {selectedComponentType === 'chart' && (
                  <div className="grid grid-cols-3 gap-4">
                    <div>
                      <Label htmlFor="chart-type" className="text-xs text-muted-foreground">
                        Chart Type
                      </Label>
                      <Select defaultValue="line">
                        <SelectTrigger id="chart-type" className="mt-1 text-sm h-8">
                          <SelectValue />
                        </SelectTrigger>
                        <SelectContent>
                          <SelectItem value="line">Line Chart</SelectItem>
                          <SelectItem value="bar">Bar Chart</SelectItem>
                          <SelectItem value="pie">Pie Chart</SelectItem>
                          <SelectItem value="area">Area Chart</SelectItem>
                        </SelectContent>
                      </Select>
                    </div>
                    <div>
                      <Label htmlFor="chart-data" className="text-xs text-muted-foreground">
                        Data Source
                      </Label>
                      <Select defaultValue="performance">
                        <SelectTrigger id="chart-data" className="mt-1 text-sm h-8">
                          <SelectValue />
                        </SelectTrigger>
                        <SelectContent>
                          <SelectItem value="performance">Performance</SelectItem>
                          <SelectItem value="capacity">Capacity Trends</SelectItem>
                          <SelectItem value="throughput">Throughput</SelectItem>
                        </SelectContent>
                      </Select>
                    </div>
                    <div>
                      <Label htmlFor="time-range" className="text-xs text-muted-foreground">
                        Time Range
                      </Label>
                      <Select defaultValue="24h">
                        <SelectTrigger id="time-range" className="mt-1 text-sm h-8">
                          <SelectValue />
                        </SelectTrigger>
                        <SelectContent>
                          <SelectItem value="1h">Last Hour</SelectItem>
                          <SelectItem value="24h">Last 24 Hours</SelectItem>
                          <SelectItem value="7d">Last 7 Days</SelectItem>
                          <SelectItem value="30d">Last 30 Days</SelectItem>
                        </SelectContent>
                      </Select>
                    </div>
                  </div>
                )}

                {selectedComponentType === 'title' && (
                  <div>
                    <Label htmlFor="title-text" className="text-xs text-muted-foreground">
                      Title Text
                    </Label>
                    <Input
                      id="title-text"
                      placeholder="Enter title text"
                      className="mt-1 text-sm h-8"
                    />
                  </div>
                )}

                {selectedComponentType === 'description' && (
                  <div>
                    <Label htmlFor="desc-text" className="text-xs text-muted-foreground">
                      Description Content
                    </Label>
                    <Textarea
                      id="desc-text"
                      placeholder="Enter description or summary text"
                      className="mt-1 text-sm min-h-20"
                    />
                  </div>
                )}

                {selectedComponentType === 'metric' && (
                  <div className="grid grid-cols-2 gap-4">
                    <div>
                      <Label htmlFor="metric-type" className="text-xs text-muted-foreground">
                        Metric Type
                      </Label>
                      <Select defaultValue="uptime">
                        <SelectTrigger id="metric-type" className="mt-1 text-sm h-8">
                          <SelectValue />
                        </SelectTrigger>
                        <SelectContent>
                          <SelectItem value="uptime">Cluster Uptime</SelectItem>
                          <SelectItem value="nodes">Active Nodes</SelectItem>
                          <SelectItem value="capacity">Total Capacity</SelectItem>
                          <SelectItem value="throughput">Throughput</SelectItem>
                        </SelectContent>
                      </Select>
                    </div>
                    <div>
                      <Label htmlFor="display-format" className="text-xs text-muted-foreground">
                        Display Format
                      </Label>
                      <Select defaultValue="number">
                        <SelectTrigger id="display-format" className="mt-1 text-sm h-8">
                          <SelectValue />
                        </SelectTrigger>
                        <SelectContent>
                          <SelectItem value="number">Number</SelectItem>
                          <SelectItem value="percentage">Percentage</SelectItem>
                          <SelectItem value="bytes">Bytes</SelectItem>
                          <SelectItem value="duration">Duration</SelectItem>
                        </SelectContent>
                      </Select>
                    </div>
                  </div>
                )}
              </div>
            </div>
          ) : (
            <div>
              <Label htmlFor="saved-component" className="text-sm">
                Select Saved Component
              </Label>
              <div className="grid grid-cols-2 gap-3 mt-2">
                {savedComponents.map((component) => (
                  <div
                    key={component.id}
                    className="flex items-center justify-between p-3 border border-border rounded-lg hover:bg-muted/50 cursor-pointer"
                    onClick={() =>
                      addComponentToWorking({ ...component, id: Date.now().toString() })
                    }
                  >
                    <div className="flex items-center gap-2">
                      {getComponentIcon(component.type)}
                      <div>
                        <p className="text-sm text-foreground/90">{component.name}</p>
                        <p className="text-xs text-muted-foreground capitalize">
                          {component.type}
                        </p>
                      </div>
                    </div>
                    <Plus className="w-4 h-4 text-muted-foreground" />
                  </div>
                ))}
              </div>
              <p
                className="text-muted-foreground opacity-60 mt-3"
                style={{ fontSize: '0.7rem', lineHeight: '1.3' }}
              >
                Click on a saved component to add it to your working set
              </p>
            </div>
          )}
        </CardContent>
      </Card>

      {/* Working Components Library */}
      {workingComponents.length > 0 && (
        <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
          <CardHeader>
            <CardTitle className="text-foreground/90 text-base flex items-center gap-2">
              <FileText className="w-4 h-4" />
              Working Components ({workingComponents.length})
            </CardTitle>
          </CardHeader>
          <CardContent>
            <div className="flex flex-wrap gap-2">
              {workingComponents.map((component) => (
                <Badge
                  key={component.id}
                  variant="secondary"
                  className="text-xs flex items-center gap-2 py-2 px-3"
                >
                  {getComponentIcon(component.type)}
                  {component.name}
                  <button
                    onClick={() => removeComponentFromWorking(component.id)}
                    className="ml-1 hover:text-destructive"
                  >
                    <Trash2 className="w-3 h-3" />
                  </button>
                </Badge>
              ))}
            </div>
          </CardContent>
        </Card>
      )}

      {/* Layout Builder */}
      <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
        <CardHeader>
          <div className="flex items-center justify-between">
            <CardTitle className="text-foreground/90 flex items-center gap-2">
              <Grid3x3 className="w-5 h-5" />
              Layout Builder
            </CardTitle>
            <div className="flex gap-2">
              <Button
                size="sm"
                variant="outline"
                className="text-xs h-8"
                onClick={() => addNewSlot('full')}
              >
                <Plus className="w-3 h-3 mr-1" />
                Full Width
              </Button>
              <Button
                size="sm"
                variant="outline"
                className="text-xs h-8"
                onClick={() => addNewSlot('half')}
              >
                <Plus className="w-3 h-3 mr-1" />
                Half Width
              </Button>
              <Button
                size="sm"
                variant="outline"
                className="text-xs h-8"
                onClick={() => addNewSlot('third')}
              >
                <Plus className="w-3 h-3 mr-1" />
                Third Width
              </Button>
            </div>
          </div>
        </CardHeader>
        <CardContent>
          <div className="space-y-3">
            {layoutSlots.map((slot) => {
              const component = getComponentById(slot.componentId);
              const widthClass =
                slot.size === 'full' ? 'w-full' : slot.size === 'half' ? 'w-1/2' : 'w-1/3';

              return (
                <div key={slot.id} className={`${widthClass}`}>
                  <div className="border-2 border-dashed border-border rounded-lg p-4 min-h-32 bg-muted/20">
                    {component ? (
                      <div className="flex items-start justify-between">
                        <div className="flex items-center gap-2">
                          {getComponentIcon(component.type)}
                          <div>
                            <p className="text-sm font-medium text-foreground/90">
                              {component.name}
                            </p>
                            <p className="text-xs text-muted-foreground capitalize">
                              {component.type}
                            </p>
                          </div>
                        </div>
                        <Button
                          size="sm"
                          variant="ghost"
                          className="h-6 w-6 p-0"
                          onClick={() => clearSlot(slot.id)}
                        >
                          <Trash2 className="w-3 h-3 text-destructive" />
                        </Button>
                      </div>
                    ) : (
                      <div className="text-center">
                        <p className="text-xs text-muted-foreground mb-2">
                          Select a component:
                        </p>
                        <Select
                          onValueChange={(value) => assignComponentToSlot(slot.id, value)}
                        >
                          <SelectTrigger className="text-sm h-8">
                            <SelectValue placeholder="Choose component..." />
                          </SelectTrigger>
                          <SelectContent>
                            {workingComponents.map((comp) => (
                              <SelectItem key={comp.id} value={comp.id}>
                                <div className="flex items-center gap-2">
                                  {getComponentIcon(comp.type)}
                                  {comp.name}
                                </div>
                              </SelectItem>
                            ))}
                          </SelectContent>
                        </Select>
                      </div>
                    )}
                  </div>
                  <p className="text-xs text-muted-foreground mt-1 capitalize">
                    {slot.size} width slot
                  </p>
                </div>
              );
            })}

            {layoutSlots.length === 0 && (
              <div className="text-center py-12 text-muted-foreground">
                <Grid3x3 className="w-12 h-12 mx-auto mb-3 opacity-50" />
                <p className="text-sm">No layout slots yet</p>
                <p className="text-xs mt-1">
                  Add slots using the buttons above to start building your layout
                </p>
              </div>
            )}
          </div>

          <p
            className="text-muted-foreground opacity-60 mt-4"
            style={{ fontSize: '0.7rem', lineHeight: '1.3' }}
          >
            Arrange components in slots to define your layout structure. Use different slot
            sizes to create flexible designs.
          </p>
        </CardContent>
      </Card>
    </>
  );
}
