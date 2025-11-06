from rest_framework.routers import SimpleRouter
from .views import AuditRootView

router = SimpleRouter()
router.register(r'audit', AuditRootView, basename='audit')

urlpatterns = router.urls

