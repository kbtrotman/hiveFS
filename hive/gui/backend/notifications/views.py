from __future__ import annotations

from uuid import uuid4

from django.db.models import Q
from django.utils import timezone
from rest_framework import mixins, status, viewsets
from rest_framework.decorators import action
from rest_framework.permissions import IsAuthenticated
from rest_framework.response import Response
from rest_framework.viewsets import GenericViewSet

from .models import Alert, EmailTarget, Notification, Schedule
from .serializers import (
    AlertSerializer,
    EmailTargetSerializer,
    NotificationSerializer,
    ScheduleSerializer,
)


RECENT_DEFAULT_LIMIT = 5
RECENT_MAX_LIMIT = 50
SEVERITY_DEFAULT_LIMIT = 20
SEVERITY_MAX_LIMIT = 100

ACTIVE_ALERT_STATUSES = {
    Alert.Status.TRIGGERED,
    Alert.Status.ACKNOWLEDGED,
}
ACTIVE_ALERT_WORKFLOW_STATUSES = {
    Alert.WorkflowStatus.NEW,
    Alert.WorkflowStatus.ACKNOWLEDGED,
}

SEVERITY_ALIASES = {
    "error": Alert.Severity.CRITICAL,
    "errors": Alert.Severity.CRITICAL,
    "critical": Alert.Severity.CRITICAL,
    "warn": Alert.Severity.WARNING,
    "warning": Alert.Severity.WARNING,
    "warnings": Alert.Severity.WARNING,
}


def _parse_positive_int(value, *, default, maximum):
    try:
        parsed = int(value)
    except (TypeError, ValueError):
        return default
    if parsed < 1:
        return default
    return min(parsed, maximum)


def _parse_bool(value):
    if isinstance(value, bool):
        return value
    if value is None:
        return False
    return str(value).strip().lower() in {"1", "true", "yes", "on"}


class BaseWorkflowViewSet(mixins.ListModelMixin, mixins.RetrieveModelMixin, GenericViewSet):
    """Shared helpers for notification/alert workflows."""

    permission_classes = [IsAuthenticated]
    node_lookup_field: str | None = None
    status_choices: set[str] = set()
    workflow_status_choices: set[str] = set()
    system_only_statuses: set[str] = set()
    system_only_workflow_statuses: set[str] = {"new"}
    close_payload_field: str | None = None
    close_status_value: str | None = None

    def get_queryset(self):
        qs = super().get_queryset()
        qs = qs.select_related("schedule").prefetch_related("schedule__email_targets")

        request = self.request
        user_id = request.query_params.get("user_id")
        if not user_id and getattr(request.user, "is_authenticated", False):
            user_id = getattr(request.user, "id", None)

        if user_id is not None:
            try:
                user_id_int = int(user_id)
            except (TypeError, ValueError):
                user_id_int = None

            if user_id_int is not None:
                qs = qs.filter(schedule__email_targets__user_id=user_id_int)
            else:
                qs = qs.filter(schedule__email_targets__user_id=user_id)

        node_id = request.query_params.get("node_id")
        if node_id and self.node_lookup_field:
            try:
                node_id_int = int(node_id)
            except (TypeError, ValueError):
                node_id_int = None

            node_filter = Q(**{self.node_lookup_field: node_id})
            if node_id_int is not None:
                node_filter |= Q(**{self.node_lookup_field: node_id_int})
            qs = qs.filter(node_filter)

        status_value = self._normalize_choice(request.query_params.get("status"))
        if status_value:
            qs = qs.filter(status=status_value)

        workflow_value = self._normalize_choice(
            request.query_params.get("workflow_status")
            or request.query_params.get("w_status")
        )
        if workflow_value:
            qs = qs.filter(w_status=workflow_value)

        return qs.distinct().order_by("-updated_at")

    @action(detail=True, methods=["post"], url_path="status")
    def set_status(self, request, pk=None):
        instance = self.get_object()
        status_value = self._normalize_choice(request.data.get("status"))
        workflow_value = self._normalize_choice(
            request.data.get("workflow_status") or request.data.get("w_status")
        )

        if not status_value and not workflow_value:
            return Response(
                {"detail": "Provide status and/or workflow_status."},
                status=status.HTTP_400_BAD_REQUEST,
            )

        updates = []
        if status_value:
            error = self._validate_status_choice(status_value)
            if error:
                return error
            instance.status = status_value
            updates.append("status")

        if workflow_value:
            error = self._validate_workflow_choice(workflow_value)
            if error:
                return error
            instance.w_status = workflow_value
            updates.append("w_status")

        instance.save(update_fields=[*updates, "updated_at"])
        serializer = self.get_serializer(instance)
        return Response(serializer.data)

    @action(detail=True, methods=["post"], url_path="request-close")
    def request_close(self, request, pk=None):
        if not self.close_payload_field:
            return Response(
                {"detail": "Close workflow not supported."},
                status=status.HTTP_405_METHOD_NOT_ALLOWED,
            )

        instance = self.get_object()
        payload = self._ensure_payload(instance, self.close_payload_field)
        token = uuid4().hex
        payload["manager_close_request"] = {
            "token": token,
            "reason": request.data.get("reason"),
            "requested_by": request.data.get("requested_by")
            or getattr(getattr(request, "user", None), "id", None),
            "requested_at": timezone.now().isoformat(),
            "status": "pending",
        }

        setattr(instance, self.close_payload_field, payload)
        instance.w_status = instance.__class__.WorkflowStatus.REVIEW
        instance.save(update_fields=[self.close_payload_field, "w_status", "updated_at"])
        return Response(
            {
                "detail": "Close approval pending manager review.",
                "token": token,
            },
            status=status.HTTP_202_ACCEPTED,
        )

    @action(detail=True, methods=["post"], url_path="approve-close")
    def approve_close(self, request, pk=None):
        if not self.close_payload_field:
            return Response(
                {"detail": "Close workflow not supported."},
                status=status.HTTP_405_METHOD_NOT_ALLOWED,
            )

        token = self._normalize_choice(request.data.get("token"))
        if not token:
            return Response(
                {"detail": "token is required for approval."},
                status=status.HTTP_400_BAD_REQUEST,
            )

        instance = self.get_object()
        payload = self._ensure_payload(instance, self.close_payload_field)
        close_meta = payload.get("manager_close_request") or {}
        if close_meta.get("token") != token or close_meta.get("status") != "pending":
            return Response(
                {"detail": "No pending manager approval for provided token."},
                status=status.HTTP_400_BAD_REQUEST,
            )

        close_meta.update(
            {
                "status": "approved",
                "approved_at": timezone.now().isoformat(),
                "approved_by": request.data.get("approved_by")
                or getattr(getattr(request, "user", None), "id", None),
            }
        )
        payload["manager_close_request"] = close_meta
        setattr(instance, self.close_payload_field, payload)

        instance.w_status = instance.__class__.WorkflowStatus.CLOSED
        update_fields = [self.close_payload_field, "w_status", "updated_at"]
        if self.close_status_value:
            instance.status = self.close_status_value
            update_fields.append("status")

        instance.save(update_fields=update_fields)
        serializer = self.get_serializer(instance)
        return Response(serializer.data)

    def _normalize_choice(self, value):
        if value is None:
            return None
        normalized = str(value).strip().lower()
        return normalized or None

    def _validate_status_choice(self, value):
        if self.status_choices and value not in self.status_choices:
            return Response(
                {"detail": f"Invalid status '{value}'."},
                status=status.HTTP_400_BAD_REQUEST,
            )
        if value in self.system_only_statuses:
            return Response(
                {"detail": f"Status '{value}' can only be changed by the system."},
                status=status.HTTP_400_BAD_REQUEST,
            )
        return None

    def _validate_workflow_choice(self, value):
        if self.workflow_status_choices and value not in self.workflow_status_choices:
            return Response(
                {"detail": f"Invalid workflow status '{value}'."},
                status=status.HTTP_400_BAD_REQUEST,
            )
        if value in self.system_only_workflow_statuses:
            return Response(
                {"detail": f"Workflow status '{value}' can only be set by the system."},
                status=status.HTTP_400_BAD_REQUEST,
            )
        return None

    def _ensure_payload(self, instance, field):
        current = getattr(instance, field) or {}
        if not isinstance(current, dict):
            current = {}
        setattr(instance, field, current)
        return current


class NotificationViewSet(BaseWorkflowViewSet):
    queryset = Notification.objects.all()
    serializer_class = NotificationSerializer
    node_lookup_field = "payload__node_id"
    close_payload_field = "payload"
    close_status_value = Notification.Status.CANCELLED
    status_choices = set(Notification.Status.values)
    workflow_status_choices = set(Notification.WorkflowStatus.values)
    system_only_statuses = {
        Notification.Status.PENDING,
        Notification.Status.QUEUED,
    }


class AlertViewSet(BaseWorkflowViewSet):
    queryset = Alert.objects.all()
    serializer_class = AlertSerializer
    node_lookup_field = "metadata__node_id"
    close_payload_field = "metadata"
    close_status_value = Alert.Status.RESOLVED
    status_choices = set(Alert.Status.values)
    workflow_status_choices = set(Alert.WorkflowStatus.values)
    system_only_statuses = {Alert.Status.TRIGGERED}

    @action(detail=False, methods=["get"], url_path="recent")
    def recent_notifications(self, request):
        limit = _parse_positive_int(
            request.query_params.get("limit"),
            default=RECENT_DEFAULT_LIMIT,
            maximum=RECENT_MAX_LIMIT,
        )
        active_only = _parse_bool(request.query_params.get("active_only"))

        notif_qs = Notification.objects.all().order_by("-updated_at")
        alert_qs = Alert.objects.all().order_by("-triggered_at")

        if active_only:
            alert_qs = alert_qs.filter(
                Q(status__in=ACTIVE_ALERT_STATUSES)
                | Q(w_status__in=ACTIVE_ALERT_WORKFLOW_STATUSES)
            )

        notif_qs = notif_qs[:limit]
        alert_qs = alert_qs[:limit]

        notif_data = NotificationSerializer(notif_qs, many=True).data
        alert_data = AlertSerializer(alert_qs, many=True).data
        combined = sorted(
            [{"type": "notification", **item} for item in notif_data]
            + [{"type": "alert", **item} for item in alert_data],
            key=lambda entry: entry.get("updated_at") or entry.get("triggered_at"),
            reverse=True,
        )[:limit]

        return Response({"results": combined})

    @action(detail=False, methods=["get"], url_path="severity")
    def severity_filtered_alerts(self, request):
        limit = _parse_positive_int(
            request.query_params.get("limit"),
            default=SEVERITY_DEFAULT_LIMIT,
            maximum=SEVERITY_MAX_LIMIT,
        )
        severity_param = request.query_params.get("severity")
        severities = []

        if severity_param:
            parts = [item.strip().lower() for item in severity_param.split(",") if item.strip()]
            for part in parts:
                mapped = SEVERITY_ALIASES.get(part, part)
                if mapped in Alert.Severity.values:
                    severities.append(mapped)

        if not severities:
            severities = [Alert.Severity.CRITICAL]

        alerts = Alert.objects.filter(severity__in=severities).order_by("-triggered_at")[:limit]
        serializer = AlertSerializer(alerts, many=True)
        return Response({"results": serializer.data, "filters": {"severity": severities}})


class ScheduleViewSet(viewsets.ModelViewSet):
    permission_classes = [IsAuthenticated]
    serializer_class = ScheduleSerializer

    def get_queryset(self):
        qs = Schedule.objects.all().order_by("schedule_id")
        name = self.request.query_params.get("name")
        if name:
            qs = qs.filter(schedule_name__icontains=name)
        return qs


class EmailTargetViewSet(viewsets.ModelViewSet):
    permission_classes = [IsAuthenticated]
    serializer_class = EmailTargetSerializer

    def get_queryset(self):
        qs = EmailTarget.objects.select_related("schedule").order_by("target_id")
        schedule_id = self.request.query_params.get("schedule_id")
        if schedule_id:
            qs = qs.filter(schedule_id=schedule_id)
        user_id = self.request.query_params.get("user_id")
        if user_id:
            qs = qs.filter(user_id=user_id)
        return qs
