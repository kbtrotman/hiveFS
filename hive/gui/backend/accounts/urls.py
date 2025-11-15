from rest_framework.routers import DefaultRouter
from .views import AccountsRootView

router = DefaultRouter(trailing_slash=False)
router.register(r'accounts', AccountsRootView, basename='accounts')

urlpatterns = router.urls
