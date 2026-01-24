'use client';

import { useMemo, useState, type CSSProperties } from 'react';
import { Moon, Sun, Settings, Bookmark, User, LogOut, Star, StarOff } from 'lucide-react';
import { Button } from './ui/button';
import {
  DropdownMenu,
  DropdownMenuContent,
  DropdownMenuItem,
  DropdownMenuLabel,
  DropdownMenuSeparator,
  DropdownMenuTrigger,
} from './ui/dropdown-menu';

type NetworkSettingsSection = 'network' | 'dns' | 'time';
type PermissionSettingsSection = 'users' | 'groups' | 'roles';

interface TopBarProps {
  theme: 'light' | 'dark';
  onThemeToggle: () => void;
  activeTab: string;
  onTabChange: (tab: string) => void;
  activeSidebarItem?: string;
  user?: any;
  onLogout?: () => void;
  onNavigateToFavorite?: (sidebarId: string, tabId: string) => void;
  showTabs?: boolean;
  favoritesEnabled?: boolean;
  settingsEnabled?: boolean;
  onOpenNetworkSettings?: (section: NetworkSettingsSection) => void;
  onOpenFilesystemSettings?: (section: 'cluster' | 'filesystem') => void;
  onOpenPermissionsSettings?: (section: PermissionSettingsSection) => void;
}

const tabs = [
  { id: 'dashboard', label: 'My Dashboards' },
  { id: 'dash_setup', label: 'Dashboard Setup' },
  { id: 'compo_creator', label: 'Component Creator' },
  { id: 'clients', label: 'Clients' },
  { id: 'monitor', label: 'Monitor' },
  { id: 'manage', label: 'Manage' },
  { id: 'performance', label: 'Performance' },
  { id: 'access', label: 'Access' },
  { id: 'audit', label: 'Audit' },
  { id: 'api_access', label: 'API Access' },
  { id: 'logs', label: 'Logs' },
  { id: 'tags', label: 'File Tagging' },
  { id: 'versions', label: 'File Versioning' },
  { id: 'tag_settings', label: 'Tag Settings' },
  { id: 'version_settings', label: 'Version Settings' },
  { id: 'cmd_line', label: 'Open Node Consoles' },
  { id: 'reports', label: 'Run Report' },
  { id: 'rep_creator', label: 'Create Report' },
  { id: 'rep_scheduler', label: 'Report Scheduler' },
  { id: 'rep_settings', label: 'Report Settings' },
  { id: 'alerts', label: 'Alerts' },
  { id: 'create_alerts', label: 'Create Alerts' },
  { id: 'not_endpoints', label: 'Notification Endpoints' },
  { id: 'not_create', label: 'Create Notifications' },
  { id: 'not_history', label: 'Notification History' },
  { id: 'hel_support', label: 'Get Support' },
  { id: 'hel_bundle', label: 'Create Support Bundle' },
  { id: 'hel_docs', label: 'Documentation' },
  { id: 'hel_search', label: 'Search Help' },
  { id: 'hel_ai', label: 'AI Helper' },
];

// Define which tabs are available for each sidebar item
const tabAvailability: Record<string, string[]> = {
  home: ['dashboard', 'dash_setup', 'compo_creator'],
  cluster: ['manage', 'monitor', 'performance', 'clients'],
  servers: ['manage', 'monitor', 'performance'],
  disk: ['manage', 'monitor', 'performance'],
  security: ['access', 'audit', 'api_access', 'logs'],
  tags: ['tags', 'versions', 'tag_settings', 'version_settings'],
  cli: ['cmd_line'],
  reports: ['reports', 'rep_creator', 'rep_scheduler', 'rep_settings'],
  notifications: ['alerts', 'create_alerts', 'not_endpoints', 'not_create', 'not_history'],
  help: ['hel_support', 'hel_bundle', 'hel_docs', 'hel_search', 'hel_ai'],
  profile: [],
};

const titleCase = (value: string) =>
  value
    .split(/[\s-_]+/)
    .filter(Boolean)
    .map((word) => word.charAt(0).toUpperCase() + word.slice(1))
    .join(' ');

type FavoriteEntry = { id: string; label: string; sidebarId: string; tabId: string };
const DEFAULT_FAVORITE: FavoriteEntry = {
  id: 'home:dashboard',
  label: 'My Dashboard',
  sidebarId: 'home',
  tabId: 'dashboard',
};

export function TopBar({
  theme,
  onThemeToggle,
  activeTab,
  onTabChange,
  activeSidebarItem = 'home',
  user,
  onLogout,
  onNavigateToFavorite,
  showTabs = true,
  favoritesEnabled = true,
  settingsEnabled = true,
  onOpenNetworkSettings,
  onOpenFilesystemSettings,
  onOpenPermissionsSettings,
}: TopBarProps) {
  const availableTabs = showTabs ? (tabAvailability[activeSidebarItem] || []) : [];
  const visibleTabs = tabs.filter((tab) => availableTabs.includes(tab.id));
  const tabsVisible = showTabs && visibleTabs.length > 0;
  const [favorites, setFavorites] = useState<FavoriteEntry[]>([DEFAULT_FAVORITE]);

  const currentFavoriteId = useMemo(
    () => `${activeSidebarItem || 'home'}:${activeTab || 'dashboard'}`,
    [activeSidebarItem, activeTab],
  );
  const currentFavoriteLabel = useMemo(() => {
    const sidebarLabel = titleCase(activeSidebarItem || 'home');
    const tabLabel = titleCase(activeTab || 'dashboard');
    return `${sidebarLabel} • ${tabLabel}`;
  }, [activeSidebarItem, activeTab]);
  const isCurrentFavorite = useMemo(
    () => favorites.some((fav) => fav.id === currentFavoriteId),
    [favorites, currentFavoriteId],
  );

  const handleAddFavorite = () => {
    if (isCurrentFavorite)
      return;
    setFavorites((prev) => [
      ...prev,
      {
        id: currentFavoriteId,
        label: currentFavoriteLabel,
        sidebarId: activeSidebarItem,
        tabId: activeTab,
      },
    ]);
  };

  const handleRemoveFavorite = () => {
    if (!isCurrentFavorite || currentFavoriteId === DEFAULT_FAVORITE.id)
      return;
    setFavorites((prev) => prev.filter((fav) => fav.id !== currentFavoriteId));
  };

  const navigateToFavorite = (favorite: FavoriteEntry) => {
    if (onNavigateToFavorite)
      onNavigateToFavorite(favorite.sidebarId, favorite.tabId);
    else
      onTabChange(favorite.tabId);
  };

  const gradientStyle: CSSProperties = {
    backgroundColor: 'var(--card)',
    backgroundImage: `
      radial-gradient(circle at 20% 20%, rgba(59, 130, 246, 0.09), transparent 58%),
      radial-gradient(circle at 80% 82%, rgba(188, 126, 83, 0.12), transparent 55%),
      linear-gradient(
        135deg,
        color-mix(in srgb, var(--card) 92%, rgba(59, 130, 246, 0.14)),
        color-mix(in srgb, var(--card) 92%, rgba(188, 126, 83, 0.14))
      )
    `,
  };

  const openNetworkSettings = (section: NetworkSettingsSection) => {
    if (onOpenNetworkSettings)
      onOpenNetworkSettings(section);
  };

  const openFilesystemSettings = (section: 'cluster' | 'filesystem') => {
    onOpenFilesystemSettings?.(section);
  };

  const openPermissionsSettings = (section: PermissionSettingsSection) => {
    onOpenPermissionsSettings?.(section);
  };

  return (
    <header
      className="relative h-16 bg-card border-b border-border overflow-hidden"
      style={gradientStyle}
    >
      <div
        aria-hidden="true"
        className="pointer-events-none absolute -inset-16 bg-[radial-gradient(circle_at_20%_20%,rgba(59,130,246,0.08),transparent_58%)] blur-3xl opacity-35"
      />
      <div
        aria-hidden="true"
        className="pointer-events-none absolute -inset-24 bg-[radial-gradient(circle_at_85%_90%,rgba(188,126,83,0.11),transparent_55%)] blur-3xl opacity-35"
      />
      <div
        aria-hidden="true"
        className="pointer-events-none absolute inset-0 bg-gradient-to-br from-sky-500/10 via-transparent to-orange-500/10 opacity-45"
      />
      <div className="relative z-10 flex h-full items-center justify-between px-6">
      {/* Tabs */}
      {tabsVisible && (
        <div className="flex flex-wrap gap-1">
          {visibleTabs.map((tab) => (
            <button
              key={tab.id}
              onClick={() => onTabChange(tab.id)}
              className={`
                px-4 py-2 rounded-md transition-colors text-sm
                ${activeTab === tab.id
                  ? 'bg-primary text-primary-foreground'
                  : 'text-muted-foreground hover:bg-muted hover:text-foreground'}
              `}
            >
              {tab.label}
            </button>
          ))}
        </div>
      )}

      {/* Right Icons */}
      <div className="flex items-center gap-2">
        {favoritesEnabled && (
          <DropdownMenu>
            <DropdownMenuTrigger asChild>
              <Button variant="ghost" size="icon">
                <Bookmark className="w-5 h-5" />
              </Button>
            </DropdownMenuTrigger>
            <DropdownMenuContent align="end">
              <DropdownMenuLabel>Favorites</DropdownMenuLabel>
              <DropdownMenuItem onClick={handleAddFavorite} disabled={isCurrentFavorite}>
                {isCurrentFavorite ? (
                  <StarOff className="w-4 h-4 mr-2" />
                ) : (
                  <Star className="w-4 h-4 mr-2" />
                )}
                Add to Favorites
              </DropdownMenuItem>
              <DropdownMenuItem
                onClick={handleRemoveFavorite}
                disabled={!isCurrentFavorite || currentFavoriteId === DEFAULT_FAVORITE.id}
              >
                <StarOff className="w-4 h-4 mr-2" />
                Remove from Favorites
              </DropdownMenuItem>
              <DropdownMenuSeparator />
              {favorites.map((favorite) => (
                <DropdownMenuItem
                  key={favorite.id}
                  onSelect={(event) => {
                    event.preventDefault();
                    navigateToFavorite(favorite);
                  }}
                >
                  {favorite.label}
                </DropdownMenuItem>
              ))}
            </DropdownMenuContent>
          </DropdownMenu>
        )}
        {settingsEnabled && (
          <DropdownMenu>
            <DropdownMenuTrigger asChild>
              <Button variant="ghost" size="icon">
                <Settings className="w-5 h-5" />
              </Button>
            </DropdownMenuTrigger>
            <DropdownMenuContent align="end">
              <DropdownMenuLabel>
                Global Settings
              </DropdownMenuLabel>
              <DropdownMenuSeparator />
              <DropdownMenuItem onSelect={() => openPermissionsSettings('users')}>
                Users
              </DropdownMenuItem>
              <DropdownMenuItem onSelect={() => openPermissionsSettings('groups')}>
                Groups
              </DropdownMenuItem>
              <DropdownMenuItem onSelect={() => openPermissionsSettings('roles')}>
                Roles
              </DropdownMenuItem>
              <DropdownMenuItem>LDAP &amp; AD</DropdownMenuItem>
              <DropdownMenuItem>MFA Enforcement</DropdownMenuItem>
              <DropdownMenuSeparator />
              <DropdownMenuItem onSelect={() => openFilesystemSettings('cluster')}>
                Cluster Config
              </DropdownMenuItem>
              <DropdownMenuItem onSelect={() => openFilesystemSettings('filesystem')}>
                FS Settings
              </DropdownMenuItem>
              <DropdownMenuSeparator />
              <DropdownMenuItem onSelect={() => openNetworkSettings('network')}>
                Network
              </DropdownMenuItem>
              <DropdownMenuItem onSelect={() => openNetworkSettings('dns')}>
                DNS
              </DropdownMenuItem>
              <DropdownMenuItem onSelect={() => openNetworkSettings('time')}>
                Time Protocol
              </DropdownMenuItem>
              <DropdownMenuSeparator />
              <DropdownMenuItem onClick={onLogout}>
                <LogOut className="w-4 h-4 mr-2" />
                Logout
              </DropdownMenuItem>
            </DropdownMenuContent>
          </DropdownMenu>
        )}
        <Button variant="ghost" size="icon" onClick={onThemeToggle}>
          {theme === 'dark' ? <Sun className="w-5 h-5" /> : <Moon className="w-5 h-5" />}
        </Button>
        
        <DropdownMenu>
          <DropdownMenuTrigger asChild>
            <Button variant="ghost" size="icon">
              <User className="w-5 h-5" />
            </Button>
          </DropdownMenuTrigger>
          <DropdownMenuContent align="end">
            <DropdownMenuLabel>
              {user?.username || 'My Account'}
            </DropdownMenuLabel>
            <DropdownMenuSeparator />
            <DropdownMenuItem>Profile Settings</DropdownMenuItem>
            <DropdownMenuItem>Preferences</DropdownMenuItem>
            <DropdownMenuSeparator />
            <DropdownMenuItem onClick={onLogout}>
              <LogOut className="w-4 h-4 mr-2" />
              Logout
            </DropdownMenuItem>
          </DropdownMenuContent>
        </DropdownMenu>
      </div>
      </div>
    </header>
  );
}
