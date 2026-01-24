from django.contrib.auth import get_user_model
from rest_framework import permissions, viewsets
from rest_framework.decorators import action
from rest_framework.response import Response
from rest_framework.views import APIView

from .models import Group, GroupMembership, Role, RoleAssignment, LDAPModel, SAMLModel, MFAModel
from .serializers import (
    GroupMembershipSerializer,
    GroupSerializer,
    RoleAssignmentSerializer,
    RoleSerializer,
    UserProfileSerializer,
    LDAPSerializer,
    SAMLSerializer,
    MFASerializer,
)

User = get_user_model()


class UserViewSet(viewsets.ModelViewSet):
    """User endpoints including a profile endpoint for the current user."""

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

class LDAPViewSet(viewsets.ModelViewSet):
    """LDAP endpoints for directory configuration."""

    serializer_class = LDAPSerializer
    permission_classes = [permissions.IsAuthenticated]
    queryset = LDAPModel.objects.all()


class SAMLViewSet(viewsets.ModelViewSet):
    """SAML endpoints for identity provider configuration."""

    serializer_class = SAMLSerializer
    permission_classes = [permissions.IsAuthenticated]
    queryset = SAMLModel.objects.all()


class MFAViewSet(viewsets.ModelViewSet):
    """MFA endpoints for multi-factor settings."""

    serializer_class = MFASerializer
    permission_classes = [permissions.IsAuthenticated]
    queryset = MFAModel.objects.select_related("user")
