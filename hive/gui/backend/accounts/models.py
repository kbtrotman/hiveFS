from django.conf import settings
from django.contrib.auth.models import (
    AbstractBaseUser,
    PermissionsMixin,
    BaseUserManager,
)
from django.utils import timezone
from django.utils.translation import gettext_lazy as _
from django.db import models

from tenant.models import Tenant


class CustomUserManager(BaseUserManager):
    """Manager that uses email as the unique identifier."""

    def _create_user(self, email, password, **extra_fields):
        if not email:
            raise ValueError("Email address must be provided")
        email = self.normalize_email(email)
        user = self.model(email=email, **extra_fields)
        user.set_password(password)
        user.save(using=self._db)
        return user

    def create_user(self, email, password=None, **extra_fields):
        extra_fields.setdefault("is_active", True)
        extra_fields.setdefault("is_staff", False)
        extra_fields.setdefault("is_superuser", False)
        if not extra_fields.get("username"):
            extra_fields["username"] = email.split("@")[0]
        return self._create_user(email, password, **extra_fields)

    def create_superuser(self, email, password, **extra_fields):
        extra_fields.setdefault("is_staff", True)
        extra_fields.setdefault("is_superuser", True)
        extra_fields.setdefault("is_active", True)
        if extra_fields.get("is_staff") is not True:
            raise ValueError("Superuser must have is_staff=True.")
        if extra_fields.get("is_superuser") is not True:
            raise ValueError("Superuser must have is_superuser=True.")
        return self._create_user(email, password, **extra_fields)


class CustomUser(AbstractBaseUser, PermissionsMixin):
    user_id = models.BigAutoField(primary_key=True)
    email = models.EmailField(_("email address"), unique=True)
    username = models.CharField(_("username"), max_length=150)
    first_name = models.CharField(_("first name"), max_length=150, blank=True)
    last_name = models.CharField(_("last name"), max_length=150, blank=True)
    email_verified = models.BooleanField(default=False)
    status = models.CharField(max_length=50, blank=True)
    created_at = models.DateTimeField(auto_now_add=True)
    updated_at = models.DateTimeField(auto_now=True)
    last_login = models.DateTimeField(blank=True, null=True)
    auth_source = models.CharField(max_length=50, blank=True)
    mfa_enabled = models.BooleanField(default=False)
    mfa_method = models.CharField(max_length=50, blank=True)
    tenant_id = models.CharField(max_length=100, blank=True)
    department = models.CharField(max_length=100, blank=True)
    title = models.CharField(max_length=100, blank=True)
    time_zone = models.CharField(max_length=50, blank=True, default='UTC')
    cost_center = models.CharField(max_length=30, blank=True)

    is_staff = models.BooleanField(
        _("staff status"),
        default=False,
        help_text=_("Designates whether the user can log into the admin site."),
    )
    is_active = models.BooleanField(
        _("active"),
        default=True,
        help_text=_(
            "Designates whether this user should be treated as active. "
            "Unselect this instead of deleting accounts."
        ),
    )
    date_joined = models.DateTimeField(default=timezone.now)

    objects = CustomUserManager()

    EMAIL_FIELD = "email"
    USERNAME_FIELD = "email"
    REQUIRED_FIELDS = ["username"]

    class Meta:
        verbose_name = _("user")
        verbose_name_plural = _("users")

    def __str__(self):
        return self.email

    @property
    def display_name(self):
        if self.first_name or self.last_name:
            return f"{self.first_name} {self.last_name}".strip()
        return self.username or self.email


class Role(models.Model):
    role_id = models.BigAutoField(primary_key=True)
    name = models.CharField(max_length=150, unique=True)
    description = models.TextField(blank=True)
    created_at = models.DateTimeField(auto_now_add=True)
    updated_at = models.DateTimeField(auto_now=True)

    class Meta:
        ordering = ["name"]

    def __str__(self):
        return self.name


class Group(models.Model):
    group_id = models.BigAutoField(primary_key=True)
    tenant = models.ForeignKey(
        Tenant, on_delete=models.CASCADE, related_name="groups"
    )
    name = models.CharField(max_length=150, unique=True)
    description = models.TextField(blank=True)
    roles = models.ManyToManyField(
        Role, through="GroupRole", related_name="groups", blank=True
    )
    created_at = models.DateTimeField(auto_now_add=True)
    updated_at = models.DateTimeField(auto_now=True)

    class Meta:
        ordering = ["name"]

    def __str__(self):
        return self.name


class GroupRole(models.Model):
    group = models.ForeignKey(Group, on_delete=models.CASCADE, related_name="group_roles")
    role = models.ForeignKey(Role, on_delete=models.CASCADE, related_name="group_roles")
    assigned_at = models.DateTimeField(auto_now_add=True)

    class Meta:
        unique_together = ("group", "role")
        verbose_name = "Group Role"
        verbose_name_plural = "Group Roles"

    def __str__(self):
        return f"{self.group.name} → {self.role.name}"


class GroupMembership(models.Model):
    class MembershipRole(models.TextChoices):
        MEMBER = "member", "Member"
        OWNER = "owner", "Owner"
        ADMIN = "admin", "Admin"

    group = models.ForeignKey(
        Group, on_delete=models.CASCADE, related_name="memberships"
    )
    user = models.ForeignKey(
        settings.AUTH_USER_MODEL,
        on_delete=models.CASCADE,
        related_name="group_memberships",
    )
    role_in_group = models.CharField(
        max_length=16,
        choices=MembershipRole.choices,
        default=MembershipRole.MEMBER,
    )
    added_at = models.DateTimeField(auto_now_add=True)

    class Meta:
        unique_together = ("group", "user")
        verbose_name = "Group Membership"
        verbose_name_plural = "Group Memberships"

    def __str__(self):
        return f"{self.user} in {self.group}"


class RoleAssignment(models.Model):
    class PermissionType(models.TextChoices):
        CLUSTER = "cluster", "Cluster"
        VOLUME = "volume", "Volume"
        SNAPSHOT = "snapshot", "Snapshot"
        NODE = "node", "Node"

    role = models.ForeignKey(
        Role, on_delete=models.CASCADE, related_name="assignments"
    )
    tenant = models.ForeignKey(
        Tenant,
        on_delete=models.CASCADE,
        related_name="role_assignments",
        null=True,
        blank=True,
    )
    permission_type = models.CharField(
        max_length=32, choices=PermissionType.choices
    )
    permission_id = models.BigIntegerField(null=True, blank=True)
    created_at = models.DateTimeField(auto_now_add=True)

    class Meta:
        verbose_name = "Role Assignment"
        verbose_name_plural = "Role Assignments"

    def __str__(self):
        target = self.permission_id if self.permission_id is not None else "*"
        return f"{self.role.name}: {self.permission_type} ({target})"
