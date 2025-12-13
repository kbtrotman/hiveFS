from rest_framework.routers import DefaultRouter
from nodes.views import StorageNodeViewSet,StorageNodeStatViewSet

router = DefaultRouter(trailing_slash=False)
router.register("nodes", StorageNodeViewSet, basename="nodes")
router.register(r'snstats', StorageNodeStatViewSet, basename='snstats')

urlpatterns = router.urls
