import type { ComponentType } from 'react';
import { ProfilePage } from '../../ProfilePage';
import { PlaceholderTab } from '../PlaceholderTab';
import { HomeDashSetupTab } from '../home/HomeDashSetupTab';
import { HomeComponentCreatorTab } from '../home/HomeComponentCreatorTab';
import { ClusterManageTab } from '../cluster/ClusterManageTab';
import { ClusterMonitorTab } from '../cluster/ClusterMonitorTab';
import { ClusterPerformanceTab } from '../cluster/ClusterPerformanceTab';
import { ClusterClientsTab } from '../cluster/ClusterClientsTab';
import { ServersManageTab } from '../servers/ServersManageTab';
import { ServersMonitorTab } from '../servers/ServersMonitorTab';
import { ServersPerformanceTab } from '../servers/ServersPerformanceTab';
import { DiskManageTab } from '../disk/DiskManageTab';
import { DiskMonitorTab } from '../disk/DiskMonitorTab';
import { DiskPerformanceTab } from '../disk/DiskPerformanceTab';
import { SecurityAccessTab } from '../security/SecurityAccessTab';
import { SecurityAuditTab } from '../security/SecurityAuditTab';
import { SecurityApiAccessTab } from '../security/SecurityApiAccessTab';
import { SecurityLogsTab } from '../security/SecurityLogsTab';
import { TagsFileTaggingTab } from '../tags/TagsFileTaggingTab';
import { TagsFileVersioningTab } from '../tags/TagsFileVersioningTab';
import { TagSettingsTab } from '../tags/TagSettingsTab';
import { VersionSettingsTab } from '../tags/VersionSettingsTab';
import { CliCommandLineTab } from '../cli/CliCommandLineTab';
import { ReportsRunReportTab } from '../reports/ReportsRunReportTab';
import { ReportsCreatorTab } from '../reports/ReportsCreatorTab';
import { ReportsSchedulerTab } from '../reports/ReportsSchedulerTab';
import { ReportsSettingsTab } from '../reports/ReportsSettingsTab';
import { NotificationsAlertsTab } from '../notifications/NotificationsAlertsTab';
import { NotificationsEndpointsTab } from '../notifications/NotificationsEndpointsTab';
import { NotificationsCreateTab } from '../notifications/NotificationsCreateTab';
import { NotificationsHistoryTab } from '../notifications/NotificationsHistoryTab';
import { HelpSupportTab } from '../help/HelpSupportTab';
import { HelpSupportBundleTab } from '../help/HelpSupportBundleTab';
import { HelpDocsTab } from '../help/HelpDocsTab';
import { HelpSearchTab } from '../help/HelpSearchTab';
import { HelpAiTab } from '../help/HelpAiTab';


interface DashboardContentProps {
  activeTab: string;
  activeSidebarItem?: string;
}

const MyDashboardsTab = () => (
  <PlaceholderTab
    title="My Dashboards"
    description="Create and manage dashboard layouts tailored to your workflow."
  />
);

const sidebarTabComponents: Record<string, Record<string, ComponentType>> = {
  home: {
    dashboard: MyDashboardsTab,
    dash_setup: HomeDashSetupTab,
    compo_creator: HomeComponentCreatorTab,
  },
  cluster: {
    manage: ClusterManageTab,
    monitor: ClusterMonitorTab,
    performance: ClusterPerformanceTab,
    clients: ClusterClientsTab,
  },
  servers: {
    manage: ServersManageTab,
    monitor: ServersMonitorTab,
    performance: ServersPerformanceTab,
  },
  disk: {
    manage: DiskManageTab,
    monitor: DiskMonitorTab,
    performance: DiskPerformanceTab,
  },
  security: {
    access: SecurityAccessTab,
    audit: SecurityAuditTab,
    api_access: SecurityApiAccessTab,
    logs: SecurityLogsTab,
  },
  tags: {
    tags: TagsFileTaggingTab,
    versions: TagsFileVersioningTab,
    tag_settings: TagSettingsTab,
    version_settings: VersionSettingsTab,
  },
  cli: {
    cmd_line: CliCommandLineTab,
  },
  reports: {
    reports: ReportsRunReportTab,
    rep_creator: ReportsCreatorTab,
    rep_scheduler: ReportsSchedulerTab,
    rep_settings: ReportsSettingsTab,
  },
  notifications: {
    alerts: NotificationsAlertsTab,
    not_endpoints: NotificationsEndpointsTab,
    not_create: NotificationsCreateTab,
    not_history: NotificationsHistoryTab,
  },
  help: {
    hel_support: HelpSupportTab,
    hel_bundle: HelpSupportBundleTab,
    hel_docs: HelpDocsTab,
    hel_search: HelpSearchTab,
    hel_ai: HelpAiTab,
  },
};

export function HomeDashboardTab({ activeTab, activeSidebarItem = 'home' }: DashboardContentProps) {
  if (activeSidebarItem === 'profile') {
    return (
      <div className="p-6">
        <ProfilePage />
      </div>
    );
  }

  const sidebarTabs = sidebarTabComponents[activeSidebarItem];
  if (sidebarTabs) {
    const TabComponent = sidebarTabs[activeTab];
    if (TabComponent) {
      return (
        <div className="p-6">
          <TabComponent />
        </div>
      );
    }
  }

  return (
    <div className="p-6 text-sm text-muted-foreground">
      Select a tab from the toolbar to begin.
    </div>
  );
}
