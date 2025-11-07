from rest_framework.response import Response
from rest_framework.viewsets import ViewSet


class APIRootView(ViewSet)):
    """Placeholder endpoint for APIs root."""

    def get(self, request):
        return Response({"detail": "API coming soon."})
