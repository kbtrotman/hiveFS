import { useState } from 'react';
import { Upload, Trash2, FileText, Image, Palette } from 'lucide-react';
import { Card, CardContent, CardHeader, CardTitle } from '../../../ui/card';
import { Input } from '../../../ui/input';
import { Label } from '../../../ui/label';
import { Textarea } from '../../../ui/textarea';
import { Button } from '../../../ui/button';
import { Separator } from '../../../ui/separator';
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '../../../ui/select';
import { Switch } from '../../../ui/switch';

interface Theme {
  id: string;
  name: string;
  uploadedDate: Date;
}

export function ReportsSettingsTab() {
  const [themes, setThemes] = useState<Theme[]>([
    { id: '1', name: 'Default Theme', uploadedDate: new Date('2024-01-15') },
    { id: '2', name: 'Corporate Blue', uploadedDate: new Date('2024-02-20') },
  ]);

  const [useBoldFont, setUseBoldFont] = useState(false);
  const [useCommasInNumbers, setUseCommasInNumbers] = useState(true);

  const deleteTheme = (id: string) => {
    setThemes(themes.filter((theme) => theme.id !== id));
  };

  return (
    <div className="flex h-full w-full flex-col gap-6 overflow-auto bg-gradient-to-br from-background via-primary/5 to-background p-8">
      {/* Page Header */}
      <div>
        <h1 className="text-2xl font-semibold text-foreground">Report Settings</h1>
        <p className="mt-1 text-sm text-muted-foreground">
          Configure global settings applied to all generated reports
        </p>
      </div>

      {/* Top Row - Data Formats, Visual Settings, and Themes */}
      <div className="grid grid-cols-1 gap-6 lg:grid-cols-3">
        {/* Data Format Settings Card */}
        <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
          <CardHeader className="pb-3">
            <CardTitle className="text-foreground/90 flex items-center gap-2 text-base">
              <FileText className="w-4 h-4" />
              Data Formats
            </CardTitle>
          </CardHeader>
          <CardContent className="space-y-3">
            <div>
              <Label htmlFor="storage-unit" className="text-xs text-muted-foreground">
                Storage Unit
              </Label>
              <Select defaultValue="auto">
                <SelectTrigger id="storage-unit" className="mt-1 text-sm h-8">
                  <SelectValue />
                </SelectTrigger>
                <SelectContent>
                  <SelectItem value="auto">Auto</SelectItem>
                  <SelectItem value="bytes">Bytes</SelectItem>
                  <SelectItem value="kb">KB</SelectItem>
                  <SelectItem value="mb">MB</SelectItem>
                  <SelectItem value="gb">GB</SelectItem>
                  <SelectItem value="tb">TB</SelectItem>
                </SelectContent>
              </Select>
            </div>

            <div>
              <Label htmlFor="date-format" className="text-xs text-muted-foreground">
                Date Format
              </Label>
              <Select defaultValue="mm-dd-yyyy">
                <SelectTrigger id="date-format" className="mt-1 text-sm h-8">
                  <SelectValue />
                </SelectTrigger>
                <SelectContent>
                  <SelectItem value="mm-dd-yyyy">MM/DD/YYYY</SelectItem>
                  <SelectItem value="dd-mm-yyyy">DD/MM/YYYY</SelectItem>
                  <SelectItem value="yyyy-mm-dd">YYYY-MM-DD</SelectItem>
                  <SelectItem value="mon-dd-yyyy">Mon DD, YYYY</SelectItem>
                </SelectContent>
              </Select>
            </div>

            <div>
              <Label htmlFor="currency-format" className="text-xs text-muted-foreground">
                Currency
              </Label>
              <Select defaultValue="usd">
                <SelectTrigger id="currency-format" className="mt-1 text-sm h-8">
                  <SelectValue />
                </SelectTrigger>
                <SelectContent>
                  <SelectItem value="usd">USD ($)</SelectItem>
                  <SelectItem value="eur">EUR (€)</SelectItem>
                  <SelectItem value="gbp">GBP (£)</SelectItem>
                  <SelectItem value="jpy">JPY (¥)</SelectItem>
                </SelectContent>
              </Select>
            </div>

            <div className="flex items-center justify-between pt-1">
              <Label htmlFor="number-commas" className="text-xs">
                Use Commas
              </Label>
              <Switch
                id="number-commas"
                checked={useCommasInNumbers}
                onCheckedChange={setUseCommasInNumbers}
              />
            </div>
          </CardContent>
        </Card>

        {/* Visual Settings Card */}
        <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
          <CardHeader className="pb-3">
            <CardTitle className="text-foreground/90 flex items-center gap-2 text-base">
              <Palette className="w-4 h-4" />
              Visual Settings
            </CardTitle>
          </CardHeader>
          <CardContent className="space-y-3">
            <div>
              <Label htmlFor="color-banding" className="text-xs text-muted-foreground">
                Color Banding
              </Label>
              <Select defaultValue="subtle">
                <SelectTrigger id="color-banding" className="mt-1 text-sm h-8">
                  <SelectValue />
                </SelectTrigger>
                <SelectContent>
                  <SelectItem value="none">None</SelectItem>
                  <SelectItem value="subtle">Subtle</SelectItem>
                  <SelectItem value="moderate">Moderate</SelectItem>
                  <SelectItem value="bold">Bold</SelectItem>
                </SelectContent>
              </Select>
            </div>

            <div>
              <Label htmlFor="font-family" className="text-xs text-muted-foreground">
                Font Family
              </Label>
              <Select defaultValue="arial">
                <SelectTrigger id="font-family" className="mt-1 text-sm h-8">
                  <SelectValue />
                </SelectTrigger>
                <SelectContent>
                  <SelectItem value="arial">Arial</SelectItem>
                  <SelectItem value="helvetica">Helvetica</SelectItem>
                  <SelectItem value="times">Times New Roman</SelectItem>
                  <SelectItem value="calibri">Calibri</SelectItem>
                </SelectContent>
              </Select>
            </div>

            <div>
              <Label htmlFor="font-size" className="text-xs text-muted-foreground">
                Font Size
              </Label>
              <Select defaultValue="10">
                <SelectTrigger id="font-size" className="mt-1 text-sm h-8">
                  <SelectValue />
                </SelectTrigger>
                <SelectContent>
                  <SelectItem value="8">8pt</SelectItem>
                  <SelectItem value="9">9pt</SelectItem>
                  <SelectItem value="10">10pt</SelectItem>
                  <SelectItem value="11">11pt</SelectItem>
                  <SelectItem value="12">12pt</SelectItem>
                </SelectContent>
              </Select>
            </div>

            <div className="flex items-center justify-between pt-1">
              <Label htmlFor="bold-font" className="text-xs">
                Use Bold
              </Label>
              <Switch
                id="bold-font"
                checked={useBoldFont}
                onCheckedChange={setUseBoldFont}
              />
            </div>
          </CardContent>
        </Card>

        {/* Theme Management Card */}
        <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
          <CardHeader className="pb-3">
            <div className="flex items-center justify-between">
              <CardTitle className="text-foreground/90 text-base">Report Themes</CardTitle>
              <Button size="sm" className="text-xs h-7 px-2">
                <Upload className="w-3 h-3 mr-1" />
                Upload
              </Button>
            </div>
          </CardHeader>
          <CardContent className="space-y-2">
            {themes.map((theme) => (
              <div
                key={theme.id}
                className="flex items-center justify-between p-2 rounded border border-border bg-muted/30"
              >
                <div className="flex items-center gap-2 flex-1 min-w-0">
                  <Palette className="w-3 h-3 text-muted-foreground flex-shrink-0" />
                  <p className="text-xs text-foreground/90 truncate">{theme.name}</p>
                </div>
                <Button
                  size="sm"
                  variant="ghost"
                  className="text-xs h-6 px-2 flex-shrink-0"
                  onClick={() => deleteTheme(theme.id)}
                >
                  <Trash2 className="w-3 h-3 text-destructive" />
                </Button>
              </div>
            ))}
            <p
              className="text-muted-foreground opacity-60 pt-1"
              style={{ fontSize: '0.7rem', lineHeight: '1.3' }}
            >
              Upload JSON or CSS theme files
            </p>
          </CardContent>
        </Card>
      </div>

      {/* Bottom Row - Layout Settings */}
      <div className="grid grid-cols-1 gap-6 lg:grid-cols-2">
        {/* Document Layout Card */}
        <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
          <CardHeader>
            <CardTitle className="text-foreground/90 flex items-center gap-2">
              <FileText className="w-5 h-5" />
              Document Layout
            </CardTitle>
          </CardHeader>
          <CardContent className="space-y-4">
            <div className="grid grid-cols-2 gap-3">
              <div>
                <Label htmlFor="margin-top" className="text-xs text-muted-foreground">
                  Top Margin (in)
                </Label>
                <Input
                  id="margin-top"
                  type="number"
                  defaultValue="0.75"
                  step="0.25"
                  className="mt-1 text-sm h-8"
                />
              </div>
              <div>
                <Label htmlFor="margin-bottom" className="text-xs text-muted-foreground">
                  Bottom Margin (in)
                </Label>
                <Input
                  id="margin-bottom"
                  type="number"
                  defaultValue="0.75"
                  step="0.25"
                  className="mt-1 text-sm h-8"
                />
              </div>
            </div>

            <div className="grid grid-cols-2 gap-3">
              <div>
                <Label htmlFor="margin-left" className="text-xs text-muted-foreground">
                  Left Margin (in)
                </Label>
                <Input
                  id="margin-left"
                  type="number"
                  defaultValue="0.5"
                  step="0.25"
                  className="mt-1 text-sm h-8"
                />
              </div>
              <div>
                <Label htmlFor="margin-right" className="text-xs text-muted-foreground">
                  Right Margin (in)
                </Label>
                <Input
                  id="margin-right"
                  type="number"
                  defaultValue="0.5"
                  step="0.25"
                  className="mt-1 text-sm h-8"
                />
              </div>
            </div>

            <Separator />

            <div>
              <Label htmlFor="page-orientation" className="text-sm">
                Default Page Orientation
              </Label>
              <Select defaultValue="portrait">
                <SelectTrigger id="page-orientation" className="mt-1.5 text-sm">
                  <SelectValue />
                </SelectTrigger>
                <SelectContent>
                  <SelectItem value="portrait">Portrait</SelectItem>
                  <SelectItem value="landscape">Landscape</SelectItem>
                </SelectContent>
              </Select>
            </div>

            <p
              className="text-muted-foreground opacity-60"
              style={{ fontSize: '0.7rem', lineHeight: '1.3' }}
            >
              Margins affect all pages. Individual reports may override these defaults.
            </p>
          </CardContent>
        </Card>

        {/* Header & Footer Card */}
        <Card className="shadow-lg">
          <CardHeader>
            <CardTitle className="text-foreground/90 flex items-center gap-2">
              <Image className="w-5 h-5" />
              Header & Footer
            </CardTitle>
          </CardHeader>
          <CardContent className="space-y-4">
            <div>
              <Label htmlFor="company-name" className="text-sm">
                Company Name
              </Label>
              <Input
                id="company-name"
                placeholder="Enter company name"
                defaultValue="HiveFS Corporation"
                className="mt-1.5 text-sm"
              />
            </div>

            <div>
              <Label className="text-sm">Company Logo</Label>
              <div className="mt-1.5 flex gap-2">
                <Button variant="outline" size="sm" className="text-xs flex-1">
                  <Upload className="w-3 h-3 mr-2" />
                  Upload Logo
                </Button>
                <Button variant="outline" size="sm" className="text-xs">
                  <Trash2 className="w-3 h-3" />
                </Button>
              </div>
              <p className="text-xs text-muted-foreground mt-1">
                Recommended: PNG or SVG, max 200px height
              </p>
            </div>

            <Separator />

            <div>
              <Label htmlFor="footer-banner" className="text-sm">
                Footer Banner Text
              </Label>
              <Textarea
                id="footer-banner"
                placeholder="Enter footer text (e.g., confidentiality notice)"
                defaultValue="Company Internal - Private Correspondence - Do Not Distribute to Unauthorized Person(s)"
                className="mt-1.5 text-sm resize-none"
                rows={3}
              />
            </div>

            <p
              className="text-muted-foreground opacity-60"
              style={{ fontSize: '0.7rem', lineHeight: '1.3' }}
            >
              Header displays company name/logo. Footer appears on every page bottom.
            </p>
          </CardContent>
        </Card>
      </div>

      {/* Save Button */}
      <div className="flex justify-end">
        <Button className="px-8">Save Settings</Button>
      </div>
    </div>
  );
}
