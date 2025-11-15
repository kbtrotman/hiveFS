from rest_framework.routers import DefaultRouter
from .views import MonitorRootView

router = DefaultRouter(trailing_slash=False)

router.register(r'monitor', MonitorRootView, basename='monitor')

urlpatterns = router.urls