from rest_framework.routers import SimpleRouter
from .views import APIRootView

router = SimpleRouter()
router.register(r'api', APIRootView, basename='api')

urlpatterns = router.urls
