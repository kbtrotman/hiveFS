import { useState, useEffect, useCallback } from 'react';
import { Sidebar } from './components/Sidebar';
import { TopBar } from './components/TopBar';
import { DashboardContent } from './components/DashboardContent';
import { LoginPage } from './components/LoginPage';
import { ClusterInitializationScreen } from './components/ClusterInitScreen';
import { NetworkingSetupScreen, type NetworkingConfiguration } from './components/NetworkingSetupScreen';
import { FilesystemSetupScreen, type FilesystemSetupConfiguration } from './components/FilesystemSetupScreen';
import { PermissionsSetupScreen } from './components/PermissionsSetupScreen';
import { Dialog, DialogContent, DialogDescription, DialogHeader, DialogTitle } from './components/ui/dialog';

const BOOTSTRAP_STATUS_URL =
  import.meta?.env?.VITE_BOOTSTRAP_STATUS_URL ??
  'http://localhost:8000/api/v1/bootstrap/status';
const BOOTSTRAP_READY_STATUSES = new Set(['IDLE', 'OP_PENDING']);

export default function App() {
  const [theme, setTheme] = useState<'light' | 'dark'>('dark');
  const [activeTab, setActiveTab] = useState('dashboard');
  const [activeSidebarItem, setActiveSidebarItem] = useState('home');
  
  // Authentication state
  const [isAuthenticated, setIsAuthenticated] = useState(false);
  const [authToken, setAuthToken] = useState<string | null>(null);
  const [user, setUser] = useState<any>(null);
  const [showClusterInitScreen, setShowClusterInitScreen] = useState(false);
  const [bootstrapStatusChecked, setBootstrapStatusChecked] = useState(false);
  const [bootstrapStatusData, setBootstrapStatusData] = useState<any>(null);
  const [networkSettingsOpen, setNetworkSettingsOpen] = useState(false);
  const [networkInitialSection, setNetworkInitialSection] = useState<'network' | 'dns' | 'time'>('network');
  const [networkIsSaving, setNetworkIsSaving] = useState(false);
  const [filesystemSettingsOpen, setFilesystemSettingsOpen] = useState(false);
  const [filesystemInitialTab, setFilesystemInitialTab] = useState<'cluster' | 'filesystem'>('cluster');
  const [permissionsSettingsOpen, setPermissionsSettingsOpen] = useState(false);
  const [permissionsInitialTab, setPermissionsInitialTab] = useState<'users' | 'groups' | 'roles'>('users');

  const toggleTheme = () => {
    setTheme(theme === 'light' ? 'dark' : 'light');
  };

  const handleLoginSuccess = (token: string, userData: any) => {
    setAuthToken(token);
    setUser(userData);
    setIsAuthenticated(true);
    
    // Store token in localStorage for persistence across page refreshes
    localStorage.setItem('authToken', token);
    localStorage.setItem('user', JSON.stringify(userData));
  };

  const handleLogout = () => {
    setIsAuthenticated(false);
    setAuthToken(null);
    setUser(null);
    localStorage.removeItem('authToken');
    localStorage.removeItem('user');
  };

  const openNetworkSettings = (section: 'network' | 'dns' | 'time') => {
    setNetworkInitialSection(section);
    setNetworkSettingsOpen(true);
  };

  const openFilesystemSettings = (tab: 'cluster' | 'filesystem') => {
    setFilesystemInitialTab(tab);
    setFilesystemSettingsOpen(true);
  };

  const openPermissionsSettings = (tab: 'users' | 'groups' | 'roles') => {
    setPermissionsInitialTab(tab);
    setPermissionsSettingsOpen(true);
  };

  const handleNetworkDialogCancel = () => {
    setNetworkSettingsOpen(false);
  };

  const handleNetworkDialogSave = (configuration: NetworkingConfiguration) => {
    setNetworkIsSaving(true);
    try {
      console.debug('Saving networking configuration', configuration);
    } finally {
      setNetworkIsSaving(false);
      setNetworkSettingsOpen(false);
    }
  };

  const handleFilesystemDialogSave = (configuration: FilesystemSetupConfiguration) => {
    console.debug('Saving filesystem configuration', configuration);
    setFilesystemSettingsOpen(false);
  };

  const networkingDialog = (
    <Dialog open={networkSettingsOpen} onOpenChange={setNetworkSettingsOpen}>
      <DialogContent className="h-[90vh] max-h-[90vh] w-full max-w-5xl overflow-hidden p-0">
        <div className="flex h-full flex-col">
          <DialogHeader className="px-6 pt-6">
            <DialogTitle>Networking Setup</DialogTitle>
            <DialogDescription>Configure interfaces, DNS, and time services.</DialogDescription>
          </DialogHeader>
          <div className="flex-1 overflow-y-auto">
            <NetworkingSetupScreen
              initialSection={networkInitialSection}
              onCancel={handleNetworkDialogCancel}
              onSave={handleNetworkDialogSave}
              isSaving={networkIsSaving}
            />
          </div>
        </div>
      </DialogContent>
    </Dialog>
  );

  const filesystemDialog = (
    <Dialog open={filesystemSettingsOpen} onOpenChange={setFilesystemSettingsOpen}>
      <DialogContent className="h-[90vh] max-h-[90vh] w-full max-w-5xl overflow-hidden p-0">
        <div className="flex h-full flex-col">
          <DialogHeader className="px-6 pt-6">
            <DialogTitle>Filesystem Configuration</DialogTitle>
            <DialogDescription>Adjust cluster-wide and filesystem-specific options.</DialogDescription>
          </DialogHeader>
          <div className="flex-1 overflow-y-auto">
            <FilesystemSetupScreen initialTab={filesystemInitialTab} onSave={handleFilesystemDialogSave} />
          </div>
        </div>
      </DialogContent>
    </Dialog>
  );

  const permissionsDialog = (
    <Dialog open={permissionsSettingsOpen} onOpenChange={setPermissionsSettingsOpen}>
      <DialogContent className="h-[90vh] max-h-[90vh] w-full max-w-6xl overflow-hidden">
        <DialogHeader>
          <DialogTitle>Permission Setup</DialogTitle>
          <DialogDescription>Manage users, groups, and roles across HiveFS.</DialogDescription>
        </DialogHeader>
        <div className="h-full overflow-y-auto pr-1">
          <PermissionsSetupScreen initialTab={permissionsInitialTab} />
        </div>
      </DialogContent>
    </Dialog>
  );

  const fetchBootstrapStatus = useCallback(async (token?: string | null) => {
    setBootstrapStatusChecked(false);
    try {
      const headers: Record<string, string> = {};
      if (token) {
        headers.Authorization = `Token ${token}`;
      }
      const response = await fetch(BOOTSTRAP_STATUS_URL, {
        method: 'GET',
        headers,
        /* credentials: 'include', */
      });
      let data: any = null;
      try {
        data = await response.json();
      } catch {
        /* ignore parse errors */
      }
      setBootstrapStatusData(data);

      const toLower = (value: unknown) => (typeof value === 'string' ? value.toLowerCase() : '');
      const readyFlagRaw = data?.ready_4_web_conf;
      const hasReadyFlag = readyFlagRaw !== undefined && readyFlagRaw !== null;
      let readyForWeb = false;
      if (typeof readyFlagRaw === 'boolean') {
        readyForWeb = readyFlagRaw;
      } else if (typeof readyFlagRaw === 'number') {
        readyForWeb = readyFlagRaw === 1;
      } else if (typeof readyFlagRaw === 'string') {
        const normalized = readyFlagRaw.trim().toLowerCase();
        readyForWeb = ['yes', 'true', '1', 'ready'].includes(normalized);
      }
      const stageValueRaw = data?.stage_of_config;
      const stageOfConfig =
        typeof stageValueRaw === 'number'
          ? stageValueRaw
          : Number.isFinite(parseInt(stageValueRaw, 10))
            ? parseInt(stageValueRaw, 10)
            : 0;
      const awaitingConfiguration = readyForWeb && stageOfConfig !== 5;
      setShowClusterInitScreen(awaitingConfiguration);
    } catch {
      setShowClusterInitScreen(false);
      setBootstrapStatusData(null);
    } finally {
      setBootstrapStatusChecked(true);
    }
  }, []);

  // Check for existing session on mount
  useEffect(() => {
    const storedToken = localStorage.getItem('authToken');
    const storedUser = localStorage.getItem('user');
    
    if (storedToken && storedUser) {
      // Optionally validate token with your API here
      setAuthToken(storedToken);
      setUser(JSON.parse(storedUser));
      setIsAuthenticated(true);
    }
  }, []);

  useEffect(() => {
    if (isAuthenticated) {
      fetchBootstrapStatus(authToken);
    } else {
      setBootstrapStatusChecked(false);
      setShowClusterInitScreen(false);
      setBootstrapStatusData(null);
    }
  }, [isAuthenticated, authToken, fetchBootstrapStatus]);

  // Show login page if not authenticated
  if (!isAuthenticated) {
    return (
      <div className={theme === 'dark' ? 'dark' : ''}>
        <LoginPage onLoginSuccess={handleLoginSuccess} />
      </div>
    );
  }

  if (!bootstrapStatusChecked) {
    return (
      <div className={theme === 'dark' ? 'dark' : ''}>
        <div className="flex h-screen items-center justify-center bg-background text-foreground">
          Checking bootstrap status...
        </div>
      </div>
    );
  }

  // Show dashboard if authenticated
  return (
    <div className={theme === 'dark' ? 'dark' : ''}>
      <div className="flex h-screen bg-background text-foreground">
        <Sidebar 
          activeItem={activeSidebarItem}
          onItemClick={setActiveSidebarItem}
        />
        <div className="flex-1 flex flex-col overflow-hidden">
          <TopBar 
            theme={theme}
            onThemeToggle={toggleTheme}
            activeTab={activeTab}
            onTabChange={setActiveTab}
            activeSidebarItem={activeSidebarItem}
            user={user}
            onLogout={handleLogout}
            onNavigateToFavorite={(sidebarId, tabId) => {
              setActiveSidebarItem(sidebarId);
              setActiveTab(tabId);
            }}
            onOpenNetworkSettings={openNetworkSettings}
            onOpenFilesystemSettings={openFilesystemSettings}
            onOpenPermissionsSettings={openPermissionsSettings}
            settingsEnabled
          />
          <main className="flex-1 overflow-y-auto bg-muted/20">
            {showClusterInitScreen ? (
              <ClusterInitializationScreen
                statusData={bootstrapStatusData}
                onActionComplete={() => fetchBootstrapStatus(authToken)}
              />
            ) : (
              <DashboardContent activeTab={activeTab} activeSidebarItem={activeSidebarItem} />
            )}
          </main>
          {networkingDialog}
          {filesystemDialog}
          {permissionsDialog}
        </div>
      </div>
    </div>
  );
}
