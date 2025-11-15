from rest_framework.routers import DefaultRouter
from .views import SettingsRootView

router = DefaultRouter(trailing_slash=False)
router.register(r'settings', SettingsRootView, basename='settings')

urlpatterns = router.urls