from django.contrib.auth import get_user_model
from rest_framework import permissions, viewsets
from rest_framework.decorators import action
from rest_framework.response import Response
from rest_framework.views import APIView

from .models import Group, GroupMembership, Role, RoleAssignment
from .serializers import (
    GroupMembershipSerializer,
    GroupSerializer,
    RoleAssignmentSerializer,
    RoleSerializer,
    UserProfileSerializer,
)

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


class GroupViewSet(viewsets.ModelViewSet):
    queryset = Group.objects.select_related("tenant").prefetch_related("roles")
    serializer_class = GroupSerializer
    permission_classes = [permissions.IsAuthenticated]


class GroupMembershipViewSet(viewsets.ModelViewSet):
    queryset = GroupMembership.objects.select_related("group", "user")
    serializer_class = GroupMembershipSerializer
    permission_classes = [permissions.IsAuthenticated]


class RoleViewSet(viewsets.ModelViewSet):
    queryset = Role.objects.all()
    serializer_class = RoleSerializer
    permission_classes = [permissions.IsAuthenticated]


class RoleAssignmentViewSet(viewsets.ModelViewSet):
    queryset = RoleAssignment.objects.select_related("role", "tenant")
    serializer_class = RoleAssignmentSerializer
    permission_classes = [permissions.IsAuthenticated]
