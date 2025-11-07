from rest_framework.response import Response
from rest_framework.viewsets import ViewSet


class TenantRootView(ViewSet):
    """Placeholder endpoint for tenant APIs."""

    def get(self, request):
        return Response({"detail": "Tenant API coming soon."})
