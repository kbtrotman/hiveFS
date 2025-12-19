from rest_framework.routers import DefaultRouter
from .views import BootstrapStatusView, BootstrapInitView, AddNodeView

router = DefaultRouter(trailing_slash=False)
#router.register(r'api', APIRootView, basename='api')
router.register(r'bootstrap/status', BootstrapStatusView, basename='bootstrap-status')
router.register(r'bootstrap/init', BootstrapInitView, basename='bootstrap-init')
router.register(r'bootstrap/addnode', AddNodeView, basename='bootstrap-addnode')

urlpatterns = router.urls
