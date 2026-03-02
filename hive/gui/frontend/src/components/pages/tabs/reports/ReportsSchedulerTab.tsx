import { useEffect, useMemo, useState } from 'react';
import { Download, Mail, Calendar, Plus, Trash2, Clock } from 'lucide-react';
import { Card, CardContent, CardHeader, CardTitle } from '../../../ui/card';
import { Input } from '../../../ui/input';
import { Label } from '../../../ui/label';
import { Button } from '../../../ui/button';
import { Separator } from '../../../ui/separator';
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '../../../ui/select';
import { Switch } from '../../../ui/switch';
import { Badge } from '../../../ui/badge';
import { Checkbox } from '../../../ui/checkbox';
import { useApiResource } from '../../../useApiResource';

interface SystemUser {
  id: number;
  email: string;
  username: string;
  first_name: string;
  last_name: string;
}

interface Schedule {
  schedule_id: number;
  schedule_name: string;
  frequency: string;
  start_at?: string | null;
  is_active?: boolean;
}

export function ReportsSchedulerTab() {
  const [outputType, setOutputType] = useState<'download' | 'email'>('download');
  const [downloadFormat, setDownloadFormat] = useState<string>('pdf');
  const [scheduleMode, setScheduleMode] = useState<'existing' | 'create'>('existing');
  const [selectedSchedule, setSelectedSchedule] = useState<string>('');
  const [selectedUsers, setSelectedUsers] = useState<string[]>([]);
  const [additionalEmails, setAdditionalEmails] = useState<string[]>([]);
  const [newEmail, setNewEmail] = useState('');

  const {
    data: systemUsers,
    isLoading: isLoadingUsers,
    error: usersError,
  } = useApiResource<SystemUser[]>('accounts', {
    initialData: [],
    transform: (payload) => (Array.isArray(payload) ? payload : []),
  });
  const {
    data: schedules,
    isLoading: isLoadingSchedules,
    error: schedulesError,
  } = useApiResource<Schedule[]>('notification-schedules', {
    initialData: [],
    transform: (payload) => (Array.isArray(payload) ? payload : []),
  });

  const scheduleOptions = useMemo(
    () =>
      schedules.map((schedule) => ({
        id: schedule.schedule_id.toString(),
        label: schedule.schedule_name,
        frequency: schedule.frequency,
        start_at: schedule.start_at,
      })),
    [schedules],
  );

  useEffect(() => {
    if (!selectedSchedule && scheduleOptions.length) {
      setSelectedSchedule(scheduleOptions[0].id);
    }
  }, [scheduleOptions, selectedSchedule]);

  const toggleUserSelection = (userId: string) => {
    setSelectedUsers((prev) =>
      prev.includes(userId)
        ? prev.filter((id) => id !== userId)
        : [...prev, userId]
    );
  };

  const addAdditionalEmail = () => {
    if (newEmail.trim() && !additionalEmails.includes(newEmail.trim())) {
      setAdditionalEmails([...additionalEmails, newEmail.trim()]);
      setNewEmail('');
    }
  };

  const removeAdditionalEmail = (email: string) => {
    setAdditionalEmails(additionalEmails.filter((e) => e !== email));
  };

  const combinedError = usersError ?? schedulesError;

  return (
    <div className="flex h-full w-full flex-col gap-6 overflow-auto bg-gradient-to-br from-background via-primary/5 to-background p-8">
      {/* Page Header */}
      <div>
        <h1 className="text-foreground/80">Report Scheduler</h1>
        <p className="text-muted-foreground text-sm mt-1">
          Configure report output format and delivery schedule
        </p>
      </div>

      {combinedError && (
        <div className="rounded-md border border-destructive/40 bg-destructive/10 px-4 py-2 text-sm text-destructive">
          {combinedError}
        </div>
      )}

      {/* Top Row - Output Configuration */}
      <div className="grid grid-cols-1 gap-6 lg:grid-cols-2">
        {/* Report Output Card */}
        <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
          <CardHeader>
            <CardTitle className="text-foreground/90 flex items-center gap-2">
              <Download className="w-5 h-5" />
              Report Output
            </CardTitle>
          </CardHeader>
          <CardContent className="space-y-4">
            <div>
              <Label className="text-sm mb-3 block">Output Type</Label>
              <div className="flex gap-3">
                <Button
                  variant={outputType === 'download' ? 'default' : 'outline'}
                  className="flex-1 text-sm"
                  onClick={() => setOutputType('download')}
                >
                  <Download className="w-4 h-4 mr-2" />
                  Download
                </Button>
                <Button
                  variant={outputType === 'email' ? 'default' : 'outline'}
                  className="flex-1 text-sm"
                  onClick={() => setOutputType('email')}
                >
                  <Mail className="w-4 h-4 mr-2" />
                  Email
                </Button>
              </div>
            </div>

            <Separator />

            <div>
              <Label htmlFor="format" className="text-sm">
                Report Format
              </Label>
              <Select value={downloadFormat} onValueChange={setDownloadFormat}>
                <SelectTrigger id="format" className="mt-1.5 text-sm">
                  <SelectValue />
                </SelectTrigger>
                <SelectContent>
                  <SelectItem value="pdf">PDF</SelectItem>
                  <SelectItem value="html">HTML</SelectItem>
                  <SelectItem value="csv">CSV</SelectItem>
                  <SelectItem value="excel">Excel</SelectItem>
                </SelectContent>
              </Select>
            </div>

            {outputType === 'download' && (
              <>
                <Separator />
                <div className="bg-muted/50 rounded-lg p-4 border border-border">
                  <p className="text-sm text-foreground/75">
                    Download reports are generated on-demand and do not require scheduling.
                  </p>
                </div>
              </>
            )}

            {outputType === 'email' && (
              <>
                <Separator />
                <Button className="w-full" variant="outline">
                  <Mail className="w-4 h-4 mr-2" />
                  Email Now
                </Button>
              </>
            )}

            <p
              className="text-muted-foreground opacity-60"
              style={{ fontSize: '0.7rem', lineHeight: '1.3' }}
            >
              {outputType === 'download'
                ? 'Click "Generate Report" to download immediately.'
                : 'Email reports are sent automatically based on the schedule below.'}
            </p>
          </CardContent>
        </Card>

        {/* Email Recipients Card - Only shown for email output */}
        {outputType === 'email' && (
          <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
            <CardHeader>
              <CardTitle className="text-foreground/90 flex items-center gap-2">
                <Mail className="w-5 h-5" />
                Email Recipients
              </CardTitle>
            </CardHeader>
            <CardContent className="space-y-4">
              <div>
                <Label className="text-sm mb-2 block">System Users</Label>
                <div className="border border-border rounded-lg max-h-40 overflow-y-auto">
                  {isLoadingUsers && (
                    <p className="p-3 text-sm text-muted-foreground">Loading accounts…</p>
                  )}
                  {!isLoadingUsers && systemUsers.length === 0 && (
                    <p className="p-3 text-sm text-muted-foreground">No users available.</p>
                  )}
                  {systemUsers.map((user) => {
                    const id = user.id.toString();
                    const displayName =
                      user.first_name || user.last_name
                        ? `${user.first_name} ${user.last_name}`.trim()
                        : user.username || user.email;
                    return (
                      <div
                        key={id}
                        className="flex items-center gap-3 p-2.5 hover:bg-muted/50 border-b border-border last:border-b-0"
                      >
                        <Checkbox
                          id={`user-${id}`}
                          checked={selectedUsers.includes(id)}
                          onCheckedChange={() => toggleUserSelection(id)}
                        />
                        <label
                          htmlFor={`user-${id}`}
                          className="flex-1 cursor-pointer text-sm"
                        >
                          <p className="text-foreground/90">{displayName}</p>
                          <p className="text-xs text-muted-foreground">{user.email}</p>
                        </label>
                      </div>
                    );
                  })}
                </div>
              </div>

              <Separator />

              <div>
                <Label htmlFor="additional-email" className="text-sm">
                  Additional Email Addresses
                </Label>
                <div className="flex gap-2 mt-1.5">
                  <Input
                    id="additional-email"
                    type="email"
                    placeholder="Enter email address"
                    value={newEmail}
                    onChange={(e) => setNewEmail(e.target.value)}
                    onKeyPress={(e) => e.key === 'Enter' && addAdditionalEmail()}
                    className="text-sm flex-1"
                  />
                  <Button
                    size="sm"
                    onClick={addAdditionalEmail}
                    disabled={!newEmail.trim()}
                  >
                    <Plus className="w-4 h-4" />
                  </Button>
                </div>
                {additionalEmails.length > 0 && (
                  <div className="flex flex-wrap gap-2 mt-2">
                    {additionalEmails.map((email) => (
                      <Badge
                        key={email}
                        variant="secondary"
                        className="text-xs flex items-center gap-1"
                      >
                        {email}
                        <button
                          onClick={() => removeAdditionalEmail(email)}
                          className="ml-1 hover:text-destructive"
                        >
                          <Trash2 className="w-3 h-3" />
                        </button>
                      </Badge>
                    ))}
                  </div>
                )}
              </div>

              <p
                className="text-muted-foreground opacity-60"
                style={{ fontSize: '0.7rem', lineHeight: '1.3' }}
              >
                Reports will be sent to all selected recipients at the scheduled time.
              </p>
            </CardContent>
          </Card>
        )}
      </div>

      {/* Schedule Configuration - Only shown for email output */}
      {outputType === 'email' && (
        <>
          <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
            <CardHeader>
              <div className="flex items-center justify-between">
                <CardTitle className="text-foreground/90 flex items-center gap-2">
                  <Calendar className="w-5 h-5" />
                  Schedule Configuration
                </CardTitle>
                <div className="flex gap-2">
                  <Button
                    size="sm"
                    variant={scheduleMode === 'existing' ? 'default' : 'outline'}
                    className="text-xs h-8"
                    onClick={() => setScheduleMode('existing')}
                  >
                    Use Existing
                  </Button>
                  <Button
                    size="sm"
                    variant={scheduleMode === 'create' ? 'default' : 'outline'}
                    className="text-xs h-8"
                    onClick={() => setScheduleMode('create')}
                  >
                    <Plus className="w-3 h-3 mr-1" />
                    Create New
                  </Button>
                </div>
              </div>
            </CardHeader>
            <CardContent>
              {scheduleMode === 'existing' ? (
                <div className="space-y-3">
                  <Label htmlFor="existing-schedule" className="text-sm">
                    Select Schedule
                  </Label>
                  <Select value={selectedSchedule} onValueChange={setSelectedSchedule}>
                    <SelectTrigger id="existing-schedule" className="text-sm">
                      <SelectValue placeholder="Choose a schedule..." />
                    </SelectTrigger>
                    <SelectContent>
                      {scheduleOptions.map((schedule) => (
                        <SelectItem key={schedule.id} value={schedule.id}>
                          {schedule.label} • {schedule.frequency}
                          {schedule.start_at ? ` @ ${new Date(schedule.start_at).toLocaleTimeString()}` : ''}
                        </SelectItem>
                      ))}
                    </SelectContent>
                  </Select>
                  <p
                    className="text-muted-foreground opacity-60"
                    style={{ fontSize: '0.7rem', lineHeight: '1.3' }}
                  >
                    Choose from pre-configured schedules or create a new one.
                  </p>
                </div>
              ) : (
                <div className="grid grid-cols-2 gap-6">
                  <div className="space-y-4">
                    <div>
                      <Label htmlFor="schedule-name" className="text-sm">
                        Schedule Name
                      </Label>
                      <Input
                        id="schedule-name"
                        placeholder="Enter schedule name"
                        className="mt-1.5 text-sm"
                      />
                    </div>

                    <div>
                      <Label htmlFor="frequency" className="text-sm">
                        Frequency
                      </Label>
                      <Select defaultValue="daily">
                        <SelectTrigger id="frequency" className="mt-1.5 text-sm">
                          <SelectValue />
                        </SelectTrigger>
                        <SelectContent>
                          <SelectItem value="daily">Daily</SelectItem>
                          <SelectItem value="weekly">Weekly</SelectItem>
                          <SelectItem value="monthly">Monthly</SelectItem>
                          <SelectItem value="quarterly">Quarterly</SelectItem>
                          <SelectItem value="yearly">Yearly</SelectItem>
                        </SelectContent>
                      </Select>
                    </div>

                    <div>
                      <Label htmlFor="time" className="text-sm">
                        Time of Day
                      </Label>
                      <Input
                        id="time"
                        type="time"
                        defaultValue="08:00"
                        className="mt-1.5 text-sm"
                      />
                    </div>

                    <div>
                      <Label htmlFor="timezone" className="text-sm">
                        Time Zone
                      </Label>
                      <Select defaultValue="utc">
                        <SelectTrigger id="timezone" className="mt-1.5 text-sm">
                          <SelectValue />
                        </SelectTrigger>
                        <SelectContent>
                          <SelectItem value="utc">UTC</SelectItem>
                          <SelectItem value="est">Eastern (EST)</SelectItem>
                          <SelectItem value="cst">Central (CST)</SelectItem>
                          <SelectItem value="mst">Mountain (MST)</SelectItem>
                          <SelectItem value="pst">Pacific (PST)</SelectItem>
                        </SelectContent>
                      </Select>
                    </div>
                  </div>

                  <div className="space-y-4">
                    <div>
                      <Label className="text-sm mb-2 block">Days of Week</Label>
                      <div className="grid grid-cols-2 gap-2">
                        {['Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday', 'Sunday'].map(
                          (day) => (
                            <div key={day} className="flex items-center gap-2">
                              <Checkbox id={day.toLowerCase()} />
                              <label
                                htmlFor={day.toLowerCase()}
                                className="text-sm cursor-pointer"
                              >
                                {day}
                              </label>
                            </div>
                          )
                        )}
                      </div>
                      <p className="text-xs text-muted-foreground mt-2">
                        For weekly schedules only
                      </p>
                    </div>

                    <Separator />

                    <div className="grid grid-cols-2 gap-3">
                      <div>
                        <Label htmlFor="start-date" className="text-xs text-muted-foreground">
                          Start Date
                        </Label>
                        <Input
                          id="start-date"
                          type="date"
                          className="mt-1 text-sm h-8"
                        />
                      </div>
                      <div>
                        <Label htmlFor="end-date" className="text-xs text-muted-foreground">
                          End Date (Optional)
                        </Label>
                        <Input
                          id="end-date"
                          type="date"
                          className="mt-1 text-sm h-8"
                        />
                      </div>
                    </div>
                  </div>
                </div>
              )}
            </CardContent>
          </Card>
        </>
      )}

      {/* Action Buttons */}
      <div className="flex justify-end gap-3">
        <Button variant="outline">Cancel</Button>
        <Button className="px-8">
          {outputType === 'download' ? 'Generate Report' : 'Save Schedule'}
        </Button>
      </div>
    </div>
  );
}
