"""
URL configuration for hive project.

The `urlpatterns` list routes URLs to views. For more information please see:
    https://docs.djangoproject.com/en/5.2/topics/http/urls/
Examples:
Function views
    1. Add an import:  from my_app import views
    2. Add a URL to urlpatterns:  path('', views.home, name='home')
Class-based views
    1. Add an import:  from other_app.views import Home
    2. Add a URL to urlpatterns:  path('', Home.as_view(), name='home')
Including another URLconf
    1. Import the include() function: from django.urls import include, path
    2. Add a URL to urlpatterns:  path('blog/', include('blog.urls'))
"""
from django.contrib import admin
from django.urls import path, include
from rest_framework.routers import DefaultRouter
from tree.views import TreeNodeViewSet
from settings.views import SettingsRootView
from audit.views import AuditRootView
from monitor.views import MonitorRootView
from tenant.views import TenantRootView
from nodes.views import StorageNodeViewSet, StorageNodeStatViewSet, HardwareStatusViewSet
from disk.views import DiskNodeViewSet, DiskNodeStatViewSet, StorageNodeFsStatsListView, StorageNodeDiskStatsListView, DiskStatusListView
from api.views import BootstrapError, BootstrapInitView, BootstrapStatusView, AddNodeView, AddForeignerView, NewTokenView
from accounts.views import (
    GroupMembershipViewSet,
    GroupViewSet,
    RoleAssignmentViewSet,
    RoleViewSet,
    UserViewSet,
    LDAPViewSet,
    SAMLViewSet,
    MFAViewSet,
)
import django_eventstream

router = DefaultRouter(trailing_slash=False)
router.register(r'tree', TreeNodeViewSet, basename='tree')
router.register(r'settings', SettingsRootView, basename='settings')
router.register(r'audit', AuditRootView, basename='audit')
router.register(r'monitor', MonitorRootView, basename='monitor')
router.register(r'tenant', TenantRootView, basename='tenant')
router.register(r'nodes', StorageNodeViewSet, basename='nodes')
router.register(r'snstats', StorageNodeViewSet, basename='snstats')
router.register(r'health/capacity', DiskNodeViewSet, basename='disk')
router.register(r'health/stats', DiskNodeStatViewSet, basename='stats')
router.register(r"health/hw_status", HardwareStatusViewSet, basename="hw_status")
router.register(r"accounts", UserViewSet, basename="account")
router.register(r"groups", GroupViewSet, basename="group")
router.register(r"roles", RoleViewSet, basename="role")
router.register(r"ldap", LDAPViewSet, basename="ldap")
router.register(r"saml", SAMLViewSet, basename="saml")
router.register(r"mfa", MFAViewSet, basename="mfa")


router.register(
    r"group-memberships", GroupMembershipViewSet, basename="group-membership"
)
router.register(
    r"role-assignments", RoleAssignmentViewSet, basename="role-assignment"
)


urlpatterns = [
    path("api/v1/", include(router.urls)),
    path("api/v1/accounts/", include("accounts.urls")),
    path("api/v1/bootstrap/status", BootstrapStatusView.as_view()),
    path("api/v1/bootstrap/init", BootstrapInitView.as_view()),
    path("api/v1/bootstrap/addnode", AddNodeView.as_view()),
    path("api/v1/bootstrap/addforeigner", AddForeignerView.as_view()),
    path("api/v1/new_token", NewTokenView.as_view()),
    path("api/v1/health/fs", StorageNodeFsStatsListView.as_view(), name="fs-stats"),
    path("api/v1/health/disks", StorageNodeDiskStatsListView.as_view(), name="disk-stats"),
    path("api/v1/health/disk_status", DiskStatusListView.as_view(), name="disk-status"),
    path("events/", include(django_eventstream.urls)),
    path('admin/', admin.site.urls),
]
