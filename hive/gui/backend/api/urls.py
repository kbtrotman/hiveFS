from rest_framework.routers import DefaultRouter
from .views import BootstrapStatusView, BootstrapInitView, AddNodeView, AddForeignerView, NewTokenView

router = DefaultRouter(trailing_slash=False)
#router.register(r'api', APIRootView, basename='api')

urlpatterns = router.urls

urlpatterns += [
    path("api/v1/bootstrap/status", BootstrapStatusView.as_view()),
    path("api/v1/bootstrap/init", BootstrapInitView.as_view()),
    path("api/v1/bootstrap/addnode", AddNodeView.as_view()),
    path("api/v1/bootstrap/addforeigner", AddForeignerView.as_view()),
    path("api/v1/new_token", NewTokenView.as_view()),
]


