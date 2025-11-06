from rest_framework.response import Response
from rest_framework.views import APIView


class AccountsRootView(APIView):
    """Placeholder endpoint for future accounts APIs."""

    def get(self, request):
        return Response({"detail": "Accounts API coming soon."})
