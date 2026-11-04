#!/usr/bin/env python3
"""Compare two outputs of an M1 identity driver (tools/w5), case by case.

A driver prints `<case id><TAB><result>` per case. The cases are matched by id, so a case present in one build
only (an enum member the new library adds) is reported on its own. Differing cases are counted by the first word
of the id (the class) and by how the result changed (`--signature` picks the part of the result that names it:
a regex with one group, default the first two words).

  compare.py OLD NEW [--show N] [--ignore PREFIX ...] [--signature REGEX] [--ids]

Exit status 0 when the outputs are identical, 1 when any case differs or exists in one build only.
"""

from __future__ import annotations

import argparse
import re
import sys
from collections import Counter
from pathlib import Path


def read(path: Path, ignore: list[str]) -> dict[str, str]:
    cases: dict[str, str] = {}
    for line in path.read_text(errors="replace").splitlines():
        if any(line.startswith(prefix) for prefix in ignore):
            continue
        key, _, result = line.partition("\t")
        cases[key] = result
    return cases


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("old", type=Path)
    parser.add_argument("new", type=Path)
    parser.add_argument("--show", type=int, default=3, help="examples shown per class")
    parser.add_argument("--ignore", action="append", default=[], help="id prefix to leave out (expected to differ)")
    parser.add_argument("--signature", help="regex with one group: the part of a result that names the outcome")
    parser.add_argument("--ids", action="store_true", help="print every differing id")
    parser.add_argument("--strip", help="regex removed from every result before comparing (output only one build can produce)")
    args = parser.parse_args()
    old, new = read(args.old, args.ignore), read(args.new, args.ignore)
    if args.strip:
        strip = re.compile(args.strip)
        old = {k: strip.sub("", v) for k, v in old.items()}
        new = {k: strip.sub("", v) for k, v in new.items()}
    pattern = re.compile(args.signature) if args.signature else None

    def signature(result: str) -> str:
        if pattern:
            match = pattern.search(result)
            return match.group(1) if match else "-"
        return " ".join(result.split()[:2]) or "-"

    total: Counter[str] = Counter()
    different: Counter[str] = Counter()
    changes: Counter[tuple[str, str, str]] = Counter()
    shown: Counter[str] = Counter()
    for key in sorted(set(old) | set(new)):
        kind = key.split(" ", 1)[0]
        total[kind] += 1
        a, b = old.get(key), new.get(key)
        if a == b:
            continue
        different[kind] += 1
        changes[(kind, "(absent)" if a is None else signature(a), "(absent)" if b is None else signature(b))] += 1
        if args.ids:
            print(f"differs: {key}")
        if shown[kind] < args.show:
            shown[kind] += 1
            print(f"[{kind}] {key}\n  old: {(a if a is not None else '(absent)')[:240]}\n  new: {(b if b is not None else '(absent)')[:240]}")
    print()
    for (kind, a, b), count in sorted(changes.items()):
        print(f"  {kind}: {a} -> {b}: {count}")
    print()
    print(f"{'class':24s} {'cases':>8s} {'different':>10s}")
    for kind in sorted(total):
        print(f"{kind:24s} {total[kind]:8d} {different[kind]:10d}")
    print(f"{'total':24s} {sum(total.values()):8d} {sum(different.values()):10d}")
    return 1 if different else 0


if __name__ == "__main__":
    sys.exit(main())
