import {
  Bell,
  FileText,
  HardDrive,
  HelpCircle,
  Home,
  Layers,
  Server,
  Shield,
  Tag,
  Terminal,
  User,
} from 'lucide-react';
import { ImageWithFallback } from './figma/ImageWithFallback';
import hiveFsLogo from './hfs_logo.png';
import type { CSSProperties } from 'react';

interface SidebarProps {
  activeItem: string;
  onItemClick: (item: string) => void;
  disabled?: boolean;
}

const menuItems = [
  { id: 'home', icon: Home, label: 'MY DASHBOARD' },
  { id: 'cluster', icon: Layers, label: 'Cluster' },
  { id: 'servers', icon: Server, label: 'Nodes' },
  { id: 'disk', icon: HardDrive, label: 'Disk' },
  { id: 'reports', icon: FileText, label: 'Reports' },
  { id: 'notifications', icon: Bell, label: 'Notifications' },
  { id: 'security', icon: Shield, label: 'Security' },
  { id: 'tags', icon: Tag, label: 'Tags/Versioning' },
  { id: 'cli', icon: Terminal, label: 'Command-line Access' },
  { id: 'help', icon: HelpCircle, label: 'Help' },
  { id: 'profile', icon: User, label: 'Profile Setup' },
];

export function Sidebar({ activeItem, onItemClick, disabled = false }: SidebarProps) {
  const baseColor = "var(--card)";
  const gradientStyle: CSSProperties = {
    backgroundColor: baseColor,
    backgroundImage: `
      radial-gradient(circle at 20% 20%, rgba(59, 130, 246, 0.09), transparent 58%),
      radial-gradient(circle at 80% 82%, rgba(188, 126, 83, 0.12), transparent 55%),
      linear-gradient(
        135deg,
        color-mix(in srgb, ${baseColor} 92%, rgba(59, 130, 246, 0.14)),
        color-mix(in srgb, ${baseColor} 92%, rgba(188, 126, 83, 0.14))
      )
    `,
  };

  return (
    <aside
      className="relative w-64 bg-card border-r border-border flex flex-col overflow-hidden"
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

      <div className="relative z-10 flex h-full flex-col">
        {/* Logo */}
        <div className="h-16 flex items-center px-6 border-b border-border">
          <div className="flex items-center gap-3">
            <ImageWithFallback 
              src={hiveFsLogo}
              alt="Beehive Logo"
              className="w-12 h-12 rounded-xl object-contain bg-card/60 p-1"
            />
            <span className="font-semibold">HiveFS</span>
          </div>
        </div>

        {/* Menu Items */}
        <nav className="flex-1 overflow-y-auto py-4">
          {menuItems.map((item) => {
            const Icon = item.icon;
            const isActive = activeItem === item.id;
            
            return (
              <button
                key={item.id}
                onClick={() => !disabled && onItemClick(item.id)}
                disabled={disabled}
                className={`
                  w-full flex items-center gap-3 px-6 py-3 transition-colors
                  ${disabled ? 'opacity-60 cursor-not-allowed' : ''}
                  ${isActive 
                    ? 'bg-primary text-primary-foreground' 
                    : 'text-muted-foreground hover:bg-muted hover:text-foreground'
                  }
                `}
              >
                <Icon className="w-5 h-5" />
                <span className="text-sm">{item.label}</span>
              </button>
            );
          })}
        </nav>
      </div>
    </aside>
  );
}
