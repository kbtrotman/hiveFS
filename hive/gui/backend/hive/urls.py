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
from tree.views import TreeNodeViewSet, UiHostMapViewSet, UiVirtualNodeViewSet
from settings.views import SettingsRootView
from audit.views import AuditEntryViewSet
from monitor.views import MonitorRootView
from tenant.views import TenantRootView
from nodes.views import StorageNodeViewSet, StorageNodeStatViewSet, HardwareStatusViewSet
from disk.views import DiskNodeViewSet, DiskNodeStatViewSet, StorageNodeFsStatsListView, StorageNodeDiskStatsListView, DiskStatusListView
from notifications.views import AlertViewSet, EmailTargetViewSet, NotificationViewSet, ScheduleViewSet
from api.views import BootstrapInitView, BootstrapStatusView, AddNodeView, AddForeignerView, NewTokenView
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
from django_eventstream import views as eventstream_views
from django.contrib.auth.decorators import login_not_required

router = DefaultRouter(trailing_slash=False)
router.register(r"tree", TreeNodeViewSet, basename="tree")
router.register(r"tree/virtual-nodes", UiVirtualNodeViewSet, basename="tree-virtual")
router.register(r"tree/host-map", UiHostMapViewSet, basename="tree-host-map")

router.register(r"settings", SettingsRootView, basename="settings")
router.register(r"audit", AuditEntryViewSet, basename="audit")
router.register(r"monitor", MonitorRootView, basename="monitor")
router.register(r"tenant", TenantRootView, basename="tenant")

router.register(r"nodes", StorageNodeViewSet, basename="nodes")
router.register(r"health/snstats", StorageNodeStatViewSet, basename="snstats")
router.register(r"health/capacity", DiskNodeViewSet, basename="disk-capacity")
router.register(r"health/stats", DiskNodeStatViewSet, basename="disk-stats")
router.register(r"health/hw_status", HardwareStatusViewSet, basename="hw-status")

router.register(r"accounts", UserViewSet, basename="accounts")
router.register(r"groups", GroupViewSet, basename="groups")
router.register(r"roles", RoleViewSet, basename="roles")
router.register(r"ldap", LDAPViewSet, basename="ldap")
router.register(r"saml", SAMLViewSet, basename="saml")
router.register(r"mfa", MFAViewSet, basename="mfa")
router.register(r"group-memberships", GroupMembershipViewSet, basename="group-memberships")
router.register(r"role-assignments", RoleAssignmentViewSet, basename="role-assignments")

router.register(r"notifications", NotificationViewSet, basename="notifications")
router.register(r"alerts", AlertViewSet, basename="alerts")
router.register(r"notification-schedules", ScheduleViewSet, basename="notification-schedule")
router.register(r"notification-endpoints", EmailTargetViewSet, basename="notification-endpoint")


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
    path("events/", login_not_required(eventstream_views.events)),
    path('admin/', admin.site.urls),
]
