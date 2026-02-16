from django.db import models


class Schedule(models.Model):
    class Frequency(models.TextChoices):
        ONCE = "once", "Once"
        INTERVAL = "interval", "Interval"
        CRON = "cron", "Cron"

    schedule_id = models.BigAutoField(primary_key=True)
    schedule_name = models.CharField(max_length=128, unique=True)
    description = models.CharField(max_length=255, blank=True, null=True)
    timezone = models.CharField(max_length=64, default="UTC")
    start_at = models.DateTimeField(blank=True, null=True)
    end_at = models.DateTimeField(blank=True, null=True)
    frequency = models.CharField(
        max_length=8,
        choices=Frequency.choices,
        default=Frequency.CRON,
    )
    interval_seconds = models.PositiveIntegerField(blank=True, null=True)
    cron_expression = models.CharField(max_length=255, blank=True, null=True)
    is_active = models.BooleanField(default=True)
    created_at = models.DateTimeField(auto_now_add=True)
    updated_at = models.DateTimeField(auto_now=True)

    class Meta:
        managed = False
        db_table = "schedules"
        indexes = [
            models.Index(fields=["is_active", "start_at"], name="idx_schedule_active"),
        ]

    def __str__(self) -> str:
        return self.schedule_name


class Notification(models.Model):
    class NotificationType(models.TextChoices):
        EMAIL = "email", "Email"
        TICKET = "ticket", "Ticket"
        WEBHOOK = "webhook", "Webhook"

    class Status(models.TextChoices):
        PENDING = "pending", "Pending"
        QUEUED = "queued", "Queued"
        SENT = "sent", "Sent"
        FAILED = "failed", "Failed"
        CANCELLED = "cancelled", "Cancelled"

    class WorkflowStatus(models.TextChoices):
        NEW = "new", "New"
        ACKNOWLEDGED = "acknowledged", "Acknowledged"
        WORKING = "working", "Working"
        PENDING_WAIT = "pending wait", "Pending Wait"
        REVIEW = "review", "Review"
        RESOLVED = "resolved", "Resolved"
        CLOSED = "closed", "Closed"

    class Priority(models.TextChoices):
        LOW = "low", "Low"
        NORMAL = "normal", "Normal"
        HIGH = "high", "High"

    notification_id = models.BigAutoField(primary_key=True)
    schedule = models.ForeignKey(
        "Schedule",
        models.CASCADE,
        db_column="schedule_id",
        related_name="notifications",
    )
    audit_entry = models.ForeignKey(
        "audit.AuditEntry",
        models.SET_NULL,
        blank=True,
        null=True,
        db_column="audit_entry_id",
        related_name="notifications",
    )
    notification_type = models.CharField(
        max_length=7,
        choices=NotificationType.choices,
        default=NotificationType.EMAIL,
    )
    subject = models.CharField(max_length=255)
    body = models.TextField(blank=True, null=True)
    payload = models.JSONField(blank=True, null=True)
    status = models.CharField(
        max_length=9,
        choices=Status.choices,
        default=Status.PENDING,
    )
    w_status = models.CharField(
        max_length=13,
        choices=WorkflowStatus.choices,
        default=WorkflowStatus.NEW,
    )
    priority = models.CharField(
        max_length=6,
        choices=Priority.choices,
        default=Priority.NORMAL,
    )
    next_attempt_at = models.DateTimeField(blank=True, null=True)
    last_attempt_at = models.DateTimeField(blank=True, null=True)
    attempt_count = models.PositiveIntegerField(default=0)
    created_at = models.DateTimeField(auto_now_add=True)
    updated_at = models.DateTimeField(auto_now=True)

    class Meta:
        managed = False
        db_table = "notifications"
        indexes = [
            models.Index(
                fields=["schedule", "status"],
                name="idx_notif_sched_status",
            ),
            models.Index(fields=["notification_type"], name="idx_notification_type"),
            models.Index(fields=["audit_entry"], name="idx_notification_audit"),
        ]

    def __str__(self) -> str:
        return self.subject


class Alert(models.Model):
    class Severity(models.TextChoices):
        INFO = "info", "Info"
        WARNING = "warning", "Warning"
        CRITICAL = "critical", "Critical"

    class Status(models.TextChoices):
        TRIGGERED = "triggered", "Triggered"
        ACKNOWLEDGED = "acknowledged", "Acknowledged"
        RESOLVED = "resolved", "Resolved"
        SUPPRESSED = "suppressed", "Suppressed"

    class WorkflowStatus(models.TextChoices):
        NEW = "new", "New"
        ACKNOWLEDGED = "acknowledged", "Acknowledged"
        WORKING = "working", "Working"
        PENDING_WAIT = "pending wait", "Pending Wait"
        REVIEW = "review", "Review"
        RESOLVED = "resolved", "Resolved"
        CLOSED = "closed", "Closed"

    alert_id = models.BigAutoField(primary_key=True)
    schedule = models.ForeignKey(
        "Schedule",
        models.CASCADE,
        db_column="schedule_id",
        related_name="alerts",
    )
    source = models.CharField(max_length=128)
    title = models.CharField(max_length=255)
    message = models.TextField()
    severity = models.CharField(
        max_length=8,
        choices=Severity.choices,
        default=Severity.INFO,
    )
    status = models.CharField(
        max_length=12,
        choices=Status.choices,
        default=Status.TRIGGERED,
    )
    w_status = models.CharField(
        max_length=13,
        choices=WorkflowStatus.choices,
        default=WorkflowStatus.NEW,
    )
    triggered_at = models.DateTimeField(auto_now_add=True)
    acknowledged_at = models.DateTimeField(blank=True, null=True)
    resolved_at = models.DateTimeField(blank=True, null=True)
    created_by = models.BigIntegerField(blank=True, null=True)
    metadata = models.JSONField(blank=True, null=True)

    class Meta:
        managed = False
        db_table = "alerts"
        indexes = [
            models.Index(
                fields=["schedule", "status"],
                name="idx_alert_schedule_status",
            ),
            models.Index(fields=["severity"], name="idx_alert_severity"),
        ]

    def __str__(self) -> str:
        return self.title


class EmailTarget(models.Model):
    class DeliveryChannel(models.TextChoices):
        EMAIL = "email", "Email"
        TICKET = "ticket", "Ticket"
        WEBHOOK = "webhook", "Webhook"

    target_id = models.BigAutoField(primary_key=True)
    schedule = models.ForeignKey(
        "Schedule",
        models.CASCADE,
        db_column="schedule_id",
        related_name="email_targets",
    )
    user_id = models.BigIntegerField()
    target_name = models.CharField(max_length=255, blank=True, null=True)
    destination_email = models.CharField(max_length=320, blank=True, null=True)
    delivery_channel = models.CharField(
        max_length=7,
        choices=DeliveryChannel.choices,
        default=DeliveryChannel.EMAIL,
    )
    is_primary = models.BooleanField(default=False)
    created_at = models.DateTimeField(auto_now_add=True)
    updated_at = models.DateTimeField(auto_now=True)

    class Meta:
        managed = False
        db_table = "email_targets"
        constraints = [
            models.UniqueConstraint(
                fields=["schedule", "user_id", "delivery_channel"],
                name="uk_user_schedule_channel",
            )
        ]
        indexes = [
            models.Index(fields=["user_id"], name="idx_email_target_user"),
        ]

    def __str__(self) -> str:
        return self.target_name or str(self.target_id)
