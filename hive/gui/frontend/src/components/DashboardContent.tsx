import { PerformanceTab } from './tabs/Dashboard/PerformanceTab';
import { DashboardSetupTab } from './tabs/Dashboard/DashboardSetupTab';
import { ClientsTab } from './tabs/Dashboard/ClientsTab';
import { StatusTab } from './tabs/Dashboard/StatusTab';
import { ServersPage } from './pages/ServersPage';
import { DiskPage } from './pages/DiskPage';
import { SecurityPage } from './pages/SecurityPage';
import { TagsVersioningPage } from './pages/TagsVersioningPage';
import { CommandLinePage } from './pages/CommandLinePage';
import { ReportsPage } from './pages/ReportsPage';
import { NotificationsPage } from './pages/NotificationsPage';
import { HelpPage } from './pages/HelpPage';
import { ProfilePage } from './pages/ProfilePage';

// Nodes tabs
import { NodesDashboardTab } from './tabs/nodes/NodesDashboardTab';
import { NodesStatusTab } from './tabs/nodes/NodesStatusTab';
import { NodesPerformanceTab } from './tabs/nodes/NodesPerformanceTab';
import { NodesClientsTab } from './tabs/nodes/NodesClientsTab';


interface DashboardContentProps {
  activeTab: string;
  activeSidebarItem?: string;
}

export function DashboardContent({ activeTab, activeSidebarItem = 'home' }: DashboardContentProps) {
  // If on home, show the tabs-based dashboard
  if (activeSidebarItem === 'home') {
    return (
      <div className="p-6">
        {activeTab === 'performance' && <PerformanceTab />}
        {activeTab === 'dashboard' && <DashboardSetupTab />}
        {activeTab === 'clients' && <ClientsTab />}
        {activeTab === 'status' && <StatusTab />}
      </div>
    );
  }

  // Nodes section with tabs
  if (activeSidebarItem === 'servers') {
    return (
      <div className="p-6">
        {activeTab === 'dashboard' && <NodesStatusTab />}
        {activeTab === 'status' && <ServersPage />}
        {activeTab === 'monitor' && <NodesDashboardTab />}
        {activeTab === 'performance' && <NodesPerformanceTab />}
        {activeTab === 'clients' && <NodesClientsTab />}
      </div>
    );
  }

  // Disk section with tabs
  if (activeSidebarItem === 'disk') {
    return (
      <div className="p-6">
        {activeTab === 'dashboard' && <DiskPage />}
        {activeTab === 'status' && <DiskPage />}
        {activeTab === 'performance' && <DiskPage />}
        {activeTab === 'clients' && <DiskPage />}
      </div>
    );
  }

  // Security section with tabs
  if (activeSidebarItem === 'security') {
    return (
      <div className="p-6">
        {activeTab === 'dashboard' && <SecurityPage />}
        {activeTab === 'status' && <SecurityPage />}
        {activeTab === 'performance' && <SecurityPage />}
        {activeTab === 'clients' && <SecurityPage />}
      </div>
    );
  }

  // Tags section with tabs (no clients)
  if (activeSidebarItem === 'tags') {
    return (
      <div className="p-6">
        {activeTab === 'dashboard' && <TagsVersioningPage />}
        {activeTab === 'status' && <TagsVersioningPage />}
        {activeTab === 'performance' && <TagsVersioningPage />}
      </div>
    );
  }

  // CLI section with tabs (only dashboard and status)
  if (activeSidebarItem === 'cli') {
    return (
      <div className="p-6">
        {activeTab === 'dashboard' && <CommandLinePage />}
        {activeTab === 'status' && <CommandLinePage />}
      </div>
    );
  }

  // Reports section (only dashboard)
  if (activeSidebarItem === 'reports') {
    return (
      <div className="p-6">
        {activeTab === 'dashboard' && <ReportsPage />}
      </div>
    );
  }

  // Notifications section with tabs (only dashboard and status)
  if (activeSidebarItem === 'notifications') {
    return (
      <div className="p-6">
        {activeTab === 'dashboard' && <NotificationsPage />}
        {activeTab === 'status' && <NotificationsPage />}
      </div>
    );
  }

  // Pages without tabs
  return (
    <div className="p-6">
      {activeSidebarItem === 'help' && <HelpPage />}
      {activeSidebarItem === 'profile' && <ProfilePage />}
    </div>
  );
}