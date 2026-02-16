import json
import socket
from datetime import datetime, timedelta, timezone
from typing import Any, Dict

try:
    from .bootstrap import BootstrapError
except ImportError:  # pragma: no cover - fallback for script-style imports
    from bootstrap import BootstrapError

SOCK_PATH = "/run/hive_bootstrap.sock"
MAX_REQUEST_BYTES = 2048
MAX_RESPONSE_BYTES = 4096
MAX_STRING_LENGTH = 256
MAX_TIMESTAMP_LENGTH = 32
TOKEN_LENGTH = 43
_HEX_DIGITS = frozenset("0123456789abcdef")
_UUID_VARIANT_CHARS = frozenset("89ab")
_UUID_HYPHEN_INDEXES = frozenset((8, 13, 18, 23))
_UUID_TEXT_LENGTH = 36
ALLOWED_T_TYPES = {
    "cluster_join",
    "node_join",
    "client_join",
}
REQUIRED_FIELDS = ("host_mid", "approved_by", "t_type")
TIMESTAMP_FIELDS = (
    "first_boot_ts",
    "issued_at",
    "expires_at",
    "approved_at",
)


class TokenRequestError(BootstrapError):
    """Raised when a new-token request cannot be fulfilled."""

def request_newtoken(payload: Dict[str, Any], timeout: float = 5.0) -> Dict[str, Any]:
    normalized = _prepare_payload(payload)
    response_line = _send_payload(normalized, timeout)
    return _parse_response(response_line)


def _prepare_payload(payload: Dict[str, Any]) -> Dict[str, Any]:
    if not isinstance(payload, dict):
        raise TokenRequestError("token request must be a JSON object")
    form_data = payload.pop("form", None)
    if form_data:
        if isinstance(form_data, str):
            try:
                form_data = json.loads(form_data)
            except json.JSONDecodeError as exc:
                raise TokenRequestError("form must be valid JSON") from exc
        if not isinstance(form_data, dict):
            raise TokenRequestError("form must be a JSON object")
        cluster_id = form_data.get("cluster_id")
        if cluster_id not in (None, ""):
            payload["cluster_id"] = cluster_id
        first_boot_ts = form_data.get("first_boot_ts")
        if first_boot_ts not in (None, ""):
            payload["first_boot_ts"] = first_boot_ts
    now = datetime.now(timezone.utc)
    default_payload: Dict[str, Any] = {
        "command": "newtoken",
        "cluster_id": None,
        "bootstrap_token": None,
        "first_boot_ts": _format_timestamp(now),
        "tid": None,
        "t_type": None,
        "host_mid": None,
        "issued_at": None,
        "expires_at": None,
        "approved_at": None,
        "approved_by": None,
    }

    for key in default_payload:
        if key == "command":
            continue
        if key in payload:
            default_payload[key] = payload[key]

    default_payload["cluster_id"] = _normalize_cluster_id(default_payload.get("cluster_id"))
    default_payload["bootstrap_token"] = None
    default_payload["tid"] = _normalize_optional_string(
        default_payload.get("tid"), "tid", MAX_STRING_LENGTH
    )
    default_payload["t_type"] = _normalize_t_type(default_payload.get("t_type"))
    default_payload["host_mid"] = _normalize_required_string(
        default_payload.get("host_mid"), "host_mid"
    )
    default_payload["approved_by"] = _normalize_required_string(
        default_payload.get("approved_by"), "approved_by"
    )

    for field in TIMESTAMP_FIELDS:
        default_payload[field] = _normalize_timestamp(
            default_payload.get(field), field
        )

    _enforce_length(default_payload)
    return default_payload


def _normalize_cluster_id(value: Any) -> str | None:
    if value in (None, ""):
        return None
    if isinstance(value, (bytes, bytearray)):
        try:
            value = value.decode("utf-8")
        except UnicodeDecodeError as exc:
            raise TokenRequestError("cluster_id must be a UUID string or null") from exc
    candidate = value.strip().lower() if isinstance(value, str) else str(value).strip().lower()
    if not candidate:
        return None
    if _is_uuid_v4(candidate):
        return candidate
    raise TokenRequestError("api error")


def _is_uuid_v4(text: str) -> bool:
    if len(text) != _UUID_TEXT_LENGTH:
        return False
    for idx, ch in enumerate(text):
        if idx in _UUID_HYPHEN_INDEXES:
            if ch != "-":
                return False
        elif ch not in _HEX_DIGITS:
            return False
    if text[14] != "4":
        return False
    if text[19] not in _UUID_VARIANT_CHARS:
        return False
    return True


def _normalize_optional_string(value: Any, field: str, max_len: int) -> str | None:
    if value in (None, ""):
        return None
    if isinstance(value, (int, float)):
        value = str(value)
    if not isinstance(value, str):
        raise TokenRequestError(f"{field} must be a string")
    value = value.strip()
    if len(value) > max_len:
        raise TokenRequestError(f"{field} is too long")
    return value

def _normalize_required_string(value: Any, field: str) -> str:
    normalized = _normalize_optional_string(value, field, MAX_STRING_LENGTH)
    if not normalized:
        raise TokenRequestError(f"{field} is required")
    return normalized


def _normalize_t_type(value: Any) -> str:
    normalized = _normalize_required_string(value, "t_type")
    lowered = normalized.lower()
    if lowered not in ALLOWED_T_TYPES:
        raise TokenRequestError("t_type is invalid")
    return lowered


def _normalize_timestamp(value: Any, field: str) -> str:
    if value in (None, ""):
        return None
    if isinstance(value, (int, float)):
        value = str(value)
    if not isinstance(value, str):
        raise TokenRequestError(f"{field} must be a string")
    value = value.strip()
    if len(value) > MAX_TIMESTAMP_LENGTH:
        raise TokenRequestError(f"{field} must be <= {MAX_TIMESTAMP_LENGTH} characters")
    if "T" not in value or not value.endswith("Z"):
        raise TokenRequestError(f"{field} must be an ISO-8601 UTC string")
    return value


def _enforce_length(payload: Dict[str, Any]) -> None:
    encoded = json.dumps(payload, separators=(",", ":")).encode("utf-8")
    if len(encoded) >= MAX_REQUEST_BYTES:
        raise TokenRequestError("token request payload exceeds size limit")


def _format_timestamp(dt: datetime) -> str:
    return dt.replace(microsecond=0).isoformat().replace("+00:00", "Z")


def _send_payload(payload: Dict[str, Any], timeout: float) -> str:
    data = json.dumps(payload, separators=(",", ":")).encode("utf-8") + b"\n"
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.settimeout(timeout)
    try:
        sock.connect(SOCK_PATH)
        sock.sendall(data)
        return _recv_line(sock)
    except OSError as exc:
        raise TokenRequestError(f"bootstrap socket error: {exc}") from exc
    finally:
        sock.close()


def _recv_line(sock: socket.socket) -> str:
    buf = bytearray()
    while len(buf) < MAX_RESPONSE_BYTES:
        chunk = sock.recv(min(1024, MAX_RESPONSE_BYTES - len(buf)))
        if not chunk:
            break
        buf.extend(chunk)
        if b"\n" in chunk or b"\r" in chunk:
            break
    if not buf:
        raise TokenRequestError("empty response from bootstrap socket")
    line = bytes(buf).splitlines()[0].decode("utf-8", errors="replace").strip()
    if not line:
        raise TokenRequestError("blank response from bootstrap socket")
    return line


def _parse_response(response_line: str) -> Dict[str, Any]:
    if response_line.startswith("ERR"):
        detail = response_line[3:].strip() or "bootstrap daemon error"
        raise TokenRequestError(detail)
    try:
        decoded = json.loads(response_line)
    except json.JSONDecodeError as exc:
        raise TokenRequestError("invalid JSON response from bootstrap") from exc
    if not isinstance(decoded, dict):
        raise TokenRequestError("bootstrap response must be a JSON object")
    if decoded.get("command") != "newtoken":
        raise TokenRequestError("unexpected bootstrap response")
    token = decoded.get("bootstrap_token")
    if not isinstance(token, str) or len(token) != TOKEN_LENGTH:
        raise TokenRequestError("bootstrap_token is invalid")
    return decoded
