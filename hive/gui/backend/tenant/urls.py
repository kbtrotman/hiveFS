from rest_framework.routers import SimpleRouter
from .views import TenantRootView

router = SimpleRouter()
router.register(r'tenant', TenantRootView, basename='tenant')

urlpatterns = router.urls