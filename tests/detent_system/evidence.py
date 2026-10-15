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


def command(
    arguments: list[str], directory: Path | None = None, maximum_bytes: int = 65536
) -> str:
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
                if len(output) + len(chunk) > maximum_bytes:
                    raise ValueError(
                        f"metadata command output exceeds {maximum_bytes // 1024} KiB"
                    )
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
    source = hashlib.sha256()
    names = command(
        ["git", "ls-files", "-c", "-o", "--exclude-standard", "-z"],
        path,
        4 * 1024 * 1024,
    )
    for name in sorted(set(names.rstrip("\0").split("\0"))):
        if not name:
            continue
        file = path / name
        source.update(name.encode() + b"\0")
        if file.is_symlink():
            source.update(b"symlink\0" + os.readlink(file).encode())
        elif file.is_file():
            source.update(b"file\0")
            digest = hashlib.sha256()
            with file.open("rb") as stream:
                while block := stream.read(65536):
                    digest.update(block)
            source.update(digest.digest())
        else:
            source.update(b"missing\0")
    return {
        "revision": revision,
        "tracked_changes_sha256": hashlib.sha256(patch.encode()).hexdigest(),
        "source_tree_sha256": source.hexdigest(),
        "clean": str(not dirty).lower(),
    }


def file_sha256(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def archive_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    files = 0
    for directory, names, filenames in os.walk(path):
        names[:] = sorted(name for name in names if name != ".git")
        for name in sorted(filenames):
            file = Path(directory) / name
            files += 1
            if files > 100000:
                raise ValueError("dependency archive exceeds 100000 files")
            digest.update(str(file.relative_to(path)).encode() + b"\0")
            if file.is_symlink():
                digest.update(b"symlink\0" + os.readlink(file).encode())
            else:
                digest.update(b"file\0" + bytes.fromhex(file_sha256(file)))
    return digest.hexdigest()


def host() -> dict[str, Any]:
    return {
        "system": platform.system(),
        "release": platform.release(),
        "machine": platform.machine(),
        "python": sys.version,
        "cpu_count": os.cpu_count(),
        "load_average": os.getloadavg(),
        "physical_memory_bytes": physical_memory(),
    }


def physical_memory() -> int:
    if sys.platform == "darwin":
        return int(command(["sysctl", "-n", "hw.memsize"]))
    if sys.platform.startswith("freebsd"):
        return int(command(["sysctl", "-n", "hw.physmem"]))
    return os.sysconf("SC_PHYS_PAGES") * os.sysconf("SC_PAGE_SIZE")


def build_context(build: Path, output: Path) -> dict[str, Any]:
    cache: dict[str, str] = {}
    for line in (build / "CMakeCache.txt").read_text().splitlines():
        if line.startswith(("CMAKE_CXX_COMPILER:", "CMAKE_BUILD_TYPE:")):
            key, value = line.split("=", 1)
            cache[key.split(":", 1)[0]] = value
    compiler = cache.get("CMAKE_CXX_COMPILER")
    if compiler is None:
        raise ValueError("build lacks compiler provenance")
    filesystem = os.statvfs(output)
    return {
        "compiler": compiler,
        "compiler_version": command([compiler, "--version"]),
        "build_type": cache.get("CMAKE_BUILD_TYPE", ""),
        "filesystem_device": os.stat(output).st_dev,
        "filesystem_block_bytes": filesystem.f_frsize,
        "filesystem_mount": command(["df", "-P", str(output)]),
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
        dependency = name.removeprefix("FETCHCONTENT_SOURCE_DIR_").lower()
        path = (
            Path(location).resolve()
            if location
            else build / "_deps" / f"{dependency}-src"
        )
        if not path.is_dir():
            continue
        if (path / ".git").exists():
            result[name.removeprefix("FETCHCONTENT_SOURCE_DIR_").lower()] = repository(
                path
            )
        else:
            result[name.removeprefix("FETCHCONTENT_SOURCE_DIR_").lower()] = {
                "source": str(path),
                "revision": "archive",
                "source_tree_sha256": archive_sha256(path),
                "clean": "unknown",
            }
    return result
