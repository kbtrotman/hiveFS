import { useEffect, useState } from 'react';
import { Plus, RefreshCw, Trash2 } from 'lucide-react';
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from './ui/card';
import { Input } from './ui/input';
import { Button } from './ui/button';
import { Checkbox } from './ui/checkbox';
import { Textarea } from './ui/textarea';
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from './ui/select';
import { Tabs, TabsContent, TabsList, TabsTrigger } from './ui/tabs';
import { Dialog, DialogContent, DialogDescription, DialogHeader, DialogTitle, DialogTrigger } from './ui/dialog';
import { Table, TableBody, TableCell, TableHead, TableHeader, TableRow } from './ui/table';
import { Badge } from './ui/badge';

type TabKey = 'users' | 'groups' | 'roles';

type AccessLevel = 'read' | 'write' | 'read_write' | 'admin';

type Permission = {
  id: string;
  view: string;
  access: AccessLevel;
};

type Role = {
  id: string;
  name: string;
  description: string;
  permissions: Permission[];
};

type Group = {
  id: string;
  name: string;
  description: string;
  roles: string[];
  members: number;
};

type User = {
  id: string;
  username: string;
  email: string;
  groups: string[];
  role?: string;
  status?: string;
};

type CollectionMap = {
  users: User[];
  groups: Group[];
  roles: Role[];
};

type DraftState = {
  users: Record<string, User>;
  groups: Record<string, Group>;
  roles: Record<string, Role>;
};

type PermissionsSetupScreenProps = {
  triggerLabel?: string;
  defaultOpen?: boolean;
};

const API_BASE_URL = 'http://localhost:8000/api/v1';
const API_ENDPOINTS: Record<TabKey, string> = {
  users: 'accounts',
  groups: 'groups',
  roles: 'roles',
};

const ACCESS_LEVELS: { label: string; value: AccessLevel }[] = [
  { label: 'Read', value: 'read' },
  { label: 'Write', value: 'write' },
  { label: 'Read & Write', value: 'read_write' },
  { label: 'Admin', value: 'admin' },
];

const PERMISSION_EXAMPLES = [
  'Dashboard',
  'Storage Nodes',
  'Performance Charts',
  'Replication Monitor',
  'Audit Logs',
  'Clients Pane',
  'Capacity Planner',
];

const FALLBACK_DATA: CollectionMap = {
  users: [
    {
      id: 'user-admin',
      username: 'admin',
      email: 'admin@hive.local',
      groups: ['Operations'],
      role: 'Administrator',
      status: 'active',
    },
    {
      id: 'user-ops',
      username: 'ops1',
      email: 'ops1@hive.local',
      groups: ['Operations', 'Support'],
      role: 'Operator',
      status: 'active',
    },
    {
      id: 'user-analytics',
      username: 'analytics',
      email: 'analytics@hive.local',
      groups: ['Analytics'],
      role: 'Observer',
      status: 'suspended',
    },
  ],
  groups: [
    {
      id: 'group-ops',
      name: 'Operations',
      description: 'Core management for HiveFS nodes and services.',
      roles: ['Administrator'],
      members: 8,
    },
    {
      id: 'group-support',
      name: 'Support',
      description: 'Limited write access for customer service.',
      roles: ['Operator'],
      members: 12,
    },
    {
      id: 'group-analytics',
      name: 'Analytics',
      description: 'Read-only access to dashboards and logs.',
      roles: ['Observer'],
      members: 5,
    },
  ],
  roles: [
    {
      id: 'role-admin',
      name: 'Administrator',
      description: 'Full control of HiveFS configuration.',
      permissions: [
        { id: 'perm-admin-dashboard', view: 'Dashboard', access: 'read_write' },
        { id: 'perm-admin-storage', view: 'Storage Nodes', access: 'read_write' },
        { id: 'perm-admin-performance', view: 'Performance Charts', access: 'read_write' },
      ],
    },
    {
      id: 'role-operator',
      name: 'Operator',
      description: 'Day-to-day node operations.',
      permissions: [
        { id: 'perm-ops-storage', view: 'Storage Nodes', access: 'read_write' },
        { id: 'perm-ops-performance', view: 'Performance Charts', access: 'read' },
        { id: 'perm-ops-clients', view: 'Clients Pane', access: 'read' },
      ],
    },
    {
      id: 'role-observer',
      name: 'Observer',
      description: 'Read-only monitoring role.',
      permissions: [
        { id: 'perm-observer-dashboard', view: 'Dashboard', access: 'read' },
        { id: 'perm-observer-logs', view: 'Audit Logs', access: 'read' },
      ],
    },
  ],
};

const safeRandomId = (prefix: string) => {
  const random =
    typeof crypto !== 'undefined' && typeof crypto.randomUUID === 'function'
      ? crypto.randomUUID()
      : `${Date.now()}-${Math.random().toString(16).slice(2)}`;
  return `${prefix}-${random}`;
};

const deepClone = <T,>(value: T): T => JSON.parse(JSON.stringify(value));

const createEmptyUser = (): User => ({
  id: safeRandomId('local-user'),
  username: '',
  email: '',
  groups: [],
  role: '',
  status: 'inactive',
});

const createEmptyGroup = (): Group => ({
  id: safeRandomId('local-group'),
  name: '',
  description: '',
  roles: [],
  members: 0,
});

const createEmptyRole = (): Role => ({
  id: safeRandomId('local-role'),
  name: '',
  description: '',
  permissions: [],
});

const createEmptyPermission = (): Permission => ({
  id: safeRandomId('local-perm'),
  view: '',
  access: 'read',
});

const normalizeUser = (payload: Partial<User>): User => ({
  ...createEmptyUser(),
  ...payload,
  id: payload.id ?? payload.username ?? safeRandomId('user'),
  username: payload.username ?? payload['name'] ?? 'user',
  email: payload.email ?? `${payload.username ?? 'user'}@hive.local`,
  groups: Array.isArray(payload.groups) ? payload.groups : [],
  role: payload.role ?? payload['roles']?.[0] ?? 'Observer',
  status: payload.status ?? 'active',
});

const normalizeGroup = (payload: Partial<Group>): Group => ({
  ...createEmptyGroup(),
  ...payload,
  id: payload.id ?? payload.name ?? safeRandomId('group'),
  name: payload.name ?? 'Unnamed Group',
  description: payload.description ?? '',
  roles: Array.isArray(payload.roles) ? payload.roles : [],
  members: typeof payload.members === 'number' ? payload.members : 0,
});

const normalizePermission = (payload: Partial<Permission>): Permission => ({
  ...createEmptyPermission(),
  ...payload,
  id: payload.id ?? safeRandomId('perm'),
  view: payload.view ?? payload['resource'] ?? 'Dashboard',
  access: (payload.access as AccessLevel) ?? 'read',
});

const normalizeRole = (payload: Partial<Role>): Role => ({
  ...createEmptyRole(),
  ...payload,
  id: payload.id ?? payload.name ?? safeRandomId('role'),
  name: payload.name ?? 'Unnamed Role',
  description: payload.description ?? '',
  permissions: Array.isArray(payload.permissions)
    ? payload.permissions.map((perm) => normalizePermission(perm))
    : [],
});

type EntityForTab<T extends TabKey> = CollectionMap[T][number];

export function PermissionsSetupScreen({
  triggerLabel = 'Manage Permissions',
  defaultOpen = false,
}: PermissionsSetupScreenProps) {
  const [open, setOpen] = useState(defaultOpen);
  const [activeTab, setActiveTab] = useState<TabKey>('users');
  const [users, setUsers] = useState<User[]>(FALLBACK_DATA.users);
  const [groups, setGroups] = useState<Group[]>(FALLBACK_DATA.groups);
  const [roles, setRoles] = useState<Role[]>(FALLBACK_DATA.roles);
  const [loading, setLoading] = useState<Record<TabKey, boolean>>({
    users: false,
    groups: false,
    roles: false,
  });
  const [statusMessage, setStatusMessage] = useState<string | null>(null);
  const [errorMessage, setErrorMessage] = useState<string | null>(null);
  const [editingRows, setEditingRows] = useState<Record<TabKey, Set<string>>>(() => ({
    users: new Set(),
    groups: new Set(),
    roles: new Set(),
  }));
  const [drafts, setDrafts] = useState<DraftState>({
    users: {},
    groups: {},
    roles: {},
  });

  const setterMap: { [K in TabKey]: React.Dispatch<React.SetStateAction<CollectionMap[K]>> } = {
    users: setUsers,
    groups: setGroups,
    roles: setRoles,
  };

const collectionMap: CollectionMap = {
  users,
  groups,
  roles,
};

const isLocalEntityId = (id: string) =>
  id.startsWith('local-user') || id.startsWith('local-group') || id.startsWith('local-role');

const setStatus = (message: string | null) => {
  setStatusMessage(message);
  if (message) {
    setTimeout(() => setStatusMessage(null), 3500);
    }
  };

  const updateLoading = (key: TabKey, isLoading: boolean) => {
    setLoading((prev) => ({ ...prev, [key]: isLoading }));
  };

  const getEntityById = <K extends TabKey>(tab: K, id: string): EntityForTab<K> | undefined => {
    return collectionMap[tab].find((entry) => entry.id === id) as EntityForTab<K> | undefined;
  };

  const patchCollection = <K extends TabKey>(
    tab: K,
    updater: (prev: CollectionMap[K]) => CollectionMap[K],
  ) => {
    setterMap[tab]((prev) => updater(prev));
  };

const finishEditing = (tab: TabKey, id: string) => {
  setEditingRows((prev) => {
    const next: Record<TabKey, Set<string>> = {
      users: new Set(prev.users),
      groups: new Set(prev.groups),
      roles: new Set(prev.roles),
    };
    next[tab].delete(id);
    return next;
  });
  setDrafts((prev) => {
    const nextTabDrafts = { ...prev[tab] };
    delete nextTabDrafts[id];
    return {
      ...prev,
      [tab]: nextTabDrafts,
    };
  });
};

const removeEntity = (tab: TabKey, id: string) => {
  patchCollection(tab, (prev) => prev.filter((item) => item.id !== id));
};

const beginEditing = <K extends TabKey>(tab: K, entity: EntityForTab<K>) => {
  setEditingRows((prev) => {
    const next: Record<TabKey, Set<string>> = {
      users: new Set(prev.users),
      groups: new Set(prev.groups),
      roles: new Set(prev.roles),
    };
    next[tab].add(entity.id);
    return next;
  });
  setDrafts((prev) => ({
    ...prev,
    [tab]: {
      ...prev[tab],
      [entity.id]: deepClone(entity),
    },
  }));
};

const cancelEditing = <K extends TabKey>(tab: K, entity: EntityForTab<K>) => {
  finishEditing(tab, entity.id);
  if (isLocalEntityId(entity.id)) {
    removeEntity(tab, entity.id);
  }
};

const toggleEditing = <K extends TabKey>(
  tab: K,
  entity: EntityForTab<K>,
  checked: boolean | 'indeterminate',
) => {
  if (checked === true) {
    beginEditing(tab, entity);
  } else {
    cancelEditing(tab, entity);
  }
};

  const handleDraftChange = <K extends TabKey>(
    tab: K,
    id: string,
    patch: Partial<EntityForTab<K>>,
  ) => {
    setDrafts((prev) => {
      const current = prev[tab][id] ?? getEntityById(tab, id);
      if (!current) {
        return prev;
      }
      return {
        ...prev,
        [tab]: {
          ...prev[tab],
          [id]: {
            ...current,
            ...patch,
          },
        },
      };
    });
  };

  const mergeSavedEntity = <K extends TabKey>(tab: K, entity: EntityForTab<K>) => {
    patchCollection(tab, (prev) => {
      const others = prev.filter((item) => item.id !== entity.id);
      return [entity, ...others];
    });
    finishEditing(tab, entity.id);
  };

  const request = async (path: string, init: RequestInit = {}) => {
    const token = localStorage.getItem('authToken');
    const headers = new Headers(init.headers ?? undefined);
    if (token && !headers.has('Authorization')) {
      headers.set('Authorization', `Token ${token}`);
    }
    const response = await fetch(path, { ...init, headers });
    if (!response.ok) {
      throw new Error(`${response.status} ${response.statusText}`);
    }
    return response;
  };

  const loadEntities = async (tab: TabKey) => {
    updateLoading(tab, true);
    try {
      const endpoint = API_ENDPOINTS[tab];
      const response = await request(`${API_BASE_URL}/${endpoint}`);
      const payload = await response.json();
      const list = Array.isArray(payload) ? payload : payload?.results ?? [];
      if (tab === 'users') {
        setUsers(list.map((entry: Partial<User>) => normalizeUser(entry)));
      } else if (tab === 'groups') {
        setGroups(list.map((entry: Partial<Group>) => normalizeGroup(entry)));
      } else {
        setRoles(list.map((entry: Partial<Role>) => normalizeRole(entry)));
      }
    } catch (err) {
      console.warn(`Failed to load ${tab}:`, err);
      setErrorMessage(`Unable to load ${tab}. Using cached data.`);
      setTimeout(() => setErrorMessage(null), 4000);
    } finally {
      updateLoading(tab, false);
    }
  };

const addEntity = (tab: TabKey) => {
  if (tab === 'users') {
    const fresh = createEmptyUser();
    setUsers((prev) => [fresh, ...prev]);
    beginEditing('users', fresh);
  } else if (tab === 'groups') {
    const fresh = createEmptyGroup();
    setGroups((prev) => [fresh, ...prev]);
    beginEditing('groups', fresh);
  } else {
    const fresh = createEmptyRole();
    setRoles((prev) => [fresh, ...prev]);
    beginEditing('roles', fresh);
  }
  setStatus(`New ${tab.slice(0, -1)} ready to edit.`);
};

  const saveEntity = async <K extends TabKey>(tab: K, draftId: string) => {
    const draft = drafts[tab][draftId] as EntityForTab<K> | undefined;
    if (!draft) return;
  const isNew = isLocalEntityId(draft.id);
    const endpoint = API_ENDPOINTS[tab];
    const url = `${API_BASE_URL}/${endpoint}${isNew ? '' : `/${draft.id}`}`;
    try {
      setStatus('Saving changes…');
      const response = await request(url, {
        method: isNew ? 'POST' : 'PUT',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(draft),
      });
      const payload = await response.json();
      const normalized =
        tab === 'users'
          ? normalizeUser(payload)
          : tab === 'groups'
            ? normalizeGroup(payload)
            : normalizeRole(payload);
      mergeSavedEntity(tab, normalized as EntityForTab<K>);
      setStatus('Changes saved.');
    } catch (err) {
      console.error(`Failed to save ${tab}`, err);
      setErrorMessage(`Unable to save ${tab.slice(0, -1)}.`);
      setTimeout(() => setErrorMessage(null), 4000);
    }
  };

  const deleteEntity = async (tab: TabKey, entity: EntityForTab<TabKey>) => {
    const endpoint = API_ENDPOINTS[tab];
    const url = `${API_BASE_URL}/${endpoint}/${entity.id}`;
    try {
      if (!isLocalEntityId(entity.id)) {
        await request(url, { method: 'DELETE' });
      }
      removeEntity(tab, entity.id);
      finishEditing(tab, entity.id);
      setStatus(`${tab.slice(0, -1)} deleted.`);
    } catch (err) {
      console.error(`Failed to delete ${tab}`, err);
      setErrorMessage(`Unable to delete ${tab.slice(0, -1)}.`);
      setTimeout(() => setErrorMessage(null), 4000);
    }
  };

  useEffect(() => {
    loadEntities('users');
    loadEntities('groups');
    loadEntities('roles');
  }, []);

  const groupNames = groups.map((group) => group.name);
  const roleNames = roles.map((role) => role.name);

  const renderGroupBadges = (items: string[]) => {
    if (!items.length) {
      return <span className="text-xs text-muted-foreground">None</span>;
    }
    return (
      <div className="flex flex-wrap gap-1">
        {items.map((item) => (
          <Badge key={item} variant="outline">
            {item}
          </Badge>
        ))}
      </div>
    );
  };

  const renderUsersTab = () => (
    <Card>
      <CardHeader className="flex flex-row items-center justify-between">
        <div>
          <CardTitle>Users</CardTitle>
          <CardDescription>Accounts that can sign into HiveFS.</CardDescription>
        </div>
        <div className="flex gap-2">
          <Button variant="outline" size="icon" onClick={() => loadEntities('users')} disabled={loading.users}>
            <RefreshCw className={`size-4 ${loading.users ? 'animate-spin' : ''}`} />
          </Button>
          <Button size="sm" onClick={() => addEntity('users')}>
            <Plus className="mr-1 size-4" /> Add User
          </Button>
        </div>
      </CardHeader>
      <CardContent>
        <Table>
          <TableHeader>
            <TableRow>
              <TableHead className="w-12">Edit</TableHead>
              <TableHead>Username</TableHead>
              <TableHead>Email</TableHead>
              <TableHead>Groups</TableHead>
              <TableHead>Role</TableHead>
              <TableHead className="text-right">Actions</TableHead>
            </TableRow>
          </TableHeader>
          <TableBody>
            {users.map((user) => {
              const editing = editingRows.users.has(user.id);
              const display = (editing ? drafts.users[user.id] : user) ?? user;
              return (
                <TableRow key={user.id}>
                  <TableCell>
                    <Checkbox
                      checked={editing}
                      onCheckedChange={(checked) => toggleEditing('users', user, checked)}
                    />
                  </TableCell>
                  <TableCell>
                    {editing ? (
                      <Input
                        value={display.username}
                        placeholder="username"
                        onChange={(event) =>
                          handleDraftChange('users', user.id, { username: event.target.value })
                        }
                      />
                    ) : (
                      <div>
                        <p className="font-medium">{display.username}</p>
                        <p className="text-xs text-muted-foreground capitalize">{display.status}</p>
                      </div>
                    )}
                  </TableCell>
                  <TableCell>
                    {editing ? (
                      <Input
                        value={display.email}
                        placeholder="user@domain"
                        onChange={(event) =>
                          handleDraftChange('users', user.id, { email: event.target.value })
                        }
                      />
                    ) : (
                      display.email
                    )}
                  </TableCell>
                  <TableCell>
                    {editing ? (
                      <CheckboxMultiSelect
                        options={groupNames}
                        value={display.groups}
                        onChange={(next) => handleDraftChange('users', user.id, { groups: next })}
                        placeholder="Assign groups"
                      />
                    ) : (
                      renderGroupBadges(display.groups)
                    )}
                  </TableCell>
                  <TableCell>
                    {editing ? (
                      <Select
                        value={display.role ?? ''}
                        onValueChange={(value) => handleDraftChange('users', user.id, { role: value })}
                      >
                        <SelectTrigger>
                          <SelectValue placeholder="Select role" />
                        </SelectTrigger>
                        <SelectContent>
                          <SelectItem value="">Unassigned</SelectItem>
                          {roleNames.map((role) => (
                            <SelectItem key={role} value={role}>
                              {role}
                            </SelectItem>
                          ))}
                        </SelectContent>
                      </Select>
                    ) : display.role ? (
                      <Badge variant="secondary">{display.role}</Badge>
                    ) : (
                      <span className="text-xs text-muted-foreground">Unassigned</span>
                    )}
                  </TableCell>
                  <TableCell className="text-right">
                    {editing ? (
                      <div className="flex justify-end gap-2">
                        <Button size="sm" onClick={() => saveEntity('users', user.id)}>
                          Save
                        </Button>
                        <Button variant="ghost" size="sm" onClick={() => cancelEditing('users', user)}>
                          Cancel
                        </Button>
                      </div>
                    ) : (
                      <Button variant="ghost" size="icon" onClick={() => deleteEntity('users', user)}>
                        <Trash2 className="size-4" />
                      </Button>
                    )}
                  </TableCell>
                </TableRow>
              );
            })}
          </TableBody>
        </Table>
      </CardContent>
    </Card>
  );

  const renderGroupsTab = () => (
    <Card>
      <CardHeader className="flex flex-row items-center justify-between">
        <div>
          <CardTitle>Groups</CardTitle>
          <CardDescription>Collections of users with shared responsibilities.</CardDescription>
        </div>
        <div className="flex gap-2">
          <Button variant="outline" size="icon" onClick={() => loadEntities('groups')} disabled={loading.groups}>
            <RefreshCw className={`size-4 ${loading.groups ? 'animate-spin' : ''}`} />
          </Button>
          <Button size="sm" onClick={() => addEntity('groups')}>
            <Plus className="mr-1 size-4" /> Add Group
          </Button>
        </div>
      </CardHeader>
      <CardContent>
        <Table>
          <TableHeader>
            <TableRow>
              <TableHead className="w-12">Edit</TableHead>
              <TableHead>Name</TableHead>
              <TableHead>Description</TableHead>
              <TableHead>Roles</TableHead>
              <TableHead>Members</TableHead>
              <TableHead className="text-right">Actions</TableHead>
            </TableRow>
          </TableHeader>
          <TableBody>
            {groups.map((group) => {
              const editing = editingRows.groups.has(group.id);
              const display = (editing ? drafts.groups[group.id] : group) ?? group;
              return (
                <TableRow key={group.id}>
                  <TableCell>
                    <Checkbox
                      checked={editing}
                      onCheckedChange={(checked) => toggleEditing('groups', group, checked)}
                    />
                  </TableCell>
                  <TableCell>
                    {editing ? (
                      <Input
                        value={display.name}
                        placeholder="Group name"
                        onChange={(event) =>
                          handleDraftChange('groups', group.id, { name: event.target.value })
                        }
                      />
                    ) : (
                      <p className="font-medium">{display.name}</p>
                    )}
                  </TableCell>
                  <TableCell>
                    {editing ? (
                      <Textarea
                        value={display.description}
                        placeholder="Describe responsibilities"
                        rows={2}
                        onChange={(event) =>
                          handleDraftChange('groups', group.id, { description: event.target.value })
                        }
                      />
                    ) : (
                      <span className="text-sm text-muted-foreground">{display.description}</span>
                    )}
                  </TableCell>
                  <TableCell>
                    {editing ? (
                      <CheckboxMultiSelect
                        options={roleNames}
                        value={display.roles}
                        onChange={(next) => handleDraftChange('groups', group.id, { roles: next })}
                        placeholder="Assign roles"
                      />
                    ) : (
                      renderGroupBadges(display.roles)
                    )}
                  </TableCell>
                  <TableCell className="font-mono text-sm">{display.members}</TableCell>
                  <TableCell className="text-right">
                    {editing ? (
                      <div className="flex justify-end gap-2">
                        <Button size="sm" onClick={() => saveEntity('groups', group.id)}>
                          Save
                        </Button>
                        <Button variant="ghost" size="sm" onClick={() => cancelEditing('groups', group)}>
                          Cancel
                        </Button>
                      </div>
                    ) : (
                      <Button variant="ghost" size="icon" onClick={() => deleteEntity('groups', group)}>
                        <Trash2 className="size-4" />
                      </Button>
                    )}
                  </TableCell>
                </TableRow>
              );
            })}
          </TableBody>
        </Table>
      </CardContent>
    </Card>
  );

  const renderRolesTab = () => (
    <Card>
      <CardHeader className="flex flex-row items-center justify-between">
        <div>
          <CardTitle>Roles</CardTitle>
          <CardDescription>Define permissions for UI areas.</CardDescription>
        </div>
        <div className="flex gap-2">
          <Button variant="outline" size="icon" onClick={() => loadEntities('roles')} disabled={loading.roles}>
            <RefreshCw className={`size-4 ${loading.roles ? 'animate-spin' : ''}`} />
          </Button>
          <Button size="sm" onClick={() => addEntity('roles')}>
            <Plus className="mr-1 size-4" /> Add Role
          </Button>
        </div>
      </CardHeader>
      <CardContent>
        <Table>
          <TableHeader>
            <TableRow>
              <TableHead className="w-12">Edit</TableHead>
              <TableHead>Name</TableHead>
              <TableHead>Description</TableHead>
              <TableHead>Permissions</TableHead>
              <TableHead className="text-right">Actions</TableHead>
            </TableRow>
          </TableHeader>
          <TableBody>
            {roles.map((role) => {
              const editing = editingRows.roles.has(role.id);
              const display = (editing ? drafts.roles[role.id] : role) ?? role;
              return (
                <TableRow key={role.id}>
                  <TableCell>
                    <Checkbox
                      checked={editing}
                      onCheckedChange={(checked) => toggleEditing('roles', role, checked)}
                    />
                  </TableCell>
                  <TableCell>
                    {editing ? (
                      <Input
                        value={display.name}
                        placeholder="Role name"
                        onChange={(event) =>
                          handleDraftChange('roles', role.id, { name: event.target.value })
                        }
                      />
                    ) : (
                      <p className="font-medium">{display.name}</p>
                    )}
                  </TableCell>
                  <TableCell>
                    {editing ? (
                      <Textarea
                        value={display.description}
                        placeholder="Describe scope"
                        rows={2}
                        onChange={(event) =>
                          handleDraftChange('roles', role.id, { description: event.target.value })
                        }
                      />
                    ) : (
                      <span className="text-sm text-muted-foreground">{display.description}</span>
                    )}
                  </TableCell>
                  <TableCell>
                    {editing ? (
                      <PermissionEditor
                        value={display.permissions}
                        onChange={(next) => handleDraftChange('roles', role.id, { permissions: next })}
                      />
                    ) : (
                      <div className="space-y-1">
                        {display.permissions.map((permission) => (
                          <div key={permission.id} className="flex items-center gap-2 text-sm">
                            <Badge variant="outline">{permission.view}</Badge>
                            <span className="text-xs uppercase tracking-wide text-muted-foreground">
                              {permission.access.replace('_', ' ')}
                            </span>
                          </div>
                        ))}
                        {!display.permissions.length && (
                          <span className="text-xs text-muted-foreground">No permissions defined.</span>
                        )}
                      </div>
                    )}
                  </TableCell>
                  <TableCell className="text-right">
                    {editing ? (
                      <div className="flex justify-end gap-2">
                        <Button size="sm" onClick={() => saveEntity('roles', role.id)}>
                          Save
                        </Button>
                        <Button variant="ghost" size="sm" onClick={() => cancelEditing('roles', role)}>
                          Cancel
                        </Button>
                      </div>
                    ) : (
                      <Button variant="ghost" size="icon" onClick={() => deleteEntity('roles', role)}>
                        <Trash2 className="size-4" />
                      </Button>
                    )}
                  </TableCell>
                </TableRow>
              );
            })}
          </TableBody>
        </Table>
      </CardContent>
    </Card>
  );

  return (
    <Dialog open={open} onOpenChange={setOpen}>
      <DialogTrigger asChild>
        <Button variant="outline">{triggerLabel}</Button>
      </DialogTrigger>
      <DialogContent className="max-w-6xl">
        <DialogHeader>
          <DialogTitle>Permission Setup</DialogTitle>
          <DialogDescription>
            Manage user accounts, group membership, and role permissions.
          </DialogDescription>
        </DialogHeader>
        <Tabs value={activeTab} onValueChange={(value) => setActiveTab(value as TabKey)}>
          <TabsList>
            <TabsTrigger value="users">Users</TabsTrigger>
            <TabsTrigger value="groups">Groups</TabsTrigger>
            <TabsTrigger value="roles">Roles</TabsTrigger>
          </TabsList>
          {statusMessage && <p className="text-xs text-emerald-500">{statusMessage}</p>}
          {errorMessage && <p className="text-xs text-destructive">{errorMessage}</p>}
          <TabsContent value="users">{renderUsersTab()}</TabsContent>
          <TabsContent value="groups">{renderGroupsTab()}</TabsContent>
          <TabsContent value="roles">{renderRolesTab()}</TabsContent>
        </Tabs>
      </DialogContent>
    </Dialog>
  );
}

type CheckboxMultiSelectProps = {
  options: string[];
  value: string[];
  placeholder?: string;
  onChange: (next: string[]) => void;
};

function CheckboxMultiSelect({ options, value, placeholder, onChange }: CheckboxMultiSelectProps) {
  const toggle = (option: string, checked: boolean | 'indeterminate') => {
    const nextChecked = checked === true;
    if (nextChecked) {
      onChange([...new Set([...value, option])]);
    } else {
      onChange(value.filter((entry) => entry !== option));
    }
  };

  if (!options.length) {
    return <span className="text-xs text-muted-foreground">No options</span>;
  }

  return (
    <div className="flex flex-wrap gap-2">
      {options.map((option) => (
        <label key={option} className="flex items-center gap-1 rounded border border-border px-2 py-1 text-xs">
          <Checkbox checked={value.includes(option)} onCheckedChange={(checked) => toggle(option, checked)} />
          <span>{option}</span>
        </label>
      ))}
      {!value.length && placeholder && (
        <span className="text-xs text-muted-foreground">{placeholder}</span>
      )}
    </div>
  );
}

type PermissionEditorProps = {
  value: Permission[];
  onChange: (next: Permission[]) => void;
};

function PermissionEditor({ value, onChange }: PermissionEditorProps) {
  const updatePermission = (id: string, patch: Partial<Permission>) => {
    onChange(value.map((perm) => (perm.id === id ? { ...perm, ...patch } : perm)));
  };

  const removePermission = (id: string) => {
    onChange(value.filter((perm) => perm.id !== id));
  };

  return (
    <div className="space-y-2">
      {value.map((permission) => (
        <div key={permission.id} className="grid grid-cols-[1fr_1fr_auto] items-center gap-2">
          <Input
            value={permission.view}
            placeholder="View name"
            onChange={(event) => updatePermission(permission.id, { view: event.target.value })}
          />
          <Select
            value={permission.access}
            onValueChange={(next) => updatePermission(permission.id, { access: next as AccessLevel })}
          >
            <SelectTrigger>
              <SelectValue placeholder="Access" />
            </SelectTrigger>
            <SelectContent>
              {ACCESS_LEVELS.map((option) => (
                <SelectItem key={option.value} value={option.value}>
                  {option.label}
                </SelectItem>
              ))}
            </SelectContent>
          </Select>
          <Button variant="ghost" size="icon" onClick={() => removePermission(permission.id)}>
            <Trash2 className="size-4" />
          </Button>
        </div>
      ))}
      <Button variant="outline" size="sm" onClick={() => onChange([...value, createEmptyPermission()])}>
        <Plus className="mr-1 size-4" /> Add Permission
      </Button>
    </div>
  );
}
