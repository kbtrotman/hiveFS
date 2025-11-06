from rest_framework.response import Response
from rest_framework.views import APIView


class APIRootView(APIView):
    """Placeholder endpoint for APIs root."""

    def get(self, request):
        return Response({"detail": "API coming soon."})
