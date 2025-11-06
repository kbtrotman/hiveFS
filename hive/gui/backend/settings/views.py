from rest_framework.response import Response
from rest_framework.views import APIView


class SettingsRootView(APIView):
    """Placeholder endpoint for settings APIs."""

    def get(self, request):
        return Response({"detail": "Settings API coming soon."})
