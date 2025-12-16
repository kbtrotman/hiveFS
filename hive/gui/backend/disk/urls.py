from rest_framework.routers import DefaultRouter
from disk.views import DiskNodeStatViewSet, DiskNodeViewSet

router = DefaultRouter(trailing_slash=False)
router.register("disk", DiskNodeViewSet, basename="disk")
router.register(r'dstats', DiskNodeStatViewSet, basename='dstats')

urlpatterns = router.urls
