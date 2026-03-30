#!/usr/bin/env python3
"""
Rotate HiveFS logs once per day.

Behavior:
- At each run, rotate configured active logs into a dated archive directory.
- Compress the rotated logs from the last completed period into a .tar.gz archive.
- Truncate/recreate the active log files so services can continue writing to the same paths.
- Retain archives for 7 days and delete older archives.

Intended usage:
- Run from cron shortly after midnight, e.g. 00:05.
- Default period label is yesterday's date, since the previous 24 hours have completed.

Example cron:
    5 0 * * * /usr/bin/python3 /usr/local/sbin/hive_log_rotate.py >> /var/log/hive_log_rotate.log 2>&1

Notes:
- This script truncates files in place after copying them into the archive. That avoids changing
  the inode the running process is writing to, which is usually safer than rename-only rotation
  when apps do not reopen their log files.
- If you prefer signal-based reopen semantics, adapt the script accordingly.
"""

from __future__ import annotations

import argparse
import gzip
import hashlib
import os
from pathlib import Path
import shutil
import sys
import tarfile
from datetime import datetime, timedelta, timezone
from typing import Iterable, List

DEFAULT_LOGS = [
    "/hive/logs/hive_bootstrap_log",
    "/hive/logs/hive_guard_log",
    "/hive/logs/hive_guard_sql_log",
    "/hive/wblogs/hive_mcl/mcl.log",
    "/hive/wblogs/hive_wbl/wbl.log",
]

DEFAULT_ARCHIVE_ROOT = "/hive/logs/archive"
DEFAULT_RETENTION_DAYS = 7
LOCK_FILE = "/var/run/hive_log_rotate.lock"


def utc_now() -> datetime:
    return datetime.now(timezone.utc)


def local_now() -> datetime:
    # Use local system time; cron typically runs in local time.
    return datetime.now().astimezone()


def ensure_parent(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)


def compute_sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def copy_and_truncate(src: Path, dest: Path) -> None:
    """
    Copy current log content to dest, then truncate src in place.
    This preserves the source inode for long-running writers.
    """
    ensure_parent(dest)
    with src.open("rb") as rf, dest.open("wb") as wf:
        shutil.copyfileobj(rf, wf, length=1024 * 1024)
        wf.flush()
        os.fsync(wf.fileno())

    # Truncate in place, preserving inode and permissions.
    with src.open("r+b") as f:
        f.truncate(0)
        f.flush()
        os.fsync(f.fileno())


def create_empty_if_missing(path: Path) -> None:
    ensure_parent(path)
    if not path.exists():
        path.touch(mode=0o644)


def archive_name_for_day(day: datetime) -> str:
    return day.strftime("%Y-%m-%d") + ".tar.gz"


def manifest_name_for_day(day: datetime) -> str:
    return day.strftime("%Y-%m-%d") + ".sha256"


def rotate_logs(log_paths: Iterable[Path], archive_root: Path, period_day: datetime, verbose: bool = False) -> tuple[Path, List[Path]]:
    day_str = period_day.strftime("%Y-%m-%d")
    staging_dir = archive_root / day_str
    staging_dir.mkdir(parents=True, exist_ok=True)

    rotated_files: List[Path] = []

    for log_path in log_paths:
        create_empty_if_missing(log_path)
        if verbose:
            print(f"Rotating {log_path}")

        dest = staging_dir / log_path.name
        copy_and_truncate(log_path, dest)
        rotated_files.append(dest)

    return staging_dir, rotated_files


def compress_staging_dir(staging_dir: Path, archive_root: Path, period_day: datetime, verbose: bool = False) -> Path:
    archive_path = archive_root / archive_name_for_day(period_day)
    if archive_path.exists():
        raise FileExistsError(f"Archive already exists: {archive_path}")

    if verbose:
        print(f"Creating archive {archive_path}")

    with tarfile.open(archive_path, mode="w:gz") as tf:
        for item in sorted(staging_dir.iterdir()):
            tf.add(item, arcname=item.name)

    # Write a simple checksum manifest for the archive itself.
    manifest_path = archive_root / manifest_name_for_day(period_day)
    digest = compute_sha256(archive_path)
    manifest_path.write_text(f"{digest}  {archive_path.name}\n", encoding="utf-8")

    # Remove uncompressed staging files after successful archive creation.
    shutil.rmtree(staging_dir)
    return archive_path


def purge_old_archives(archive_root: Path, retention_days: int, now: datetime, verbose: bool = False) -> None:
    cutoff = now - timedelta(days=retention_days)

    for path in archive_root.iterdir() if archive_root.exists() else []:
        if not path.is_file():
            continue
        if path.suffix not in {".gz", ".sha256"}:
            continue
        try:
            mtime = datetime.fromtimestamp(path.stat().st_mtime, tz=now.tzinfo)
        except FileNotFoundError:
            continue
        if mtime < cutoff:
            if verbose:
                print(f"Removing old archive artifact {path}")
            path.unlink(missing_ok=True)


def acquire_lock(lock_path: Path) -> int:
    ensure_parent(lock_path)
    fd = os.open(str(lock_path), os.O_CREAT | os.O_RDWR, 0o644)
    try:
        import fcntl
        fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        os.close(fd)
        raise RuntimeError("Another instance is already running")
    os.ftruncate(fd, 0)
    os.write(fd, str(os.getpid()).encode("ascii"))
    return fd


def parse_args(argv: list[str]) -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Rotate HiveFS logs and WBL/MCL logs")
    p.add_argument("--log", dest="logs", action="append", default=[], help="Log path to rotate; may be specified multiple times")
    p.add_argument("--archive-root", default=DEFAULT_ARCHIVE_ROOT, help=f"Archive root directory (default: {DEFAULT_ARCHIVE_ROOT})")
    p.add_argument("--retention-days", type=int, default=DEFAULT_RETENTION_DAYS, help=f"Days of archives to retain (default: {DEFAULT_RETENTION_DAYS})")
    p.add_argument("--day", help="Override archive day as YYYY-MM-DD; default is yesterday in local time")
    p.add_argument("--verbose", action="store_true")
    return p.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    logs = [Path(p) for p in (args.logs or DEFAULT_LOGS)]
    archive_root = Path(args.archive_root)

    now = local_now()
    if args.day:
        try:
            period_day = datetime.strptime(args.day, "%Y-%m-%d").replace(tzinfo=now.tzinfo)
        except ValueError as exc:
            print(f"Invalid --day value: {exc}", file=sys.stderr)
            return 2
    else:
        period_day = (now - timedelta(days=1)).replace(hour=0, minute=0, second=0, microsecond=0)

    lock_fd = None
    try:
        lock_fd = acquire_lock(Path(LOCK_FILE))
        archive_root.mkdir(parents=True, exist_ok=True)

        staging_dir, rotated_files = rotate_logs(logs, archive_root, period_day, verbose=args.verbose)
        archive_path = compress_staging_dir(staging_dir, archive_root, period_day, verbose=args.verbose)
        purge_old_archives(archive_root, args.retention_days, now, verbose=args.verbose)

        # Ensure active files still exist even if something external removed them.
        for log in logs:
            create_empty_if_missing(log)

        if args.verbose:
            print(f"Archive created: {archive_path}")
            for rf in rotated_files:
                print(f"Included: {rf.name}")
        return 0
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    finally:
        if lock_fd is not None:
            try:
                os.close(lock_fd)
            except OSError:
                pass


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
