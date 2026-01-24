import { Card, CardContent, CardDescription, CardHeader, CardTitle } from '../ui/card';
import { Button } from '../ui/button';
import { Badge } from '../ui/badge';
import { FileText, Download, Calendar, TrendingUp } from 'lucide-react';

const reports = [
  { name: 'System Performance Report', type: 'Performance', date: '2024-12-02', size: '2.3 MB', status: 'ready' },
  { name: 'Storage Utilization Report', type: 'Storage', date: '2024-12-01', size: '1.8 MB', status: 'ready' },
  { name: 'Security Audit Report', type: 'Security', date: '2024-12-01', size: '945 KB', status: 'ready' },
  { name: 'Monthly Summary - November', type: 'Summary', date: '2024-11-30', size: '3.1 MB', status: 'ready' },
  { name: 'User Activity Report', type: 'Activity', date: '2024-11-29', size: '1.2 MB', status: 'ready' },
  { name: 'Weekly Backup Report', type: 'Backup', date: '2024-11-28', size: '856 KB', status: 'generating' },
];

const scheduledReports = [
  { name: 'Daily Performance Summary', frequency: 'Daily', time: '00:00 UTC', format: 'PDF' },
  { name: 'Weekly Storage Analysis', frequency: 'Weekly', time: 'Monday 08:00 UTC', format: 'Excel' },
  { name: 'Monthly Security Audit', frequency: 'Monthly', time: '1st at 00:00 UTC', format: 'PDF' },
];

export function ReportsPage() {
  return (
    <div className="space-y-6">
      <div className="flex justify-between items-center">
        <div>
          <h2>Reports</h2>
          <p className="text-muted-foreground">Generate and download system reports</p>
        </div>
        <Button>Generate New Report</Button>
      </div>

      <div className="grid grid-cols-1 md:grid-cols-4 gap-4">
        <Card>
          <CardContent className="pt-6">
            <div className="flex items-center justify-between">
              <div>
                <p className="text-sm text-muted-foreground">Total Reports</p>
                <p className="mt-2">245</p>
              </div>
              <FileText className="w-5 h-5 text-blue-500" />
            </div>
          </CardContent>
        </Card>
        <Card>
          <CardContent className="pt-6">
            <div className="flex items-center justify-between">
              <div>
                <p className="text-sm text-muted-foreground">This Month</p>
                <p className="mt-2">28</p>
              </div>
              <Calendar className="w-5 h-5 text-green-500" />
            </div>
          </CardContent>
        </Card>
        <Card>
          <CardContent className="pt-6">
            <div className="flex items-center justify-between">
              <div>
                <p className="text-sm text-muted-foreground">Scheduled</p>
                <p className="mt-2">3</p>
              </div>
              <TrendingUp className="w-5 h-5 text-purple-500" />
            </div>
          </CardContent>
        </Card>
        <Card>
          <CardContent className="pt-6">
            <p className="text-sm text-muted-foreground">Storage Used</p>
            <p className="mt-2">458 MB</p>
          </CardContent>
        </Card>
      </div>

      <Card>
        <CardHeader>
          <CardTitle>Recent Reports</CardTitle>
          <CardDescription>Generated reports available for download</CardDescription>
        </CardHeader>
        <CardContent>
          <div className="space-y-3">
            {reports.map((report, index) => (
              <div key={index} className="flex items-center justify-between py-3 border-b border-border last:border-0">
                <div className="flex items-center gap-3">
                  <FileText className="w-5 h-5 text-muted-foreground" />
                  <div>
                    <p>{report.name}</p>
                    <p className="text-sm text-muted-foreground">
                      {report.date} • {report.size}
                    </p>
                  </div>
                </div>
                <div className="flex items-center gap-3">
                  <Badge variant="outline">{report.type}</Badge>
                  <Badge variant={report.status === 'ready' ? 'default' : 'secondary'}>
                    {report.status}
                  </Badge>
                  {report.status === 'ready' && (
                    <Button variant="ghost" size="sm">
                      <Download className="w-4 h-4 mr-2" />
                      Download
                    </Button>
                  )}
                </div>
              </div>
            ))}
          </div>
        </CardContent>
      </Card>

      <Card>
        <CardHeader>
          <CardTitle>Scheduled Reports</CardTitle>
          <CardDescription>Automatically generated reports</CardDescription>
        </CardHeader>
        <CardContent>
          <div className="space-y-4">
            {scheduledReports.map((report, index) => (
              <div key={index} className="flex items-center justify-between p-4 border border-border rounded-lg">
                <div>
                  <p>{report.name}</p>
                  <p className="text-sm text-muted-foreground mt-1">
                    {report.frequency} at {report.time}
                  </p>
                </div>
                <div className="flex items-center gap-2">
                  <Badge variant="outline">{report.format}</Badge>
                  <Button variant="ghost" size="sm">Edit</Button>
                </div>
              </div>
            ))}
          </div>
        </CardContent>
      </Card>

      <Card>
        <CardHeader>
          <CardTitle>Report Types</CardTitle>
          <CardDescription>Available report templates</CardDescription>
        </CardHeader>
        <CardContent>
          <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
            <div className="p-4 border border-border rounded-lg">
              <p>Performance Analysis</p>
              <p className="text-sm text-muted-foreground mt-1">
                Detailed metrics on IOPS, throughput, and latency
              </p>
              <Button variant="outline" size="sm" className="mt-3">Generate</Button>
            </div>
            <div className="p-4 border border-border rounded-lg">
              <p>Storage Capacity Planning</p>
              <p className="text-sm text-muted-foreground mt-1">
                Forecast future storage needs based on trends
              </p>
              <Button variant="outline" size="sm" className="mt-3">Generate</Button>
            </div>
            <div className="p-4 border border-border rounded-lg">
              <p>Security Compliance</p>
              <p className="text-sm text-muted-foreground mt-1">
                Security events, access logs, and compliance status
              </p>
              <Button variant="outline" size="sm" className="mt-3">Generate</Button>
            </div>
            <div className="p-4 border border-border rounded-lg">
              <p>User Activity Summary</p>
              <p className="text-sm text-muted-foreground mt-1">
                User access patterns and resource usage
              </p>
              <Button variant="outline" size="sm" className="mt-3">Generate</Button>
            </div>
          </div>
        </CardContent>
      </Card>
    </div>
  );
}
