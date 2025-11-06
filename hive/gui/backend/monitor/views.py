from rest_framework.response import Response
from rest_framework.views import APIView


class MonitorRootView(APIView):
    """Placeholder endpoint for monitor APIs."""

    def get(self, request):
        return Response({"detail": "Monitor API coming soon."})
