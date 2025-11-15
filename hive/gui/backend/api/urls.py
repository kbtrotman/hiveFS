from rest_framework.routers import DefaultRouter
from .views import APIRootView

router = DefaultRouter(trailing_slash=False)
router.register(r'api', APIRootView, basename='api')

urlpatterns = router.urls
