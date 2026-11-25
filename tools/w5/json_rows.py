#!/usr/bin/env python3
"""Turns a benchmark's JSON output into unit rows for ab_compare.py.

  BENCH ... | json_rows.py [--keep REGEX]

The input is one JSON object, or one per line (trials): the numeric fields are medians over the lines.
A field whose name contains `_ms`, `_us` or `_ns` becomes `<name> <value> ms|us|ns`; other fields (counts,
bytes) are left out. `--keep` limits the rows to the names that match.
"""

from __future__ import annotations

import argparse
import json
import re
import statistics
import sys


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--keep")
    args = parser.parse_args()
    objects = []
    for raw in sys.stdin.read().splitlines():
        line = raw.strip()
        if line.startswith("{"):
            objects.append(json.loads(line))
    if not objects:
        print("no JSON object on stdin", file=sys.stderr)
        return 2
    keep = re.compile(args.keep) if args.keep else None
    rows = 0
    for name in objects[0]:
        values = [
            o[name]
            for o in objects
            if isinstance(o.get(name), (int, float)) and not isinstance(o.get(name), bool)
        ]
        unit = next((u for u in ("ms", "us", "ns") if f"_{u}" in name), None)
        if not values or unit is None or (keep and not keep.search(name)):
            continue
        print(f"{name} {statistics.median(values):.6f} {unit}")
        rows += 1
    return 0 if rows else 2


if __name__ == "__main__":
    sys.exit(main())
