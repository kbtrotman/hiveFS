from rest_framework.response import Response
from rest_framework.viewsets import ViewSet


class MonitorRootView(ViewSet):
    """Placeholder endpoint for monitor APIs."""

    def get(self, request):
        return Response({"detail": "Monitor API coming soon."})
