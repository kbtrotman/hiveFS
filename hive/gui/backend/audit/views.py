from rest_framework.response import Response
from rest_framework.views import APIView


class AuditRootView(APIView):
    """Placeholder endpoint for audit APIs."""

    def get(self, request):
        return Response({"detail": "Audit API coming soon."})
