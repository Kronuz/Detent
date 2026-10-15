"""Record reproducible source and host context without environment values."""

from __future__ import annotations

import hashlib
import os
import platform
import selectors
import subprocess
import sys
import time
from pathlib import Path
from typing import Any


def command(arguments: list[str], directory: Path | None = None) -> str:
    process = subprocess.Popen(
        arguments, cwd=directory, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL
    )
    output = bytearray()
    deadline = time.monotonic() + 10
    try:
        assert process.stdout is not None
        with selectors.DefaultSelector() as selector:
            selector.register(process.stdout, selectors.EVENT_READ)
            while True:
                remaining = deadline - time.monotonic()
                if remaining <= 0 or not selector.select(remaining):
                    raise ValueError("metadata command exceeded ten seconds")
                chunk = os.read(process.stdout.fileno(), 8192)
                if not chunk:
                    break
                if len(output) + len(chunk) > 65536:
                    raise ValueError("metadata command output exceeds 64 KiB")
                output.extend(chunk)
        process.wait(timeout=max(0.001, deadline - time.monotonic()))
        if process.returncode:
            raise ValueError(f"metadata command failed: {arguments[0]}")
        return output.decode().strip()
    finally:
        if process.poll() is None:
            process.kill()
        process.wait()
        if process.stdout is not None:
            process.stdout.close()


def repository(path: Path) -> dict[str, str]:
    revision = command(["git", "rev-parse", "HEAD"], path)
    dirty = command(["git", "status", "--porcelain", "--untracked-files=all"], path)
    patch = command(["git", "diff", "--binary", "HEAD"], path)
    return {
        "revision": revision,
        "tracked_changes_sha256": hashlib.sha256(patch.encode()).hexdigest(),
        "clean": str(not dirty).lower(),
    }


def host() -> dict[str, Any]:
    return {
        "system": platform.system(),
        "release": platform.release(),
        "machine": platform.machine(),
        "python": sys.version,
        "cpu_count": os.cpu_count(),
        "load_average": os.getloadavg(),
    }


def cache_dependencies(build: Path) -> dict[str, dict[str, str]]:
    cache = build / "CMakeCache.txt"
    if not cache.exists():
        raise ValueError("build lacks CMakeCache.txt")
    result: dict[str, dict[str, str]] = {}
    for line in cache.read_text().splitlines():
        if not line.startswith("FETCHCONTENT_SOURCE_DIR_") or ":PATH=" not in line:
            continue
        name, location = line.split(":PATH=", 1)
        if not location:
            continue
        path = Path(location).resolve()
        if (path / ".git").exists():
            result[name.removeprefix("FETCHCONTENT_SOURCE_DIR_").lower()] = repository(
                path
            )
        else:
            result[name.removeprefix("FETCHCONTENT_SOURCE_DIR_").lower()] = {
                "source": str(path),
                "revision": "archive",
                "clean": "unknown",
            }
    return result
