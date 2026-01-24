from django.urls import include, path
from allauth.account.views import ConfirmEmailView
from dj_rest_auth.registration.views import VerifyEmailView

from .views import SessionStatusView

urlpatterns = [
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
    path(
        "auth/session/",
        SessionStatusView.as_view(),
        name="session-status",
    ),
]
