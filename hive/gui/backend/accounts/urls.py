from django.urls import include, path
from rest_framework.routers import DefaultRouter
from allauth.account.views import ConfirmEmailView
from dj_rest_auth.registration.views import VerifyEmailView

from .views import (
    GroupMembershipViewSet,
    GroupViewSet,
    RoleAssignmentViewSet,
    RoleViewSet,
    SessionStatusView,
    UserViewSet,
)

router = DefaultRouter(trailing_slash=False)
router.register(r"users", UserViewSet, basename="users")
router.register(r"groups", GroupViewSet, basename="groups")
router.register(r"group-memberships", GroupMembershipViewSet, basename="group-memberships")
router.register(r"roles", RoleViewSet, basename="roles")
router.register(r"role-assignments", RoleAssignmentViewSet, basename="role-assignments")

urlpatterns = [
    path("", include(router.urls)),
    path("auth/", include("dj_rest_auth.urls")),
    path("auth/registration/", include("dj_rest_auth.registration.urls")),
    path(
        "auth/registration/account-confirm-email/<str:key>/",
        ConfirmEmailView.as_view(),
    ),
    path(
        "auth/account-confirm-email/",
        VerifyEmailView.as_view(),
        name="account_email_verification_sent",
    ),
    path("auth/session/", SessionStatusView.as_view(), name="session-status"),
]
