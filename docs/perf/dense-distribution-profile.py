"""Where a sampled agent boot spends its busy samples: by phase and by leaf category.

usage: dense-distribution-profile.py SAMPLE.txt...
Walks `sample`'s call tree (every thread; about 1.2 ms a sample on this host). A frame's own samples
are its count less its children's; idle waits (condition variables, poll, kevent, semaphores) are
left out. A sample's phase is the innermost enclosing frame that names one (PHASES); its category is
what the leaf is (CATEGORIES). Blocking syscalls (F_FULLFSYNC, link, open) count as busy: they are
wall time on the boot's path, not CPU. Prints one JSON line per file.
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

FRAME = re.compile(r"^([ +!:|]*)(\d+) (.+?)  \(in (.+?)\)")
THREAD = re.compile(r"^    (\d+) Thread_")
IDLE = re.compile(
    r"^(__psynch_cvwait|__ulock_wait|poll|kevent|__semwait_signal|mach_msg2_trap|__workq_kernreturn"
    r"|semaphore_wait_trap|std::__libcpp_atomic_wait)"
)
PHASES = [
    ("validate", re.compile(r"PagedValidator|GraphValidator|PageValidator|DependencyDiscovery")),
    ("pack", re.compile(r"PagedPacker")),
    ("build", re.compile(r"PagedSnapshotBuilder|PagedSnapshotPlanBuilder|TrieCursor|LeafBody")),
    ("receive transfer", re.compile(r"receive_image|receive_delta|TransferReceiver|[Tt]ransfer")),
    ("scrub", re.compile(r"PackScrub|scrub_loop")),
    ("publish", re.compile(r"AgentStore::publish")),
    ("decode frames", re.compile(r"SessionFrames|FrameDecoder|decode_frame|Frames")),
    ("network", re.compile(r"ssl|SSL|reactor_tls|http|Http")),
]
CATEGORIES = [
    ("device sync", re.compile(r"^(__fcntl|fcntl|fsync|fdatasync)$")),
    (
        "fs metadata",
        re.compile(r"^(__unlinkat|unlink|linkat|__rename|renameat|__open|open|__openat|close|mkdir|fstat)"),
    ),
    ("write", re.compile(r"^(write|pwrite|__pwrite|writev)$")),
    ("read", re.compile(r"^(read|pread|__pread|readv)$")),
    ("sha256", re.compile(r"sha256", re.IGNORECASE)),
    ("zstd", re.compile(r"ZSTD|HUF_|FSE_")),
    ("tls crypto", re.compile(r"aes|gcm|ghash|chacha|poly1305", re.IGNORECASE)),
    (
        "memory",
        re.compile(r"malloc|free|memmove|memcpy|memset|bzero|nanov2|szone|tiny_|small_|operator new|operator delete"),
    ),
]


def classify(rules: list[tuple[str, re.Pattern[str]]], symbol: str) -> str | None:
    return next((name for name, pattern in rules if pattern.search(symbol)), None)


def ordered(counts: dict[str, int]) -> dict[str, int]:
    return dict(sorted(counts.items(), key=lambda item: -item[1]))


def profile(path: Path) -> dict[str, object]:
    text = path.read_text(errors="replace")
    tree = text[text.index("Call graph:") : text.index("Total number in stack")]
    phases: dict[str, int] = {}
    categories: dict[str, int] = {}
    stack: list[list] = []  # [depth, count, symbol, image, children's samples], outermost first

    def close(frame: list) -> None:
        own = frame[1] - frame[4]
        if own <= 0 or IDLE.search(frame[2]):
            return
        symbols = [each[2] for each in stack] + [frame[2]]
        phase = next((found for symbol in reversed(symbols) if (found := classify(PHASES, symbol))), "other")
        category = classify(CATEGORIES, frame[2]) or ("detent code" if frame[3].startswith("detent-") else "other")
        phases[phase] = phases.get(phase, 0) + own
        categories[category] = categories.get(category, 0) + own

    for line in tree.splitlines():
        if THREAD.match(line):
            while stack:
                close(stack.pop())
            continue
        found = FRAME.match(line)
        if not found:
            continue
        depth, count = len(found.group(1)), int(found.group(2))
        while stack and stack[-1][0] >= depth:
            close(stack.pop())
        if stack:
            stack[-1][4] += count
        stack.append([depth, count, found.group(3), found.group(4), 0])
    while stack:
        close(stack.pop())
    return {
        "file": str(path),
        "busy_samples": sum(phases.values()),
        "by_phase": ordered(phases),
        "by_category": ordered(categories),
    }


if __name__ == "__main__":
    for argument in sys.argv[1:]:
        print(json.dumps(profile(Path(argument))))
