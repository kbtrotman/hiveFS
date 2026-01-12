from django.contrib.auth import get_user_model
from dj_rest_auth.registration.serializers import RegisterSerializer
from rest_framework import serializers

from tenant.models import Tenant
from .models import Group, GroupMembership, Role, RoleAssignment


class UserProfileSerializer(serializers.ModelSerializer):
    class Meta:
        model = get_user_model()
        fields = (
            "id",
            "email",
            "username",
            "first_name",
            "last_name",
            "email_verified",
            "status",
            "created_at",
            "updated_at",
            "last_login",
            "auth_source",
            "mfa_enabled",
            "mfa_method",
            "tenant_id",
            "department",
            "title",
            "time_zone",
            "cost_center",
            "is_active",
            "is_staff",
            "date_joined",
        )


class CustomRegisterSerializer(RegisterSerializer):
    username = serializers.CharField(required=True, max_length=150)
    first_name = serializers.CharField(required=False, allow_blank=True, max_length=150)
    last_name = serializers.CharField(required=False, allow_blank=True, max_length=150)

    def get_cleaned_data(self):
        data = super().get_cleaned_data()
        data["username"] = self.validated_data.get("username", "")
        data["first_name"] = self.validated_data.get("first_name", "")
        data["last_name"] = self.validated_data.get("last_name", "")
        return data

    def save(self, request):
        user = super().save(request)
        user.first_name = self.cleaned_data.get("first_name", "")
        user.last_name = self.cleaned_data.get("last_name", "")
        user.save(update_fields=["first_name", "last_name"])
        return user


class RoleSerializer(serializers.ModelSerializer):
    class Meta:
        model = Role
        fields = ("role_id", "name", "description", "created_at", "updated_at")
        read_only_fields = ("role_id", "created_at", "updated_at")


class RoleAssignmentSerializer(serializers.ModelSerializer):
    role_id = serializers.PrimaryKeyRelatedField(
        queryset=Role.objects.all(), source="role"
    )
    tenant_id = serializers.PrimaryKeyRelatedField(
        queryset=Tenant.objects.all(),
        source="tenant",
        allow_null=True,
        required=False,
    )

    class Meta:
        model = RoleAssignment
        fields = (
            "id",
            "role_id",
            "tenant_id",
            "permission_type",
            "permission_id",
            "created_at",
        )
        read_only_fields = ("id", "created_at")


class GroupSerializer(serializers.ModelSerializer):
    tenant_id = serializers.PrimaryKeyRelatedField(
        queryset=Tenant.objects.all(), source="tenant"
    )
    roles = serializers.PrimaryKeyRelatedField(
        queryset=Role.objects.all(), many=True, required=False
    )

    class Meta:
        model = Group
        fields = (
            "group_id",
            "name",
            "description",
            "tenant_id",
            "roles",
            "created_at",
            "updated_at",
        )
        read_only_fields = ("group_id", "created_at", "updated_at")

    def create(self, validated_data):
        roles = validated_data.pop("roles", [])
        group = super().create(validated_data)
        if roles:
            group.roles.set(roles)
        return group

    def update(self, instance, validated_data):
        roles = validated_data.pop("roles", None)
        group = super().update(instance, validated_data)
        if roles is not None:
            group.roles.set(roles)
        return group


class GroupMembershipSerializer(serializers.ModelSerializer):
    group_id = serializers.PrimaryKeyRelatedField(
        queryset=Group.objects.all(), source="group"
    )
    user_id = serializers.PrimaryKeyRelatedField(
        queryset=get_user_model().objects.all(), source="user"
    )

    class Meta:
        model = GroupMembership
        fields = (
            "id",
            "group_id",
            "user_id",
            "role_in_group",
            "added_at",
        )
        read_only_fields = ("id", "added_at")
