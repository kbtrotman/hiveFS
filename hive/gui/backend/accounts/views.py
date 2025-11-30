from django.contrib.auth import get_user_model
from rest_framework import permissions, viewsets
from rest_framework.decorators import action
from rest_framework.response import Response
from rest_framework.views import APIView

from .serializers import UserProfileSerializer

User = get_user_model()


class UserViewSet(viewsets.GenericViewSet):
    """Endpoints for authenticated users to inspect their profile."""

    serializer_class = UserProfileSerializer
    permission_classes = [permissions.IsAuthenticated]
    queryset = User.objects.all()

    @action(detail=False, methods=["get"], url_path="me")
    def me(self, request):
        serializer = self.get_serializer(request.user)
        return Response(serializer.data)


class SessionStatusView(APIView):
    permission_classes = [permissions.AllowAny]

    def get(self, request):
        authenticated = request.user.is_authenticated
        data = {"authenticated": authenticated}
        if authenticated:
            data["user"] = UserProfileSerializer(request.user).data
        return Response(data)
