from __future__ import annotations

from django.db.models import Count, Q
from django.utils.dateparse import parse_datetime
from rest_framework import mixins, status, viewsets
from rest_framework.decorators import action
from rest_framework.permissions import IsAuthenticated
from rest_framework.response import Response

from .models import AuditEntry
from .serializers import AuditEntrySerializer
from .log_management import LogManager, InvalidLogError, LogNotFoundError


class AuditEntryViewSet(mixins.ListModelMixin, mixins.RetrieveModelMixin, viewsets.GenericViewSet):
    """Read-only access to audit entries with filtering helpers."""

    permission_classes = [IsAuthenticated]
    serializer_class = AuditEntrySerializer
    queryset = AuditEntry.objects.all()
    MAX_LOG_BYTES = 5 * 1024 * 1024  # hard stop at 5MB payloads

    def get_queryset(self):
        qs = super().get_queryset().order_by("-created_at")
        params = self.request.query_params

        criteria = self._build_criteria(params)
        if criteria:
            qs = qs.filter(*criteria)

        limit = params.get("limit")
        if limit:
            try:
                limit_val = min(int(limit), 200)
                if limit_val > 0:
                    qs = qs[:limit_val]
            except (TypeError, ValueError):
                pass

        return qs

    @action(detail=False, methods=["get"], url_path="summary")
    def summary(self, request):
        params = request.query_params
        criteria = self._build_criteria(params)
        qs = AuditEntry.objects.filter(*criteria) if criteria else AuditEntry.objects.all()

        aggregates = (
            qs.values("page_name", "setting_name")
            .order_by("page_name", "setting_name")
            .annotate(entry_count=Count("audit_entry_id"))
        )
        return Response(list(aggregates))

    @action(detail=False, methods=["get"], url_path="logs")
    def fetch_log(self, request):
        """Return the requested log (or archive) content via LogManager."""

        manager = LogManager()
        log_key = request.query_params.get("log")
        if not log_key:
            return Response(
                {
                    "detail": "Query parameter 'log' is required.",
                    "available_logs": manager.list_logs(),
                },
                status=status.HTTP_400_BAD_REQUEST,
            )

        archive_name = request.query_params.get("archive")
        max_bytes_param = request.query_params.get("max_bytes")
        max_bytes: int | None = None
        if max_bytes_param:
            try:
                parsed = int(max_bytes_param)
            except (TypeError, ValueError):
                return Response(
                    {"detail": "Query parameter 'max_bytes' must be an integer."},
                    status=status.HTTP_400_BAD_REQUEST,
                )
            if parsed > 0:
                max_bytes = min(parsed, self.MAX_LOG_BYTES)

        try:
            if archive_name:
                content = manager.view_archive(log_key, archive_name)
                response_payload = {
                    "log": log_key,
                    "archive": archive_name,
                    "content": content,
                    "is_archive": True,
                }
            else:
                content = manager.get_current_log(log_key, max_bytes=max_bytes)
                response_payload = {
                    "log": log_key,
                    "content": content,
                    "is_archive": False,
                }
            response_payload["available_archives"] = manager.list_archives(log_key)
            response_payload["truncated"] = (
                max_bytes is not None and len(content.encode("utf-8")) >= max_bytes
            )
            return Response(response_payload)
        except InvalidLogError as exc:
            return Response(
                {"detail": str(exc), "available_logs": manager.list_logs()},
                status=status.HTTP_400_BAD_REQUEST,
            )
        except LogNotFoundError as exc:
            return Response({"detail": str(exc)}, status=status.HTTP_404_NOT_FOUND)

    def _parse_dt(self, value):
        if not value:
            return None
        parsed = parse_datetime(value)
        return parsed

    def _build_criteria(self, params):
        criteria: list[Q] = []

        user_id = params.get("user_id")
        if user_id:
            try:
                user_id_int = int(user_id)
            except (TypeError, ValueError):
                user_id_int = None
            cond = Q(user_id=user_id)
            if user_id_int is not None:
                cond |= Q(user_id=user_id_int)
            criteria.append(cond)
        else:
            req_user = getattr(self.request, "user", None)
            if getattr(req_user, "is_authenticated", False):
                req_user_id = getattr(req_user, "id", None)
                if req_user_id is not None:
                    criteria.append(Q(user_id=req_user_id))

        node_id = params.get("node_id")
        if node_id:
            criteria.append(
                Q(change_context__node_id=node_id) | Q(change_context__node__id=node_id)
            )

        page_name = params.get("page_name")
        if page_name:
            criteria.append(Q(page_name__iexact=page_name))

        setting_name = params.get("setting_name")
        if setting_name:
            criteria.append(Q(setting_name__iexact=setting_name))

        request_id = params.get("request_id")
        if request_id:
            criteria.append(Q(request_id=request_id))

        severity = params.get("severity")
        if severity:
            criteria.append(Q(change_context__severity=severity))

        start = self._parse_dt(params.get("start_at"))
        end = self._parse_dt(params.get("end_at"))
        if start:
            criteria.append(Q(created_at__gte=start))
        if end:
            criteria.append(Q(created_at__lte=end))

        return criteria
