import json
import socket

SOCK_PATH = "/run/hive_bootstrap.sock"
MAX_RESPONSE = 4096

BOOTSTRAP_MAX_TRIES = 3
BOOTSTRAP_STATE_FIELDS = (
    "cluster_state",
    "database_state",
    "cont1_state",
    "cont2_state",
)
CRITICAL_STATE_FIELDS = BOOTSTRAP_STATE_FIELDS[1:]
UNCONFIGURED_STATE = "unconfigured"


class BootstrapError(Exception):
    pass


def _normalize_payload(payload: dict | None) -> dict | None:
    if payload is None:
        return None
    if not isinstance(payload, dict):
        raise BootstrapError("bootstrap payload must be a JSON object")
    normalized = dict(payload)
    missing = [field for field in BOOTSTRAP_STATE_FIELDS if field not in normalized]
    if missing:
        raise BootstrapError(
            f"missing bootstrap field(s): {', '.join(sorted(missing))}"
        )
    return normalized


def _needs_component_configuration(payload: dict | None) -> bool:
    if not isinstance(payload, dict):
        return False
    for field in CRITICAL_STATE_FIELDS:
        value = payload.get(field)
        if isinstance(value, str) and value.lower() == UNCONFIGURED_STATE:
            return True
    return False


def _call_with_reconfigure(op: str,
                           payload: dict,
                           timeout: float) -> dict:
    working_payload = dict(payload)
    for attempt in range(1, BOOTSTRAP_MAX_TRIES + 1):
        working_payload["bootstrap_try"] = attempt
        _send_bootstrap_command(op, working_payload, timeout)
        status = _send_bootstrap_command("status", None, timeout)
        if not _needs_component_configuration(status):
            return status
    raise BootstrapError(
        "bootstrap failed: database or containers remain unconfigured"
    )


def _recv_line(sock: socket.socket, limit: int = MAX_RESPONSE) -> str:
    buf = bytearray()
    while len(buf) < limit:
        chunk = sock.recv(min(1024, limit - len(buf)))
        if not chunk:
            break
        buf.extend(chunk)
        if b"\n" in chunk or b"\r" in chunk:
            break
    if not buf:
        raise BootstrapError("empty response from bootstrap socket")
    line = bytes(buf).splitlines()[0].decode("utf-8", errors="replace").strip()
    if not line:
        raise BootstrapError("blank response from bootstrap socket")
    return line


def _send_bootstrap_command(op: str,
                            args: dict | None,
                            timeout: float) -> dict:
    if not op:
        raise BootstrapError("command is required")
    payload = {"command": op}
    if args:
        payload.update(args)
    data = json.dumps(payload, separators=(",", ":")).encode("utf-8") + b"\n"

    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.settimeout(timeout)
    try:
        sock.connect(SOCK_PATH)
        sock.sendall(data)
        response = _recv_line(sock)
    except OSError as exc:
        raise BootstrapError(f"bootstrap socket error: {exc}") from exc
    finally:
        sock.close()

    if response.startswith("OK"):
        message = response[2:].strip()
        if not message:
            return {"ok": True}
        try:
            decoded = json.loads(message)
        except json.JSONDecodeError:
            return {"ok": True, "message": message}
        if isinstance(decoded, dict):
            return decoded
        return {"ok": True, "result": decoded}
    if response.startswith("ERR"):
        detail = response[3:].strip() or "bootstrap daemon error"
        raise BootstrapError(detail)
    raise BootstrapError(f"unexpected bootstrap response: {response}")


def call_bootstrap(op: str,
                   args: dict | None = None,
                   timeout: float = 5.0) -> dict:
    payload = _normalize_payload(args)
    if payload is not None and _needs_component_configuration(payload):
        return _call_with_reconfigure(op, payload, timeout)
    return _send_bootstrap_command(op, payload, timeout)


def call_addnode(op: str,
                 args: dict | None = None,
                 timeout: float = 5.0) -> dict:
    return call_bootstrap(op, args, timeout)
