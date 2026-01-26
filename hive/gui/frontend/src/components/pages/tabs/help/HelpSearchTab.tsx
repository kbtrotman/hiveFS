import { useState } from 'react';
import { Card, CardContent, CardHeader, CardTitle } from '../../../ui/card';
import { Input } from '../../../ui/input';
import { Label } from '../../../ui/label';
import { Button } from '../../../ui/button';
import { Separator } from '../../../ui/separator';
import { Badge } from '../../../ui/badge';
import {
  Search,
  Book,
  FileText,
  ExternalLink,
  TrendingUp,
  Clock,
  Sparkles,
  Filter,
  ChevronRight,
  Bookmark,
  Star,
  ArrowRight,
  HelpCircle,
  Zap,
  Code,
  Settings,
  Shield,
  Database,
} from 'lucide-react';

interface DocResult {
  id: string;
  title: string;
  category: string;
  excerpt: string;
  url: string;
  relevance: number;
  lastUpdated: string;
  tags: string[];
}

interface QuickLink {
  id: string;
  title: string;
  description: string;
  category: string;
  icon: typeof Book;
  url: string;
}

export function HelpSearchTab() {
  const [searchQuery, setSearchQuery] = useState('');
  const [selectedCategory, setSelectedCategory] = useState('all');
  const [isSearching, setIsSearching] = useState(false);
  const [hasSearched, setHasSearched] = useState(false);

  // Mock search results
  const [searchResults, setSearchResults] = useState<DocResult[]>([]);

  // Mock popular searches
  const popularSearches = [
    'cluster setup',
    'file versioning',
    'backup configuration',
    'performance tuning',
    'security settings',
    'API authentication',
  ];

  // Mock recent searches
  const [recentSearches] = useState([
    'How to configure email notifications',
    'Restore deleted files',
    'API key permissions',
  ]);

  // Categories
  const categories = [
    { value: 'all', label: 'All Documentation', icon: Book, color: '#6b7280' },
    { value: 'getting-started', label: 'Getting Started', icon: Sparkles, color: '#10b981' },
    { value: 'installation', label: 'Installation', icon: Settings, color: '#3b82f6' },
    { value: 'configuration', label: 'Configuration', icon: Settings, color: '#8b5cf6' },
    { value: 'features', label: 'Features', icon: Zap, color: '#f59e0b' },
    { value: 'api', label: 'API Reference', icon: Code, color: '#06b6d4' },
    { value: 'security', label: 'Security', icon: Shield, color: '#ef4444' },
    { value: 'troubleshooting', label: 'Troubleshooting', icon: HelpCircle, color: '#ec4899' },
  ];

  // Quick links to common topics
  const quickLinks: QuickLink[] = [
    {
      id: '1',
      title: 'Quick Start Guide',
      description: 'Get HiveFS up and running in minutes',
      category: 'getting-started',
      icon: Sparkles,
      url: 'https://github.com/kbtrotman/hivefs/wiki/Quick-Start',
    },
    {
      id: '2',
      title: 'Installation Guide',
      description: 'Complete installation instructions for all platforms',
      category: 'installation',
      icon: Settings,
      url: 'https://github.com/kbtrotman/hivefs/wiki/Installation',
    },
    {
      id: '3',
      title: 'Cluster Configuration',
      description: 'Configure and optimize your HiveFS cluster',
      category: 'configuration',
      icon: Database,
      url: 'https://github.com/kbtrotman/hivefs/wiki/Cluster-Configuration',
    },
    {
      id: '4',
      title: 'File Versioning',
      description: 'Block-based versioning and file recovery',
      category: 'features',
      icon: Clock,
      url: 'https://github.com/kbtrotman/hivefs/wiki/File-Versioning',
    },
    {
      id: '5',
      title: 'API Documentation',
      description: 'REST API endpoints and authentication',
      category: 'api',
      icon: Code,
      url: 'https://github.com/kbtrotman/hivefs/wiki/API-Reference',
    },
    {
      id: '6',
      title: 'Security Best Practices',
      description: 'Secure your HiveFS deployment',
      category: 'security',
      icon: Shield,
      url: 'https://github.com/kbtrotman/hivefs/wiki/Security',
    },
  ];

  // Mock all documentation (would be fetched from API in real implementation)
  const allDocs: DocResult[] = [
    {
      id: '1',
      title: 'Getting Started with HiveFS',
      category: 'getting-started',
      excerpt:
        'HiveFS is a distributed filesystem that provides high availability, scalability, and performance. This guide will help you set up your first cluster and configure basic settings...',
      url: 'https://github.com/kbtrotman/hivefs/wiki/Getting-Started',
      relevance: 95,
      lastUpdated: '2026-01-20T10:00:00Z',
      tags: ['beginner', 'setup', 'cluster'],
    },
    {
      id: '2',
      title: 'Configuring Email Notifications',
      category: 'configuration',
      excerpt:
        'Email notifications keep you informed about cluster events, alerts, and system status. Configure SMTP settings, notification endpoints, and alert rules to receive important updates...',
      url: 'https://github.com/kbtrotman/hivefs/wiki/Email-Notifications',
      relevance: 92,
      lastUpdated: '2026-01-18T14:30:00Z',
      tags: ['notifications', 'email', 'alerts'],
    },
    {
      id: '3',
      title: 'File Versioning and Recovery',
      category: 'features',
      excerpt:
        'HiveFS uses block-based versioning to track file changes over time. Learn how to configure retention policies, restore previous versions, and recover deleted files using the timeline interface...',
      url: 'https://github.com/kbtrotman/hivefs/wiki/File-Versioning',
      relevance: 88,
      lastUpdated: '2026-01-22T09:15:00Z',
      tags: ['versioning', 'backup', 'recovery', 'restore'],
    },
    {
      id: '4',
      title: 'API Authentication and Permissions',
      category: 'api',
      excerpt:
        'Secure your API access with key-based authentication. Create API keys, assign permissions, set expiration dates, and manage access control for your HiveFS cluster programmatically...',
      url: 'https://github.com/kbtrotman/hivefs/wiki/API-Authentication',
      relevance: 90,
      lastUpdated: '2026-01-19T16:45:00Z',
      tags: ['api', 'authentication', 'security', 'permissions'],
    },
    {
      id: '5',
      title: 'Performance Tuning Best Practices',
      category: 'configuration',
      excerpt:
        'Optimize your HiveFS deployment for maximum performance. Configure cache settings, adjust thread pools, tune network parameters, and monitor resource utilization for optimal throughput...',
      url: 'https://github.com/kbtrotman/hivefs/wiki/Performance-Tuning',
      relevance: 85,
      lastUpdated: '2026-01-21T11:00:00Z',
      tags: ['performance', 'optimization', 'tuning'],
    },
    {
      id: '6',
      title: 'Cluster Setup and Initialization',
      category: 'installation',
      excerpt:
        'Initialize a new HiveFS cluster with proper node configuration. Set cluster name, configure networking, add nodes, and establish quorum for high availability distributed storage...',
      url: 'https://github.com/kbtrotman/hivefs/wiki/Cluster-Setup',
      relevance: 93,
      lastUpdated: '2026-01-17T13:20:00Z',
      tags: ['cluster', 'setup', 'installation', 'nodes'],
    },
    {
      id: '7',
      title: 'Security Audit Logs',
      category: 'security',
      excerpt:
        'HiveFS maintains immutable audit logs of all security-related events. Monitor user access, track authentication attempts, review permission changes, and export audit trails for compliance...',
      url: 'https://github.com/kbtrotman/hivefs/wiki/Audit-Logs',
      relevance: 87,
      lastUpdated: '2026-01-23T08:30:00Z',
      tags: ['security', 'audit', 'compliance', 'logging'],
    },
    {
      id: '8',
      title: 'Troubleshooting Connection Issues',
      category: 'troubleshooting',
      excerpt:
        'Diagnose and resolve cluster node connectivity problems. Check network configuration, verify firewall rules, test port accessibility, and review daemon logs for connection errors...',
      url: 'https://github.com/kbtrotman/hivefs/wiki/Troubleshooting-Connections',
      relevance: 84,
      lastUpdated: '2026-01-24T15:00:00Z',
      tags: ['troubleshooting', 'network', 'connectivity'],
    },
    {
      id: '9',
      title: 'File Tagging System',
      category: 'features',
      excerpt:
        'Organize files beyond traditional folder structures using hierarchical tags. Create nested tag taxonomies, apply multiple tags per file, and search across tag-based classifications...',
      url: 'https://github.com/kbtrotman/hivefs/wiki/File-Tagging',
      relevance: 86,
      lastUpdated: '2026-01-25T10:15:00Z',
      tags: ['tagging', 'organization', 'metadata'],
    },
    {
      id: '10',
      title: 'Backup and Disaster Recovery',
      category: 'configuration',
      excerpt:
        'Implement comprehensive backup strategies for your HiveFS cluster. Configure automated backups, set retention policies, test recovery procedures, and ensure business continuity...',
      url: 'https://github.com/kbtrotman/hivefs/wiki/Backup-Recovery',
      relevance: 91,
      lastUpdated: '2026-01-16T12:00:00Z',
      tags: ['backup', 'recovery', 'disaster recovery'],
    },
  ];

  const handleSearch = () => {
    if (!searchQuery.trim()) return;

    setIsSearching(true);
    setHasSearched(false);

    // Simulate search delay
    setTimeout(() => {
      // Filter and score results based on search query
      const results = allDocs
        .map((doc) => {
          const query = searchQuery.toLowerCase();
          let score = 0;

          // Title match (highest weight)
          if (doc.title.toLowerCase().includes(query)) score += 50;

          // Excerpt match
          if (doc.excerpt.toLowerCase().includes(query)) score += 30;

          // Tag match
          if (doc.tags.some((tag) => tag.toLowerCase().includes(query))) score += 20;

          // Category filter
          if (selectedCategory !== 'all' && doc.category !== selectedCategory) {
            score = 0;
          }

          return { ...doc, relevance: Math.min(100, doc.relevance + score) };
        })
        .filter((doc) => doc.relevance > 0)
        .sort((a, b) => b.relevance - a.relevance)
        .slice(0, 10);

      setSearchResults(results);
      setIsSearching(false);
      setHasSearched(true);
    }, 600);
  };

  const handleQuickSearch = (query: string) => {
    setSearchQuery(query);
    setTimeout(() => handleSearch(), 100);
  };

  const highlightText = (text: string, query: string) => {
    if (!query.trim()) return text;

    const parts = text.split(new RegExp(`(${query})`, 'gi'));
    return parts.map((part, index) =>
      part.toLowerCase() === query.toLowerCase() ? (
        <mark key={index} className="bg-yellow-300/50 dark:bg-yellow-500/30 rounded px-0.5">
          {part}
        </mark>
      ) : (
        part
      )
    );
  };

  const getCategoryBadge = (category: string) => {
    const cat = categories.find((c) => c.value === category);
    if (!cat) return null;

    return (
      <Badge
        variant="outline"
        className="text-xs"
        style={{ borderColor: cat.color + '60', color: cat.color }}
      >
        {cat.label}
      </Badge>
    );
  };

  const getRelativeTime = (timestamp: string) => {
    const date = new Date(timestamp);
    const now = new Date();
    const diff = now.getTime() - date.getTime();
    const days = Math.floor(diff / 86400000);

    if (days === 0) return 'Today';
    if (days === 1) return 'Yesterday';
    if (days < 7) return `${days} days ago`;
    if (days < 30) return `${Math.floor(days / 7)} weeks ago`;
    return `${Math.floor(days / 30)} months ago`;
  };

  return (
    <div className="flex h-full w-full flex-col gap-6 overflow-auto bg-gradient-to-br from-background via-primary/5 to-background p-8">
      {/* Page Header */}
      <div>
        <h1 className="text-foreground/80">Help & Documentation Search</h1>
        <p className="text-muted-foreground text-sm mt-1">
          Search through HiveFS documentation to find answers and guides
        </p>
      </div>

      {/* Search Bar */}
      <Card className="border-primary/20 bg-gradient-to-b from-background/90 to-background shadow-lg shadow-primary/10">
        <CardContent className="pt-6">
          <div className="space-y-4">
            <div className="relative">
              <Search className="absolute left-3 top-1/2 -translate-y-1/2 w-5 h-5 text-muted-foreground" />
              <Input
                placeholder="Search documentation... (e.g., 'how to restore deleted files')"
                value={searchQuery}
                onChange={(e) => setSearchQuery(e.target.value)}
                onKeyDown={(e) => e.key === 'Enter' && handleSearch()}
                className="pl-10 pr-4 h-12 text-base"
              />
            </div>

            <div className="flex items-center gap-3">
              <div className="flex items-center gap-2">
                <Filter className="w-4 h-4 text-muted-foreground" />
                <Label className="text-xs text-muted-foreground">Filter by:</Label>
              </div>
              <div className="flex flex-wrap gap-2">
                {categories.map((cat) => {
                  const Icon = cat.icon;
                  return (
                    <button
                      key={cat.value}
                      onClick={() => setSelectedCategory(cat.value)}
                      className={`flex items-center gap-1.5 px-3 py-1.5 rounded-full text-xs transition-all ${
                        selectedCategory === cat.value
                          ? 'bg-primary text-primary-foreground'
                          : 'bg-muted hover:bg-muted/80'
                      }`}
                    >
                      <Icon className="w-3 h-3" />
                      {cat.label}
                    </button>
                  );
                })}
              </div>
            </div>

            <Button
              onClick={handleSearch}
              disabled={!searchQuery.trim() || isSearching}
              className="w-full"
              size="lg"
            >
              {isSearching ? (
                <>
                  <Search className="w-4 h-4 mr-2 animate-pulse" />
                  Searching...
                </>
              ) : (
                <>
                  <Search className="w-4 h-4 mr-2" />
                  Search Documentation
                </>
              )}
            </Button>
          </div>
        </CardContent>
      </Card>

      {/* Search Results */}
      {hasSearched && (
        <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
          <CardHeader>
            <div className="flex items-center justify-between">
              <CardTitle className="text-foreground/90 flex items-center gap-2">
                <FileText className="w-5 h-5" />
                Search Results
              </CardTitle>
              <Badge variant="outline">{searchResults.length} result(s)</Badge>
            </div>
          </CardHeader>
          <CardContent>
            {searchResults.length > 0 ? (
              <div className="space-y-3">
                {searchResults.map((result) => (
                  <div
                    key={result.id}
                    className="border rounded-lg p-4 bg-background hover:bg-muted/20 transition-colors"
                  >
                    <div className="flex items-start justify-between mb-2">
                      <div className="flex-1">
                        <div className="flex items-center gap-2 mb-2">
                          <h3 className="text-base font-semibold text-foreground/90">
                            {highlightText(result.title, searchQuery)}
                          </h3>
                          {getCategoryBadge(result.category)}
                          <Badge variant="secondary" className="text-xs">
                            {result.relevance}% match
                          </Badge>
                        </div>
                        <p className="text-sm text-muted-foreground mb-3">
                          {highlightText(result.excerpt, searchQuery)}
                        </p>
                        <div className="flex items-center gap-3 text-xs text-muted-foreground">
                          <div className="flex items-center gap-1">
                            <Clock className="w-3 h-3" />
                            <span>Updated {getRelativeTime(result.lastUpdated)}</span>
                          </div>
                          <span>•</span>
                          <div className="flex items-center gap-1.5 flex-wrap">
                            {result.tags.map((tag) => (
                              <Badge key={tag} variant="outline" className="text-xs">
                                {tag}
                              </Badge>
                            ))}
                          </div>
                        </div>
                      </div>
                    </div>
                    <Separator className="my-3" />
                    <div className="flex items-center justify-between">
                      <a
                        href={result.url}
                        target="_blank"
                        rel="noopener noreferrer"
                        className="text-sm text-primary hover:underline flex items-center gap-1"
                      >
                        View full documentation
                        <ExternalLink className="w-3 h-3" />
                      </a>
                      <Button size="sm" variant="ghost" className="h-7 text-xs">
                        <Bookmark className="w-3 h-3 mr-1" />
                        Bookmark
                      </Button>
                    </div>
                  </div>
                ))}
              </div>
            ) : (
              <div className="text-center py-12 text-muted-foreground">
                <Search className="w-16 h-16 mx-auto mb-4 opacity-30" />
                <p className="text-base font-medium mb-2">No results found</p>
                <p className="text-sm">
                  Try different keywords or browse the quick links below
                </p>
              </div>
            )}
          </CardContent>
        </Card>
      )}

      <div className="grid grid-cols-1 gap-6 lg:grid-cols-2">
        {/* Quick Links */}
        <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
          <CardHeader>
            <CardTitle className="text-foreground/90 flex items-center gap-2">
              <Sparkles className="w-5 h-5" />
              Quick Links
            </CardTitle>
          </CardHeader>
          <CardContent>
            <div className="space-y-2">
              {quickLinks.map((link) => {
                const Icon = link.icon;
                return (
                  <a
                    key={link.id}
                    href={link.url}
                    target="_blank"
                    rel="noopener noreferrer"
                    className="block border rounded-lg p-3 bg-background hover:bg-muted/30 transition-colors group"
                  >
                    <div className="flex items-start gap-3">
                      <div className="p-2 rounded bg-primary/10">
                        <Icon className="w-4 h-4 text-primary" />
                      </div>
                      <div className="flex-1 min-w-0">
                        <div className="flex items-center justify-between mb-1">
                          <p className="text-sm font-medium text-foreground/90 group-hover:text-primary transition-colors">
                            {link.title}
                          </p>
                          <ChevronRight className="w-4 h-4 text-muted-foreground group-hover:translate-x-0.5 transition-transform" />
                        </div>
                        <p className="text-xs text-muted-foreground">{link.description}</p>
                      </div>
                    </div>
                  </a>
                );
              })}
            </div>
          </CardContent>
        </Card>

        {/* Popular & Recent Searches */}
        <div className="space-y-6">
          {/* Popular Searches */}
          <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
            <CardHeader>
              <CardTitle className="text-foreground/90 flex items-center gap-2">
                <TrendingUp className="w-5 h-5" />
                Popular Searches
              </CardTitle>
            </CardHeader>
            <CardContent>
              <div className="flex flex-wrap gap-2">
                {popularSearches.map((search, index) => (
                  <button
                    key={index}
                    onClick={() => handleQuickSearch(search)}
                    className="px-3 py-1.5 rounded-full bg-muted hover:bg-muted/80 text-sm transition-colors flex items-center gap-1.5 group"
                  >
                    <Star className="w-3 h-3 text-yellow-500" />
                    {search}
                    <ArrowRight className="w-3 h-3 opacity-0 group-hover:opacity-100 transition-opacity" />
                  </button>
                ))}
              </div>
            </CardContent>
          </Card>

          {/* Recent Searches */}
          <Card className="border-primary/10 bg-gradient-to-b from-background/80 to-background shadow-lg shadow-primary/10">
            <CardHeader>
              <CardTitle className="text-foreground/90 flex items-center gap-2">
                <Clock className="w-5 h-5" />
                Recent Searches
              </CardTitle>
            </CardHeader>
            <CardContent>
              <div className="space-y-2">
                {recentSearches.map((search, index) => (
                  <button
                    key={index}
                    onClick={() => handleQuickSearch(search)}
                    className="w-full text-left px-3 py-2 rounded bg-muted/30 hover:bg-muted/60 text-sm transition-colors flex items-center justify-between group"
                  >
                    <span className="text-foreground/90">{search}</span>
                    <ArrowRight className="w-4 h-4 text-muted-foreground opacity-0 group-hover:opacity-100 group-hover:translate-x-0.5 transition-all" />
                  </button>
                ))}
                {recentSearches.length === 0 && (
                  <p className="text-xs text-muted-foreground text-center py-4">
                    No recent searches
                  </p>
                )}
              </div>
            </CardContent>
          </Card>
        </div>
      </div>

      {/* Help Footer */}
      <div className="bg-gradient-to-r from-blue-500/10 to-purple-500/10 border border-blue-500/30 rounded-lg p-4">
        <div className="flex items-start gap-3">
          <HelpCircle className="w-5 h-5 text-blue-500 mt-0.5 flex-shrink-0" />
          <div className="flex-1">
            <p className="text-sm font-medium text-foreground/90 mb-2">
              Can't find what you're looking for?
            </p>
            <p className="text-xs text-muted-foreground mb-3">
              If you can't find the answer in our documentation, you can get help from the
              community or contact support directly.
            </p>
            <div className="flex gap-2">
              <Button
                size="sm"
                variant="outline"
                className="text-xs h-7"
                onClick={() =>
                  window.open('https://github.com/kbtrotman/hivefs/discussions', '_blank')
                }
              >
                <Book className="w-3 h-3 mr-1" />
                Community Discussions
                <ExternalLink className="w-3 h-3 ml-1" />
              </Button>
              <Button
                size="sm"
                variant="outline"
                className="text-xs h-7"
                onClick={() =>
                  window.open('https://github.com/kbtrotman/hivefs/issues/new', '_blank')
                }
              >
                <FileText className="w-3 h-3 mr-1" />
                Report an Issue
                <ExternalLink className="w-3 h-3 ml-1" />
              </Button>
            </div>
          </div>
        </div>
      </div>
    </div>
  );
}
