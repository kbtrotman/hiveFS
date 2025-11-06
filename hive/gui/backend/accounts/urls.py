from rest_framework.routers import SimpleRouter
from .views import AccountsRootView

router = SimpleRouter()
router.register(r'accounts', AccountsRootView, basename='accounts')

urlpatterns = router.urls
