from django.db import models


class UiVirtualNode(models.Model):
    class NodeKind(models.TextChoices):
        VIRTUAL = "virtual", "Virtual"
        MOUNT = "mount", "Mount"

    class TargetType(models.TextChoices):
        NONE = "none", "None"
        HOST = "host", "Host"
        DENTRY = "dentry", "Dentry"

    id = models.BigAutoField(primary_key=True)
    parent = models.ForeignKey(
        "self",
        models.SET_NULL,
        blank=True,
        null=True,
        related_name="children",
        db_column="parent_id",
        db_constraint=False,
    )
    name = models.CharField(max_length=255)
    node_kind = models.CharField(
        max_length=7,
        choices=NodeKind.choices,
    )
    target_type = models.CharField(
        max_length=6,
        choices=TargetType.choices,
        default=TargetType.NONE,
    )
    target_host = models.CharField(max_length=128, blank=True, null=True)
    target_dentry = models.BigIntegerField(blank=True, null=True)
    sort_order = models.IntegerField(default=0)

    class Meta:
        db_table = "ui_virtual_node"
        managed = False
        indexes = [
            models.Index(fields=["parent"], name="idx_ui_virtual_parent"),
            models.Index(fields=["node_kind"], name="idx_ui_virtual_kind"),
        ]
        constraints = [
            models.UniqueConstraint(
                fields=["parent", "name"],
                name="uk_ui_virtual_parent_name",
            )
        ]

    def __str__(self):
        return f"{self.name} ({self.node_kind})"


class UiHostMap(models.Model):
    host_id = models.CharField(primary_key=True, max_length=128)
    root_dentry = models.BigIntegerField()

    class Meta:
        db_table = "ui_host_map"
        managed = False
        indexes = [
            models.Index(fields=["root_dentry"], name="idx_ui_host_root"),
        ]

    def __str__(self):
        return self.host_id
    
    
class UnifiedTree(models.Model):
    node_id = models.CharField(primary_key=True, max_length=64)
    parent_node_id = models.CharField(max_length=64, null=True)
    name = models.CharField(max_length=255)
    node_kind = models.CharField(max_length=32)
    inode_id = models.BigIntegerField(null=True)
    dentry_id = models.BigIntegerField(null=True)
    has_children = models.BooleanField()

    class Meta:
        managed = False
        db_table = 'v_unified_tree'
        