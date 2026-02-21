from rest_framework.routers import DefaultRouter
from nodes.views import HardwareStatusViewSet, StorageNodeStatViewSet, StorageNodeViewSet

router = DefaultRouter(trailing_slash=False)
router.register("nodes", StorageNodeViewSet, basename="nodes")
router.register(r"snstats", StorageNodeStatViewSet, basename="snstats")
router.register(r"nodes/hardware", HardwareStatusViewSet, basename="nodes-hardware")

urlpatterns = router.urls
