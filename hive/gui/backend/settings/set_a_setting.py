import json
import socket
import sys
from typing import Any, Tuple

SOCK_PATH = "/run/hive_bootstrap.sock"
MAX_RESPONSE_BYTES = 4096
DEFAULT_TIMEOUT = 5.0


def main(argv: list[str]) -> int:
    try:
        setting_name, setting_value = _parse_inputs(argv)
    except ValueError as exc:
        print(f"set_a_setting: {exc}", file=sys.stderr)
        return 2

    payload = {
        "command": "update_setting",
        "setting_name": setting_name,
        "setting_value": setting_value,
    }

    try:
        response = _send_payload(payload, DEFAULT_TIMEOUT)
    except RuntimeError as exc:
        print(f"set_a_setting: {exc}", file=sys.stderr)
        return 1

    if response:
        print(response)
    return 0


def _parse_inputs(argv: list[str]) -> Tuple[str, Any]:
    argc = len(argv)
    if argc == 2:
        return _validate_payload(argv[0], _loads(argv[1], "setting_value"))
    if argc == 1:
        raw = _loads(argv[0], "payload")
        return _coerce_payload(raw)
    if argc == 0 and not sys.stdin.isatty():
        raw = sys.stdin.read()
        return _coerce_payload(_loads(raw, "payload"))
    raise ValueError("usage: set_a_setting.py <setting_name> <setting_value_json> or JSON payload")


def _loads(text: str, context: str) -> Any:
    try:
        return json.loads(text)
    except json.JSONDecodeError as exc:
        raise ValueError(f"{context} must be valid JSON") from exc


def _validate_payload(setting_name: str, setting_value: Any) -> Tuple[str, Any]:
    name = str(setting_name).strip()
    if not name:
        raise ValueError("setting_name is required")
    return name, setting_value


def _coerce_payload(data: Any) -> Tuple[str, Any]:
    if not isinstance(data, dict):
        raise ValueError("payload must be a JSON object")
    if "setting_name" not in data:
        raise ValueError("setting_name is required")
    if "setting_value" not in data:
        raise ValueError("setting_value is required")
    return _validate_payload(data["setting_name"], data["setting_value"])


def _send_payload(payload: dict[str, Any], timeout: float) -> str:
    data = json.dumps(payload, separators=(",", ":")).encode("utf-8") + b"\n"
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
        sock.settimeout(timeout)
        try:
            sock.connect(SOCK_PATH)
            sock.sendall(data)
            response = _recv_line(sock)
        except OSError as exc:
            raise RuntimeError(f"bootstrap socket error: {exc}") from exc

    if response.startswith("ERR"):
        detail = response[3:].strip() or "bootstrap daemon error"
        raise RuntimeError(detail)
    if response.startswith("OK"):
        return response[2:].strip()
    return response


def _recv_line(sock: socket.socket) -> str:
    buf = bytearray()
    recv = sock.recv
    while len(buf) < MAX_RESPONSE_BYTES:
        chunk = recv(min(1024, MAX_RESPONSE_BYTES - len(buf)))
        if not chunk:
            break
        buf.extend(chunk)
        if b"\n" in chunk or b"\r" in chunk:
            break
    if not buf:
        raise RuntimeError("empty response from bootstrap socket")
    line = bytes(buf).splitlines()[0].decode("utf-8", errors="replace").strip()
    if not line:
        raise RuntimeError("blank response from bootstrap socket")
    return line


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
