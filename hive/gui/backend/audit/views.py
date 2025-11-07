from rest_framework.response import Response
from rest_framework.viewsets import ViewSet


class AuditRootView(ViewSet):
    """Placeholder endpoint for audit APIs."""

    def get(self, request):
        return Response({"detail": "Audit API coming soon."})
