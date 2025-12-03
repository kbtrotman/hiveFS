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
from nodes.views import StorageNodeViewSet

router = DefaultRouter(trailing_slash=False)
#router.register(r'groups', GroupViewSet, basename='group')
router.register(r'tree', TreeNodeViewSet, basename='tree')
router.register(r'settings', SettingsRootView, basename='settings')
router.register(r'audit', AuditRootView, basename='audit')
router.register(r'monitor', MonitorRootView, basename='monitor')
router.register(r'tenant', TenantRootView, basename='tenant')
router.register(r'nodes', StorageNodeViewSet, basename='nodes')


urlpatterns = [
    path("api/v1/", include(router.urls)),
    path("api/v1/accounts/", include("accounts.urls")),
    path('admin/', admin.site.urls),
]
