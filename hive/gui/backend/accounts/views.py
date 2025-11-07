from rest_framework.response import Response
from rest_framework.viewsets import ViewSet
from rest_framework.decorators import action

class AccountsRootView(ViewSet):
    """Root of the Accounts API: /api/v1/accounts/"""

    # GET /api/v1/accounts/
    def list(self, request):
        return Response({"detail": "Accounts API root"})

    # GET /api/v1/accounts/status/
    @action(detail=False, methods=["get"])
    def status(self, request):
        return Response({"ok": True})

    # POST /api/v1/accounts/auth/login/
    @action(detail=False, methods=["post"], url_path="auth/login")
    def auth_login(self, request):
        # ... handle login payload ...
        return Response({"token": "..."})

    # POST /api/v1/accounts/{pk}/reset-password/
    @action(detail=True, methods=["post"], url_path="reset-password")
    def reset_password(self, request, pk=None):
        # ... reset password for user pk ...
        return Response({"done": True})