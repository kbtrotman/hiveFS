import type { ComponentType } from 'react';
import { ProfilePage } from './pages/ProfilePage';
import { HomeDashboardTab } from './pages/tabs/home/HomeDashboardTab';
import { HomeDashSetupTab } from './pages/tabs/home/HomeDashSetupTab';
import { HomeComponentCreatorTab } from './pages/tabs/home/HomeComponentCreatorTab';
import { ClusterManageTab } from './pages/tabs/cluster/ClusterManageTab';
import { ClusterMonitorTab } from './pages/tabs/cluster/ClusterMonitorTab';
import { ClusterPerformanceTab } from './pages/tabs/cluster/ClusterPerformanceTab';
import { ClusterClientsTab } from './pages/tabs/cluster/ClusterClientsTab';
import { ServersManageTab } from './pages/tabs/servers/ServersManageTab';
import { ServersMonitorTab } from './pages/tabs/servers/ServersMonitorTab';
import { ServersPerformanceTab } from './pages/tabs/servers/ServersPerformanceTab';
import { DiskManageTab } from './pages/tabs/disk/DiskManageTab';
import { DiskMonitorTab } from './pages/tabs/disk/DiskMonitorTab';
import { DiskPerformanceTab } from './pages/tabs/disk/DiskPerformanceTab';
import { SecurityAccessTab } from './pages/tabs/security/SecurityAccessTab';
import { SecurityAuditTab } from './pages/tabs/security/SecurityAuditTab';
import { SecurityApiAccessTab } from './pages/tabs/security/SecurityApiAccessTab';
import { SecurityLogsTab } from './pages/tabs/security/SecurityLogsTab';
import { FileTaggingTab } from './pages/tabs/tags/FileTagging';
import { FileVersioningTab } from './pages/tabs/tags/FileVersioningTab';
import { TagSettingsTab } from './pages/tabs/tags/TagSettingsTab';
import { VersionSettingsTab } from './pages/tabs/tags/VersionSettingsTab';
import { CliCommandLineTab } from './pages/tabs/cli/CliCommandLineTab';
import { ReportsRunReportTab } from './pages/tabs/reports/ReportsRunReportTab';
import { ReportsCreatorTab } from './pages/tabs/reports/ReportsCreatorTab';
import { ReportsSchedulerTab } from './pages/tabs/reports/ReportsSchedulerTab';
import { ReportsSettingsTab } from './pages/tabs/reports/ReportsSettingsTab';
import { NotificationsAlertsTab } from './pages/tabs/notifications/NotificationsAlertsTab';
import { NotificationsEndpointsTab } from './pages/tabs/notifications/NotificationsEndpointsTab';
import { NotificationsCreateTab } from './pages/tabs/notifications/NotificationsCreateTab';
import { NotificationsHistoryTab } from './pages/tabs/notifications/NotificationsHistoryTab';
import { HelpSupportTab } from './pages/tabs/help/HelpSupportTab';
import { HelpSupportBundleTab } from './pages/tabs/help/HelpSupportBundleTab';
import { HelpDocsTab } from './pages/tabs/help/HelpDocsTab';
import { HelpSearchTab } from './pages/tabs/help/HelpSearchTab';
import { HelpAiTab } from './pages/tabs/help/HelpAiTab';
import { NetworkingSetupScreen } from './NetworkingSetupScreen';
import { FilesystemSetupScreen } from './FilesystemSetupScreen';
import { PermissionsSetupScreen } from './PermissionsSetupScreen';
import { ClusterInitializationScreen } from './ClusterInitScreen';

const NetworkingSetupTab = () => <NetworkingSetupScreen initialSection="network" />;
const FilesystemSetupTab = () => <FilesystemSetupScreen initialTab="cluster" />;
const PermissionsSetupTab = () => <PermissionsSetupScreen initialTab="users" />;
const ClusterInitTab = () => <ClusterInitializationScreen />;


interface DashboardContentProps {
  activeTab: string;
  activeSidebarItem?: string;
}

const sidebarTabComponents: Record<string, Record<string, ComponentType>> = {
  home: {
    dashboard: HomeDashboardTab,
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
    tags: FileTaggingTab,
    versions: FileVersioningTab,
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
  setup: {
    cluster_init: ClusterInitTab,
    network_setup: NetworkingSetupTab,
    filesystem_setup: FilesystemSetupTab,
    permissions_setup: PermissionsSetupTab,
  },
};

export function DashboardContent({ activeTab, activeSidebarItem = 'home' }: DashboardContentProps) {
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
