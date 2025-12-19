from rest_framework.routers import DefaultRouter
from .views import BootstrapStatusView, BootstrapInitView, AddNodeView

router = DefaultRouter(trailing_slash=False)
#router.register(r'api', APIRootView, basename='api')

urlpatterns = router.urls

urlpatterns += [
    path("api/v1/bootstrap/status", BootstrapStatusView.as_view()),
    path("api/v1/bootstrap/init", BootstrapInitView.as_view()),
    path("api/v1/bootstrap/addnode", AddNodeView.as_view()),
]


