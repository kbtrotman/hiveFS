from __future__ import annotations

import bz2
import gzip
import lzma
import zipfile
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Mapping, Optional

__all__ = [
    "HIVE_DATA_DIR",
    "HIVE_LOG_DIR",
    "LOG_FILE_MAP",
    "LogManager",
    "LogError",
    "InvalidLogError",
    "LogNotFoundError",
]

HIVE_DATA_DIR = Path("/hive")
HIVE_LOG_DIR = HIVE_DATA_DIR / "logs"

LOG_FILE_MAP = {
    "hive_guard_log": HIVE_LOG_DIR / "hive_guard_log",
    "hive_bootstrap_log": HIVE_LOG_DIR / "hive_bootstrap_log",
    "hive_guard_sql_log": HIVE_LOG_DIR / "hive_guard_sql.log",
    "hive_guard.log": HIVE_LOG_DIR / "hive_guard.log",
    "hive_bootstrap.log": HIVE_LOG_DIR / "hive_bootstrap.log",
}

ARCHIVE_SUFFIXES = {".gz", ".zip", ".bz2", ".xz"}


class LogError(RuntimeError):
    """Base class for log related errors."""


class InvalidLogError(LogError, ValueError):
    """Raised when a user requests a log that is not registered."""


class LogNotFoundError(LogError, FileNotFoundError):
    """Raised when the requested log file or archive is missing."""


@dataclass(frozen=True)
class LogDescriptor:
    key: str
    path: Path

    @property
    def archive_prefixes(self) -> tuple[str, str]:
        name = self.path.name
        stem = self.path.stem
        return name, stem


class LogManager:
    """Helper for serving Hive log files to the API layer."""

    def __init__(self, log_dir: Path | str = HIVE_LOG_DIR, logs: Mapping[str, Path] | None = None):
        self.log_dir = Path(log_dir)
        log_map = logs or LOG_FILE_MAP
        self._logs: Dict[str, LogDescriptor] = {
            key: LogDescriptor(key=key, path=Path(path)) for key, path in log_map.items()
        }

    def list_logs(self) -> List[str]:
        return sorted(self._logs.keys())

    def get_current_log(self, key: str, *, max_bytes: Optional[int] = None, encoding: str = "utf-8") -> str:
        descriptor = self._require_log(key)
        if not descriptor.path.exists():
            raise LogNotFoundError(f"Log file '{descriptor.path}' not found")
        if max_bytes is None or max_bytes <= 0:
            data = descriptor.path.read_bytes()
        else:
            data = self._tail_bytes(descriptor.path, max_bytes)
        try:
            return data.decode(encoding, errors="replace")
        finally:
            del data

    def download_current_log(self, key: str) -> bytes:
        descriptor = self._require_log(key)
        if not descriptor.path.exists():
            raise LogNotFoundError(f"Log file '{descriptor.path}' not found")
        return descriptor.path.read_bytes()

    def list_archives(self, key: str) -> List[str]:
        descriptor = self._require_log(key)
        archives = [candidate.name for candidate in self._iter_archives(descriptor)]
        return sorted(archives, reverse=True)

    def view_archive(self, key: str, archive_name: str, *, encoding: str = "utf-8") -> str:
        data = self._read_archive_bytes(key, archive_name)
        try:
            return data.decode(encoding, errors="replace")
        finally:
            del data

    def download_archive(self, key: str, archive_name: str) -> bytes:
        return self._read_archive_bytes(key, archive_name)

    def _require_log(self, key: str) -> LogDescriptor:
        descriptor = self._logs.get(key)
        if descriptor is None:
            raise InvalidLogError(f"Unknown log '{key}'. Valid options: {', '.join(self.list_logs())}")
        return descriptor

    def _iter_archives(self, descriptor: LogDescriptor) -> Iterable[Path]:
        if not self.log_dir.exists():
            return []
        name, stem = descriptor.archive_prefixes
        for candidate in self.log_dir.iterdir():
            if candidate == descriptor.path or not candidate.is_file():
                continue
            if candidate.suffix.lower() not in ARCHIVE_SUFFIXES:
                continue
            candidate_name = candidate.name
            if candidate_name.startswith(name) or candidate_name.startswith(stem):
                yield candidate

    def _read_archive_bytes(self, key: str, archive_name: str) -> bytes:
        descriptor = self._require_log(key)
        archive_path = (self.log_dir / archive_name).resolve(strict=False)
        base_dir = self.log_dir.resolve(strict=False)
        try:
            archive_path.relative_to(base_dir)
        except ValueError as exc:
            raise InvalidLogError("Archive path must stay within the log directory") from exc
        if not archive_path.exists():
            raise LogNotFoundError(f"Archive '{archive_name}' not found")
        valid = False
        for prefix in descriptor.archive_prefixes:
            if archive_path.name.startswith(prefix):
                valid = True
                break
        if not valid:
            raise InvalidLogError(
                f"Archive '{archive_name}' does not belong to log '{key}'."
            )
        return self._decompress_file(archive_path)

    def _decompress_file(self, path: Path) -> bytes:
        suffix = path.suffix.lower()
        if suffix == ".gz":
            with gzip.open(path, "rb") as gz:
                return gz.read()
        if suffix == ".zip":
            with zipfile.ZipFile(path) as zf:
                for info in zf.infolist():
                    if info.is_dir():
                        continue
                    return zf.read(info)
                raise LogNotFoundError(f"Archive '{path}' is empty")
        if suffix == ".bz2":
            with bz2.open(path, "rb") as bz:
                return bz.read()
        if suffix == ".xz":
            with lzma.open(path, "rb") as lz:
                return lz.read()
        return path.read_bytes()

    def _tail_bytes(self, path: Path, max_bytes: int) -> bytes:
        with path.open("rb") as handle:
            handle.seek(0, 2)
            size = handle.tell()
            offset = max(size - max_bytes, 0)
            handle.seek(offset)
            return handle.read()
