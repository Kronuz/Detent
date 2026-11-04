#!/usr/bin/env python3
"""Compares two http_replay.py outputs case by case.

  replay_compare.py OLD_DIR NEW_DIR

The daemon's own counters (requests served, uptime, rejected publishes and the health ETag and length that
follow from them) are masked, since a rejected request is not counted in the new build. Prints the cases that
still differ and whether the persisted snapshot.json is byte-identical.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path


def load(path: Path) -> dict[str, str]:
    text = path.read_bytes().decode("latin1")
    parts = re.split(r"^=== (\S+) \((\d+) bytes\)\n", text, flags=re.M)
    return {parts[i]: parts[i + 2] for i in range(1, len(parts), 3)}


def mask(s: str) -> str:
    s = re.sub(r'"requests_served":\d+', '"requests_served":N', s)
    s = re.sub(r'"uptime_seconds":\d+', '"uptime_seconds":N', s)
    s = re.sub(r'"puts_rejected":\d+', '"puts_rejected":N', s)
    if '"has_snapshot"' in s:
        s = re.sub(r'ETag: W/"[0-9a-f]+"\r?\n', "ETag: W\n", s)
        s = re.sub(r"Content-Length: 13\d\r?\n", "Content-Length: health\n", s)
    return s


def main() -> int:
    old_dir, new_dir = Path(sys.argv[1]), Path(sys.argv[2])
    old, new = load(old_dir / "responses.txt"), load(new_dir / "responses.txt")
    differing = [k for k in old if mask(old[k]) != mask(new.get(k, ""))]
    print(
        f"{len(old)} cases; {len(old) - len(differing)} identical; differing: {', '.join(differing) or 'none'}"
    )
    for k in differing:
        first = lambda s: s.split("\r\n")[0].split("\n")[0][:80]  # noqa: E731
        print(f"  {k}: old [{first(old[k])}] new [{first(new[k])}]")
    same = (old_dir / "snapshot.json").read_bytes() == (new_dir / "snapshot.json").read_bytes()
    print(f"persisted snapshot.json byte-identical: {same}")
    return 1 if differing or not same else 0


if __name__ == "__main__":
    sys.exit(main())
