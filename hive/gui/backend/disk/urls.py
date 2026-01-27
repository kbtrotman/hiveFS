from rest_framework.routers import DefaultRouter
from django.urls import path, include
from disk.views import DiskNodeStatViewSet, DiskNodeViewSet, StorageNodeFsStatsListView, StorageNodeDiskStatsListView

router = DefaultRouter(trailing_slash=False)
router.register("health/capacity", DiskNodeViewSet, basename="disk")
router.register(r'health/stats', DiskNodeStatViewSet, basename='stats')

urlpatterns = router.urls

urlpatterns = [
    path("health/fs", StorageNodeFsStatsListView.as_view(), name="fs-stats"),
    path("health/disks", StorageNodeDiskStatsListView.as_view(), name="disk-stats"),
]
