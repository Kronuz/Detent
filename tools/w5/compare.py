#!/usr/bin/env python3
"""Compare two outputs of an M1 identity driver (tools/w5), line by line.

A driver prints one line per case, in a fixed order, so the two outputs are compared line by
line. The differing pairs are reported and counted by the first word of the line (the class of
the case).

  compare.py OLD NEW [--show N] [--ignore PREFIX ...]

Exit status 0 when the outputs are identical, 1 when any line differs, 2 on a usage error.
"""

from __future__ import annotations

import argparse
import sys
from collections import Counter
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("old", type=Path)
    parser.add_argument("new", type=Path)
    parser.add_argument("--show", type=int, default=8, help="differing pairs shown per class")
    parser.add_argument("--ignore", action="append", default=[], help="line prefix to leave out (expected to differ)")
    args = parser.parse_args()
    old = args.old.read_text(errors="replace").splitlines()
    new = args.new.read_text(errors="replace").splitlines()

    def keep(line: str) -> bool:
        return not any(line.startswith(prefix) for prefix in args.ignore)

    old = [line for line in old if keep(line)]
    new = [line for line in new if keep(line)]
    if len(old) != len(new):
        print(f"line counts differ: old {len(old)}, new {len(new)}")
    total: Counter[str] = Counter()
    different: Counter[str] = Counter()
    shown: Counter[str] = Counter()
    for a, b in zip(old, new):
        kind = a.split(" ", 1)[0]
        total[kind] += 1
        if a != b:
            different[kind] += 1
            if shown[kind] < args.show:
                shown[kind] += 1
                print(f"[{kind}]\n  old: {a[:300]}\n  new: {b[:300]}")
    print()
    print(f"{'class':24s} {'cases':>8s} {'different':>10s}")
    for kind in sorted(total):
        print(f"{kind:24s} {total[kind]:8d} {different[kind]:10d}")
    print(f"{'total':24s} {sum(total.values()):8d} {sum(different.values()):10d}")
    return 1 if different or len(old) != len(new) else 0


if __name__ == "__main__":
    sys.exit(main())
