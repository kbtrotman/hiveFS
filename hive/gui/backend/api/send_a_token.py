from __future__ import annotations

import logging
import os
import re
import subprocess
import threading
from email.message import EmailMessage
from typing import Any, Dict, Tuple

from .newtoken import TokenRequestError, request_newtoken


logger = logging.getLogger(__name__)

REQUIRED_COMMAND = "newtoken"
MAX_EMAIL_LENGTH = 320
EMAIL_PATTERN = re.compile(
    r"^[A-Za-z0-9.!#$%&'*+/=?^_`{|}~-]+@"
    r"[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?"
    r"(?:\.[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?)*$"
)
DEFAULT_EMAIL_SENDER = os.environ.get("HIVE_TOKEN_EMAIL_FROM", "hive@localhost")
SENDMAIL_PATH = os.environ.get("SENDMAIL", "/usr/sbin/sendmail")


class SendTokenError(TokenRequestError):
    """Raised when a token-send request is invalid."""


def send_a_token(payload: Dict[str, Any], timeout: float = 5.0) -> Dict[str, Any]:
    """
    Validate a gen_token request, relay it to hive_guard, and email the result.
    """
    normalized_payload, recipient = _normalize_request(payload)
    token_response = request_newtoken(normalized_payload, timeout=timeout)
    _dispatch_email(recipient, token_response)
    return token_response


def _normalize_request(payload: Dict[str, Any]) -> Tuple[Dict[str, Any], str]:
    if not isinstance(payload, dict):
        raise SendTokenError("payload must be a JSON object")
    working = dict(payload)

    command = working.pop("command", None)
    if not isinstance(command, str) or command.strip().lower() != REQUIRED_COMMAND:
        raise SendTokenError("command must be 'newtoken'")

    email_value = working.pop("rec_email", None)
    recipient = _normalize_email(email_value)
    return working, recipient


def _normalize_email(value: Any) -> str:
    if isinstance(value, (bytes, bytearray)):
        try:
            value = value.decode("utf-8")
        except UnicodeDecodeError as exc:
            raise SendTokenError("rec_email must be UTF-8 text") from exc
    if not isinstance(value, str):
        raise SendTokenError("rec_email is required")
    candidate = value.strip()
    if not candidate:
        raise SendTokenError("rec_email is required")
    if len(candidate) > MAX_EMAIL_LENGTH:
        raise SendTokenError("rec_email is too long")
    if not EMAIL_PATTERN.fullmatch(candidate):
        raise SendTokenError("rec_email is invalid")
    return candidate


def _dispatch_email(recipient: str, token_payload: Dict[str, Any]) -> None:
    thread = threading.Thread(
        target=_send_email_safe,
        args=(recipient, token_payload),
        name="token-mailer",
        daemon=True,
    )
    thread.start()


def _send_email_safe(recipient: str, token_payload: Dict[str, Any]) -> None:
    try:
        message = _build_email_message(recipient, token_payload)
        _send_via_postfix(message)
    except Exception:  # pylint: disable=broad-except
        logger.exception("failed to send bootstrap token email to %s", recipient)


def _build_email_message(recipient: str, token_payload: Dict[str, Any]) -> EmailMessage:
    token = token_payload.get("bootstrap_token") or "<unknown>"
    token_type = token_payload.get("token_type") or token_payload.get("t_type") or "unknown"
    host_mid = token_payload.get("host_mid") or "unknown host"
    issued_at = token_payload.get("issued_at") or "unknown"
    expires_at = token_payload.get("expires_at") or "unknown"

    body = (
        "A new Hive bootstrap token has been generated.\n\n"
        f"Token     : {token}\n"
        f"Type      : {token_type}\n"
        f"Machine   : {host_mid}\n"
        f"Issued At : {issued_at}\n"
        f"Expires At: {expires_at}\n\n"
        "Use this token on the target machine to join it to the storage cluster.\n"
    )

    message = EmailMessage()
    message["To"] = recipient
    message["From"] = _determine_sender()
    message["Subject"] = "Hive bootstrap token"
    message.set_content(body)
    return message


def _determine_sender() -> str:
    try:
        from django.conf import settings  # type: ignore

        return (
            getattr(settings, "TOKEN_SENDER_EMAIL", None)
            or getattr(settings, "DEFAULT_FROM_EMAIL", DEFAULT_EMAIL_SENDER)
        )
    except Exception:  # pylint: disable=broad-except
        return DEFAULT_EMAIL_SENDER


def _send_via_postfix(message: EmailMessage) -> None:
    if not SENDMAIL_PATH:
        raise SendTokenError("sendmail path is not configured")
    try:
        proc = subprocess.run(
            [SENDMAIL_PATH, "-t", "-oi"],
            input=message.as_bytes(),
            check=False,
        )
    except OSError as exc:
        raise SendTokenError(f"sendmail failed: {exc}") from exc
    if proc.returncode != 0:
        raise SendTokenError(f"sendmail exited with status {proc.returncode}")


__all__ = ["send_a_token", "SendTokenError"]
