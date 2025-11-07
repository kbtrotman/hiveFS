from rest_framework.response import Response
from rest_framework.viewsets import ViewSet


class SettingsRootView(ViewSet):
    """Placeholder endpoint for settings APIs."""

    def get(self, request):
        return Response({"detail": "Settings API coming soon."})
