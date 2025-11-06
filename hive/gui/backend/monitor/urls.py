from rest_framework.routers import SimpleRouter
from .views import MonitorRootView

router = SimpleRouter()
router.register(r'monitor', MonitorRootView, basename='monitor')

urlpatterns = router.urls