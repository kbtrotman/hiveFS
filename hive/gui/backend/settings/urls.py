from rest_framework.routers import SimpleRouter
from .views import SettingsRootView

router = SimpleRouter()
router.register(r'settings', SettingsRootView, basename='settings')

urlpatterns = router.urls