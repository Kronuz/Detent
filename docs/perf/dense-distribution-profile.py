"""Where a sampled agent boot spends its busy samples, by category.

usage: dense-distribution-profile.py SAMPLE.txt...
Reads `sample`'s "Sort by top of stack" section (self samples per symbol, at 1 ms; symbols with
fewer than 5 samples are not listed) and prints one JSON line per file: busy samples by category
and the top busy symbols. Idle waits (condition variables, poll, kevent) are left out.
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

ROW = re.compile(r"^\s+(.+?)\s+\(in (.+?)\)\s+(\d+)$")
IDLE = {"__psynch_cvwait", "__ulock_wait", "poll", "kevent", "__semwait_signal", "mach_msg2_trap", "__workq_kernreturn"}
CATEGORIES = [
    ("sync", re.compile(r"^(__fcntl|fsync|fdatasync)$")),
    (
        "fs metadata",
        re.compile(r"^(__unlinkat|unlink|linkat|__rename|renameat|__open|open|__openat|close|mkdir|fstat)"),
    ),
    ("write", re.compile(r"^(write|pwrite|__pwrite|writev)$")),
    ("read", re.compile(r"^(read|pread|__pread|readv)$")),
    ("hash", re.compile(r"sha256", re.IGNORECASE)),
    ("decompress", re.compile(r"ZSTD|HUF_|FSE_", re.IGNORECASE)),
    ("tls", re.compile(r"aes|gcm|ssl|ghash|chacha", re.IGNORECASE)),
    ("memory", re.compile(r"malloc|free|memmove|memcpy|memset|bzero|platform_mem|nanov2|szone|tiny_|small_")),
]


def profile(path: Path) -> dict[str, object]:
    text = path.read_text(errors="replace")
    section = text.split("Sort by top of stack", 1)[1].split("Binary Images", 1)[0]
    busy: dict[str, int] = {}
    symbols: list[tuple[int, str]] = []
    for line in section.splitlines():
        found = ROW.match(line)
        if not found:
            continue
        symbol, image, count = found.group(1), found.group(2), int(found.group(3))
        if symbol in IDLE:
            continue
        category = next((name for name, pattern in CATEGORIES if pattern.search(symbol)), None)
        if category is None:
            category = "detent code" if image.startswith("detent-") else f"other ({image})"
        busy[category] = busy.get(category, 0) + count
        symbols.append((count, symbol[:100]))
    return {
        "file": str(path),
        "busy_ms": sum(busy.values()),
        "by_category_ms": dict(sorted(busy.items(), key=lambda item: -item[1])),
        "top": [f"{count} {symbol}" for count, symbol in sorted(symbols, reverse=True)[:12]],
    }


if __name__ == "__main__":
    for argument in sys.argv[1:]:
        print(json.dumps(profile(Path(argument))))
