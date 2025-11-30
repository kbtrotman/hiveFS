from django.conf import settings
from django.db import migrations, models
import django.db.models.deletion


class Migration(migrations.Migration):

    dependencies = [
        ("tenant", "0001_initial"),
        ("accounts", "0001_initial"),
    ]

    operations = [
        migrations.CreateModel(
            name="Role",
            fields=[
                ("role_id", models.BigAutoField(primary_key=True, serialize=False)),
                ("name", models.CharField(max_length=150, unique=True)),
                ("description", models.TextField(blank=True)),
                ("created_at", models.DateTimeField(auto_now_add=True)),
                ("updated_at", models.DateTimeField(auto_now=True)),
            ],
            options={"ordering": ["name"]},
        ),
        migrations.CreateModel(
            name="Group",
            fields=[
                ("group_id", models.BigAutoField(primary_key=True, serialize=False)),
                ("name", models.CharField(max_length=150, unique=True)),
                ("description", models.TextField(blank=True)),
                ("created_at", models.DateTimeField(auto_now_add=True)),
                ("updated_at", models.DateTimeField(auto_now=True)),
                (
                    "tenant",
                    models.ForeignKey(
                        on_delete=django.db.models.deletion.CASCADE,
                        related_name="groups",
                        to="tenant.tenant",
                    ),
                ),
            ],
            options={"ordering": ["name"]},
        ),
        migrations.CreateModel(
            name="RoleAssignment",
            fields=[
                (
                    "id",
                    models.BigAutoField(
                        auto_created=True,
                        primary_key=True,
                        serialize=False,
                        verbose_name="ID",
                    ),
                ),
                (
                    "permission_type",
                    models.CharField(
                        choices=[
                            ("cluster", "Cluster"),
                            ("volume", "Volume"),
                            ("snapshot", "Snapshot"),
                            ("node", "Node"),
                        ],
                        max_length=32,
                    ),
                ),
                ("permission_id", models.BigIntegerField(blank=True, null=True)),
                ("created_at", models.DateTimeField(auto_now_add=True)),
                (
                    "role",
                    models.ForeignKey(
                        on_delete=django.db.models.deletion.CASCADE,
                        related_name="assignments",
                        to="accounts.role",
                    ),
                ),
                (
                    "tenant",
                    models.ForeignKey(
                        blank=True,
                        null=True,
                        on_delete=django.db.models.deletion.CASCADE,
                        related_name="role_assignments",
                        to="tenant.tenant",
                    ),
                ),
            ],
            options={
                "verbose_name": "Role Assignment",
                "verbose_name_plural": "Role Assignments",
            },
        ),
        migrations.CreateModel(
            name="GroupRole",
            fields=[
                (
                    "id",
                    models.BigAutoField(
                        auto_created=True,
                        primary_key=True,
                        serialize=False,
                        verbose_name="ID",
                    ),
                ),
                ("assigned_at", models.DateTimeField(auto_now_add=True)),
                (
                    "group",
                    models.ForeignKey(
                        on_delete=django.db.models.deletion.CASCADE,
                        related_name="group_roles",
                        to="accounts.group",
                    ),
                ),
                (
                    "role",
                    models.ForeignKey(
                        on_delete=django.db.models.deletion.CASCADE,
                        related_name="group_roles",
                        to="accounts.role",
                    ),
                ),
            ],
            options={
                "verbose_name": "Group Role",
                "verbose_name_plural": "Group Roles",
                "unique_together": {("group", "role")},
            },
        ),
        migrations.CreateModel(
            name="GroupMembership",
            fields=[
                (
                    "id",
                    models.BigAutoField(
                        auto_created=True,
                        primary_key=True,
                        serialize=False,
                        verbose_name="ID",
                    ),
                ),
                (
                    "role_in_group",
                    models.CharField(
                        choices=[
                            ("member", "Member"),
                            ("owner", "Owner"),
                            ("admin", "Admin"),
                        ],
                        default="member",
                        max_length=16,
                    ),
                ),
                ("added_at", models.DateTimeField(auto_now_add=True)),
                (
                    "group",
                    models.ForeignKey(
                        on_delete=django.db.models.deletion.CASCADE,
                        related_name="memberships",
                        to="accounts.group",
                    ),
                ),
                (
                    "user",
                    models.ForeignKey(
                        on_delete=django.db.models.deletion.CASCADE,
                        related_name="group_memberships",
                        to=settings.AUTH_USER_MODEL,
                    ),
                ),
            ],
            options={
                "verbose_name": "Group Membership",
                "verbose_name_plural": "Group Memberships",
                "unique_together": {("group", "user")},
            },
        ),
        migrations.AddField(
            model_name="group",
            name="roles",
            field=models.ManyToManyField(
                blank=True,
                related_name="groups",
                through="accounts.GroupRole",
                to="accounts.role",
            ),
        ),
    ]
