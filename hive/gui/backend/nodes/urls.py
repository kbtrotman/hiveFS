from rest_framework.routers import DefaultRouter
from nodes.views import StorageNodeViewSet

router = DefaultRouter(trailing_slash=False)
router.register("nodes", StorageNodeViewSet, basename="nodes")

urlpatterns = router.urls
