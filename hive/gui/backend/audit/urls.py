from rest_framework.routers import DefaultRouter

from .views import AuditEntryViewSet

router = DefaultRouter(trailing_slash=False)
router.register(r"audit", AuditEntryViewSet, basename="audit")

urlpatterns = router.urls
