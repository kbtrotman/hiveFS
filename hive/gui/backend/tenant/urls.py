from rest_framework.routers import DefaultRouter
from .views import TenantRootView

router = DefaultRouter(trailing_slash=False)

router.register(r'tenant', TenantRootView, basename='tenant')

urlpatterns = router.urls