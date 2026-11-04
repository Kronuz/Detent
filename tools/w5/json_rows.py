#!/usr/bin/env python3
"""Turns a benchmark's one-line JSON object into unit rows for ab_compare.py.

  BENCH ... | json_rows.py [--keep REGEX]

Every numeric field whose name contains `_ms` or `_us` becomes `<name> <value> ms|us`; other fields (counts,
bytes) are left out. `--keep` limits the rows to the names that match.
"""

from __future__ import annotations

import argparse
import json
import re
import sys


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--keep")
    args = parser.parse_args()
    text = sys.stdin.read()
    start = text.find("{")
    if start < 0:
        print("no JSON object on stdin", file=sys.stderr)
        return 2
    value = json.loads(text[start:])
    keep = re.compile(args.keep) if args.keep else None
    rows = 0
    for name, number in value.items():
        if not isinstance(number, (int, float)) or isinstance(number, bool):
            continue
        unit = "ms" if "_ms" in name else "us" if "_us" in name else None
        if unit is None or (keep and not keep.search(name)):
            continue
        print(f"{name} {number:.6f} {unit}")
        rows += 1
    return 0 if rows else 2


if __name__ == "__main__":
    sys.exit(main())
