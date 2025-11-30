from django.contrib import admin
from django.contrib.auth.admin import UserAdmin as BaseUserAdmin
from django.utils.translation import gettext_lazy as _

from .models import (
    CustomUser,
    Group,
    GroupMembership,
    GroupRole,
    Role,
    RoleAssignment,
)


@admin.register(CustomUser)
class CustomUserAdmin(BaseUserAdmin):
    ordering = ("email",)
    list_display = ("email", "username", "first_name", "last_name", "is_staff")
    search_fields = ("email", "username", "first_name", "last_name")
    readonly_fields = ("date_joined",)

    fieldsets = (
        (None, {"fields": ("email", "password")}),
        (_("Personal info"), {"fields": ("username", "first_name", "last_name")}),
        (
            _("Permissions"),
            {
                "fields": (
                    "is_active",
                    "is_staff",
                    "is_superuser",
                    "groups",
                    "user_permissions",
                )
            },
        ),
        (_("Important dates"), {"fields": ("last_login", "date_joined")}),
        (_("Verification"), {"fields": ("email_verified",)}),
    )
    add_fieldsets = (
        (
            None,
            {
                "classes": ("wide",),
                "fields": ("email", "username", "password1", "password2"),
            },
        ),
    )


@admin.register(Role)
class RoleAdmin(admin.ModelAdmin):
    list_display = ("name", "description", "created_at", "updated_at")
    search_fields = ("name",)


@admin.register(Group)
class GroupAdmin(admin.ModelAdmin):
    list_display = ("name", "tenant", "created_at", "updated_at")
    search_fields = ("name",)
    list_filter = ("tenant",)
    filter_horizontal = ("roles",)


@admin.register(GroupMembership)
class GroupMembershipAdmin(admin.ModelAdmin):
    list_display = ("group", "user", "role_in_group", "added_at")
    list_filter = ("role_in_group",)
    search_fields = ("group__name", "user__email")


@admin.register(GroupRole)
class GroupRoleAdmin(admin.ModelAdmin):
    list_display = ("group", "role", "assigned_at")
    search_fields = ("group__name", "role__name")


@admin.register(RoleAssignment)
class RoleAssignmentAdmin(admin.ModelAdmin):
    list_display = ("role", "permission_type", "permission_id", "tenant", "created_at")
    list_filter = ("permission_type",)
    search_fields = ("role__name",)
