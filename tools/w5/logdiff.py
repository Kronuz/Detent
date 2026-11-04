#!/usr/bin/env python3
"""Diff two daemon logs after removing what differs by construction: the date and time, the port, the state path.

  logdiff.py OLD.log NEW.log
"""

from __future__ import annotations

import difflib
import re
import sys
from pathlib import Path


def normalize(path: Path) -> list[str]:
    out = []
    for line in path.read_bytes().decode("utf-8", "replace").splitlines():
        line = re.sub(r"\d{4}-\d{2}-\d{2} [0-9:.]+", "TIME", line)
        line = re.sub(r"/\S*/(replay|state)\S*", "DIR", line)
        line = re.sub(r":184\d\d\b", ":PORT", line)
        out.append(line)
    return out


def main() -> int:
    old, new = normalize(Path(sys.argv[1])), normalize(Path(sys.argv[2]))
    diff = list(difflib.unified_diff(old, new, "old", "new", lineterm="", n=0))
    print(f"old {len(old)} lines, new {len(new)} lines, {sum(1 for d in diff if d[:1] in '+-' and d[:3] not in ('+++', '---'))} differing")
    print("\n".join(diff[:80]))
    return 1 if diff else 0


if __name__ == "__main__":
    sys.exit(main())
