import { useState, useEffect, useCallback } from 'react';
import { Sidebar } from './components/Sidebar';
import { TopBar } from './components/TopBar';
import { DashboardContent } from './components/DashboardContent';
import { LoginPage } from './components/LoginPage';
import { ClusterInitializationScreen } from './components/ClusterInitScreen';
import { NetworkingSetupScreen, type NetworkingConfiguration } from './components/NetworkingSetupScreen';
import { FilesystemSetupScreen, type FilesystemSetupConfiguration } from './components/FilesystemSetupScreen';
import { PermissionsSetupScreen } from './components/PermissionsSetupScreen';
import { Button } from './components/ui/button';

type SetupView = 'network' | 'filesystem' | 'permissions';

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
  const [networkInitialSection, setNetworkInitialSection] = useState<'network' | 'dns' | 'time'>('network');
  const [networkIsSaving, setNetworkIsSaving] = useState(false);
  const [filesystemInitialTab, setFilesystemInitialTab] = useState<'cluster' | 'filesystem'>('cluster');
  const [permissionsInitialTab, setPermissionsInitialTab] = useState<'users' | 'groups' | 'roles'>('users');
  const [activeSetupView, setActiveSetupView] = useState<SetupView | null>(null);

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
    setActiveSetupView('network');
  };

  const openFilesystemSettings = (tab: 'cluster' | 'filesystem') => {
    setFilesystemInitialTab(tab);
    setActiveSetupView('filesystem');
  };

  const openPermissionsSettings = (tab: 'users' | 'groups' | 'roles') => {
    setPermissionsInitialTab(tab);
    setActiveSetupView('permissions');
  };

  const closeSetupView = () => {
    setActiveSetupView(null);
  };

  const handleNetworkDialogCancel = () => {
    closeSetupView();
  };

  const handleNetworkDialogSave = (configuration: NetworkingConfiguration) => {
    setNetworkIsSaving(true);
    try {
      console.debug('Saving networking configuration', configuration);
    } finally {
      setNetworkIsSaving(false);
      closeSetupView();
    }
  };

  const handleFilesystemDialogSave = (configuration: FilesystemSetupConfiguration) => {
    console.debug('Saving filesystem configuration', configuration);
    closeSetupView();
  };

  useEffect(() => {
    if (showClusterInitScreen && activeSetupView)
      setActiveSetupView(null);
  }, [showClusterInitScreen, activeSetupView]);

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
  const navigationLocked = showClusterInitScreen;
  return (
    <div className={theme === 'dark' ? 'dark' : ''}>
      <div className="flex h-screen bg-background text-foreground">
        <Sidebar 
          activeItem={activeSidebarItem}
          onItemClick={setActiveSidebarItem}
          disabled={navigationLocked}
        />
        <div className="flex-1 flex min-h-0 flex-col overflow-hidden">
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
            settingsEnabled={!navigationLocked}
            favoritesEnabled={!navigationLocked}
            showTabs={!navigationLocked}
            navigationLocked={navigationLocked}
          />
          <main className="flex-1 overflow-hidden bg-muted/20">
            {showClusterInitScreen ? (
              <div className="h-full overflow-y-auto">
                <ClusterInitializationScreen
                  statusData={bootstrapStatusData}
                  onActionComplete={() => fetchBootstrapStatus(authToken)}
                  userId={user?.id ?? user?.user_id ?? null}
                />
              </div>
            ) : activeSetupView ? (
              <div className="relative flex h-full w-full items-center justify-center px-2 py-2">
                <div className="absolute inset-0 bg-background/80 backdrop-blur-sm" />
                <div
                  className={`relative z-10 flex min-h-0 flex-col overflow-hidden rounded-xl border border-border bg-card shadow-2xl ${
                    activeSetupView === 'permissions'
                      ? 'h-[calc(100%-20px)] w-[calc(100%-20px)] max-w-[1400px]'
                      : 'h-[calc(100%-30px)] w-[calc(100%-30px)] max-w-[1200px]'
                  }`}
                >
                  <div className="flex items-center justify-between border-b border-border px-6 py-4">
                    <div>
                      <h2 className="text-xl font-semibold">
                        {activeSetupView === 'network'
                          ? 'Networking Setup'
                          : activeSetupView === 'filesystem'
                            ? 'Filesystem Configuration'
                            : 'Permission Setup'}
                      </h2>
                      <p className="text-sm text-muted-foreground">
                        {activeSetupView === 'network'
                          ? 'Configure interfaces, DNS, and time services.'
                          : activeSetupView === 'filesystem'
                            ? 'Adjust cluster-wide and filesystem-specific options.'
                            : 'Manage users, groups, and roles across HiveFS.'}
                      </p>
                    </div>
                    <Button variant="outline" onClick={closeSetupView}>
                      Close
                    </Button>
                  </div>
                  <div className="flex-1 min-h-0 overflow-y-auto">
                    {activeSetupView === 'network' && (
                      <NetworkingSetupScreen
                        initialSection={networkInitialSection}
                        onCancel={handleNetworkDialogCancel}
                        onSave={handleNetworkDialogSave}
                      isSaving={networkIsSaving}
                    />
                  )}
                  {activeSetupView === 'filesystem' && (
                    <FilesystemSetupScreen initialTab={filesystemInitialTab} onSave={handleFilesystemDialogSave} />
                  )}
                  {activeSetupView === 'permissions' && (
                    <PermissionsSetupScreen initialTab={permissionsInitialTab} />
                  )}
                  </div>
                </div>
              </div>
            ) : (
              <div className="h-full overflow-y-auto">
                <DashboardContent activeTab={activeTab} activeSidebarItem={activeSidebarItem} />
              </div>
            )}
          </main>
        </div>
      </div>
    </div>
  );
}
