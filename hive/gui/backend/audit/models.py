from django.db import models


class AuditEntry(models.Model):
    audit_entry_id = models.BigAutoField(primary_key=True)
    user_id = models.BigIntegerField()
    username = models.CharField(max_length=150)
    user_display_name = models.CharField(max_length=255, blank=True, null=True)
    page_name = models.CharField(max_length=128)
    setting_name = models.CharField(max_length=128)
    previous_value = models.TextField(blank=True, null=True)
    new_value = models.TextField(blank=True, null=True)
    change_summary = models.TextField(blank=True, null=True)
    change_context = models.JSONField(blank=True, null=True)
    request_ip = models.CharField(max_length=64, blank=True, null=True)
    request_id = models.CharField(max_length=36, blank=True, null=True)
    created_at = models.DateTimeField(auto_now_add=True)

    class Meta:
        managed = False
        db_table = "audit_entries"
        indexes = [
            models.Index(fields=["user_id", "created_at"], name="idx_audit_user"),
            models.Index(fields=["page_name", "setting_name"], name="idx_audit_page_setting"),
            models.Index(fields=["request_id"], name="idx_audit_request"),
        ]

    def __str__(self) -> str:
        return f"{self.username} {self.setting_name}"
