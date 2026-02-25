import json
import logging
import subprocess
import sys
from pathlib import Path

from django.core.serializers.json import DjangoJSONEncoder
from django.db import transaction
from rest_framework.exceptions import ValidationError
from rest_framework.response import Response
from rest_framework.viewsets import ViewSet

from .models import Settings
from .serializers import SettingsSerializer

logger = logging.getLogger(__name__)
SET_SCRIPT = Path(__file__).with_name("set_a_setting.py")
READONLY_FIELDS = {"settings_id", "created_at", "updated_at"}
MODEL_FIELDS = {
    field.name
    for field in Settings._meta.get_fields()
    if getattr(field, "concrete", False) and not getattr(field, "many_to_many", False)
}
MUTABLE_FIELDS = MODEL_FIELDS - READONLY_FIELDS


class SettingsRootView(ViewSet):
    serializer_class = SettingsSerializer
    http_method_names = ["get", "post", "options", "head"]

    def list(self, request):
        instance = self._fetch_settings()
        serializer = self.serializer_class(instance)
        names = self._requested_names(request, include_readonly=True)
        data = serializer.data
        if names is None:
            return Response(data)
        return Response({name: data[name] for name in names})

    def create(self, request):
        payload = self._coerce_payload(request.data)
        requested_names = self._requested_names(request, include_readonly=False)
        if requested_names is not None:
            missing = [name for name in requested_names if name not in payload]
            if missing:
                raise ValidationError({"setting_name": f"Missing values for {', '.join(missing)}."})
            extra = [key for key in payload if key not in requested_names]
            if extra:
                raise ValidationError({
                    "setting_name": f"Unexpected settings in body: {', '.join(extra)}."
                })
            target_payload = {name: payload[name] for name in requested_names}
        else:
            target_payload = payload

        if not target_payload:
            raise ValidationError({"detail": "Provide at least one setting to update."})

        invalid = sorted(set(target_payload) - MUTABLE_FIELDS)
        if invalid:
            raise ValidationError({"setting_name": f"Unsupported settings: {', '.join(invalid)}."})

        instance = self._fetch_settings()
        serializer = self.serializer_class(instance, data=target_payload, partial=True)
        serializer.is_valid(raise_exception=True)

        changes = self._collect_changes(instance, serializer.validated_data)
        if not changes:
            return self._build_response(instance, requested_names)

        self._apply_via_raft(changes)
        with transaction.atomic():
            instance = serializer.save()
        return self._build_response(instance, requested_names)

    def _fetch_settings(self) -> Settings:
        instance = Settings.objects.order_by("settings_id").first()
        if instance is None:
            instance = Settings.objects.create()
        return instance

    def _coerce_payload(self, data):
        if hasattr(data, "dict") and not isinstance(data, dict):
            data = data.dict()
        if not isinstance(data, dict):
            raise ValidationError({"detail": "Request body must be a JSON object."})
        return data

    def _requested_names(self, request, *, include_readonly: bool):
        raw_values = request.query_params.getlist("setting_name")
        if not raw_values:
            return None
        allowed = MODEL_FIELDS if include_readonly else MUTABLE_FIELDS
        names: list[str] = []
        for raw in raw_values:
            for name in (part.strip() for part in raw.split(",")):
                if not name:
                    continue
                if name not in allowed:
                    raise ValidationError({"setting_name": f"Unknown setting '{name}'."})
                if name not in names:
                    names.append(name)
        if not names:
            return None
        return names

    def _collect_changes(self, instance: Settings, validated_data):
        changes = {}
        for key, value in validated_data.items():
            if getattr(instance, key) != value:
                changes[key] = value
        return changes

    def _apply_via_raft(self, changes: dict):
        if not changes:
            return
        if not SET_SCRIPT.exists():
            raise ValidationError({"detail": "set_a_setting.py script is missing."})
        for name, value in changes.items():
            serialized = json.dumps(value, cls=DjangoJSONEncoder, separators=(",", ":"))
            try:
                result = subprocess.run(
                    [sys.executable, str(SET_SCRIPT), name, serialized],
                    capture_output=True,
                    text=True,
                    check=False,
                    timeout=15,
                )
            except subprocess.TimeoutExpired as exc:
                logger.error("set_a_setting timed out for %s", name)
                raise ValidationError({"detail": f"Timed out while applying {name}."}) from exc
            if result.returncode != 0:
                stderr = (result.stderr or result.stdout or "set_a_setting failed").strip()
                logger.error("set_a_setting failed for %s: %s", name, stderr)
                raise ValidationError({"detail": f"Failed to apply {name}: {stderr}"})

    def _build_response(self, instance: Settings, names):
        serializer = self.serializer_class(instance)
        data = serializer.data
        if not names:
            return Response(data)
        return Response({name: data[name] for name in names})
