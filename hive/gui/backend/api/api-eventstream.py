#!/usr/bin/env python3
import argparse
import json
import logging
import os
import socket
import sys
import time
from pathlib import Path


MAX_RESPONSE = 4096
SOCK_PATH = "/run/hive_bootstrap.sock"
DEFAULT_INTERVAL = 5.0
DEFAULT_TIMEOUT = 5.0
DEFAULT_IDLE_TICKS = 2
DEFAULT_MAX_DURATION = 900.0
EVENT_CHANNEL_PREFIX = "job-"
BACKEND_ROOT = Path(__file__).resolve().parents[1]


def setup_django():
    if str(BACKEND_ROOT) not in sys.path:
        sys.path.insert(0, str(BACKEND_ROOT))
    os.environ.setdefault("DJANGO_SETTINGS_MODULE", "hive.settings")
    import django  # type: ignore

    django.setup()
    from django_eventstream import send_event  # type: ignore

    return send_event


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
        raise RuntimeError("empty response from bootstrap socket")
    line = bytes(buf).splitlines()[0].decode("utf-8", errors="replace").strip()
    if not line:
        raise RuntimeError("blank response from bootstrap socket")
    return line


def fetch_status(timeout: float) -> dict:
    payload = {"command": "status"}
    data = json.dumps(payload, separators=(",", ":")).encode("utf-8") + b"\n"

    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.settimeout(timeout)
    try:
        sock.connect(SOCK_PATH)
        sock.sendall(data)
        response = _recv_line(sock)
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
        return decoded if isinstance(decoded, dict) else {"ok": True, "result": decoded}

    if response.startswith("ERR"):
        detail = response[3:].strip() or "bootstrap daemon error"
        raise RuntimeError(detail)
    raise RuntimeError(f"unexpected bootstrap response: {response}")


def should_stop(status: dict) -> bool:
    state = (status.get("config_status") or "").upper()
    msg = (status.get("config_msg") or "").lower()
    if state in {"IN_ERROR", "INVALID_OP"}:
        return True
    if state == "IDLE" and "processing" not in msg:
        return True
    return False


def poll_job(job_id: str,
             send_event,
             interval: float,
             timeout: float,
             idle_ticks: int,
             max_duration: float) -> None:
    logger = logging.getLogger("api-eventstream")
    start = time.monotonic()
    idle_seen = 0
    logger.info("job %s poller started interval=%s timeout=%s", job_id, interval, timeout)

    while True:
        try:
            status = fetch_status(timeout)
        except Exception as exc:  # pylint: disable=broad-except
            logger.warning("job %s status poll failed: %s", job_id, exc)
            status = {"ok": False, "error": str(exc)}

        payload = {
            "job_id": job_id,
            "status": status.get("config_status"),
            "progress": status.get("config_progress"),
            "msg": status.get("config_msg"),
            "raw": status,
        }
        try:
            send_event(f"{EVENT_CHANNEL_PREFIX}{job_id}", "log", payload)
            logger.debug("job %s event emitted status=%s", job_id, payload.get("status"))
        except Exception as exc:  # pylint: disable=broad-except
            logger.warning("job %s event emit failed: %s", job_id, exc)

        if should_stop(status):
            idle_seen += 1
        else:
            idle_seen = 0

        if idle_seen >= idle_ticks:
            logger.info("job %s stopping after idle ticks=%s", job_id, idle_seen)
            break
        if time.monotonic() - start >= max_duration:
            logger.info("job %s stopping after max_duration reached")
            break
        time.sleep(interval)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="Bootstrap job eventstream poller")
    parser.add_argument("--job-id", required=True, help="Job identifier to monitor")
    parser.add_argument("--interval", type=float, default=DEFAULT_INTERVAL)
    parser.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT)
    parser.add_argument("--idle-ticks", type=int, default=DEFAULT_IDLE_TICKS,
                        help="Number of idle polls before exiting")
    parser.add_argument("--max-duration", type=float, default=DEFAULT_MAX_DURATION,
                        help="Maximum seconds to poll before stopping")
    parser.add_argument("--log-file", help="Optional file to append worker logs")
    args = parser.parse_args(argv)

    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s api-eventstream %(levelname)s %(message)s",
        filename=args.log_file,
        filemode="a" if args.log_file else None,
    )
    send_event = setup_django()
    poll_job(
        args.job_id,
        send_event,
        args.interval,
        args.timeout,
        args.idle_ticks,
        args.max_duration,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
