from rest_framework.routers import SimpleRouter
from .views import TreeNodeViewSet, UiVirtualNodeViewSet, UiHostMapViewSet

router = SimpleRouter()
router.register("tree", TreeNodeViewSet, basename="tree")
router.register("virtual-nodes", UiVirtualNodeViewSet, basename="virtual-node")
router.register("host-maps", UiHostMapViewSet, basename="host-map")

urlpatterns = router.urls
