from rest_framework.response import Response
from rest_framework.views import APIView


class TenantRootView(APIView):
    """Placeholder endpoint for tenant APIs."""

    def get(self, request):
        return Response({"detail": "Tenant API coming soon."})
