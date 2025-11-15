from rest_framework.routers import DefaultRouter
from .views import AuditRootView

router = DefaultRouter(trailing_slash=False)

router.register(r'audit', AuditRootView, basename='audit')

urlpatterns = router.urls
