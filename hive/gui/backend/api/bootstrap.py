import json
import socket

SOCK_PATH = "/run/hive_bootstrap.sock"
MAX_RESPONSE = 4096

BOOTSTRAP_MAX_TRIES = 3
BOOTSTRAP_STATE_FIELDS = (
    "cluster_state",
    "database_state",
    "kv_state",
    "cont1_state",
    "cont2_state",
)
CRITICAL_STATE_FIELDS = tuple(field for field in BOOTSTRAP_STATE_FIELDS
                              if field != "cluster_state")
UNCONFIGURED_STATE = "unconfigured"
PAYLOAD_TEMPLATE = {
    "node_id": None,
    "cluster_id": None,
    "cluster_state": UNCONFIGURED_STATE,
    "node_state": UNCONFIGURED_STATE,
    "cluster_name": None,
    "cluster_desc": None,
    "database_state": "configured",
    "kv_state": "configured",
    "cont1_state": "configured",
    "cont2_state": "configured",
    "min_nodes_req": 4,
    "bootstrap_token": None,
    "first_boot_ts": "",
    "hive_version": None,
    "hive_patch_level": None,
    "config_status": None,
    "config_progress": None,
    "config_msg": None,
    "last_attempt_status": None,
    "last_attempt_progress": None,
    "last_attempt_msg": None,
    "num_of_attempts_this_stage": 0,
    "stage_of_config": 0,
    "ready_4_web_conf": None,
    "pub_key": None,
    "user_id": None,
    "command": "status",
    "ok": True,
}


class BootstrapError(Exception):
    pass


def _normalize_payload(payload: dict | None) -> dict | None:
    if payload is None:
        return None
    if not isinstance(payload, dict):
        raise BootstrapError("bootstrap payload must be a JSON object")
    normalized = dict(PAYLOAD_TEMPLATE)
    normalized.update(payload)
    return normalized


def _normalize_response(response: dict | None) -> dict | None:
    if response is None:
        return None
    if not isinstance(response, dict):
        return response
    normalized = dict(PAYLOAD_TEMPLATE)
    normalized.update(response)
    if "config_progress" not in response and "percent" in response:
        percent = response.get("percent")
        if isinstance(percent, (int, float)):
            normalized["config_progress"] = f"{int(percent)}%"
        elif isinstance(percent, str):
            normalized["config_progress"] = percent
    if "command" not in normalized and "status" in normalized:
        normalized["command"] = "status"
    normalized.pop("percent", None)
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
            return _normalize_response(status)
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
        result = _call_with_reconfigure(op, payload, timeout)
    else:
        result = _send_bootstrap_command(op, payload, timeout)
    return _normalize_response(result)


def call_addnode(op: str,
                 args: dict | None = None,
                 timeout: float = 5.0) -> dict:
    return call_bootstrap(op, args, timeout)

def call_foreigner(op: str,
                 args: dict | None = None,
                 timeout: float = 5.0) -> dict:
    return call_bootstrap(op, args, timeout)
