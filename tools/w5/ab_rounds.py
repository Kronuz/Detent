#!/usr/bin/env python3
"""Runs two commands interleaved, with an A/A control, and saves each run's output for ab_compare.py.

  ab_rounds.py --a "CMD" --b "CMD" --rounds N --out DIR

Every round runs A, B and A again (the control, `aa`), the order rotating from round to round so a drift
does not always favour one arm. Output of run n of an arm: DIR/<arm>/run-NNN.txt (stdout, or stderr if stdout is
empty). A command that exits nonzero stops the run. Then compare with cpp-libraries' tools/ab_compare.py:
  ab_compare.py compare --format unit --a DIR/a --b DIR/b --aa DIR/aa --label-a old --label-b new
"""

from __future__ import annotations

import argparse
import shlex
import subprocess
import sys
import time
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--a", required=True)
    parser.add_argument("--b", required=True)
    parser.add_argument("--rounds", type=int, default=5)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=600)
    args = parser.parse_args()
    commands = {"a": shlex.split(args.a), "b": shlex.split(args.b), "aa": shlex.split(args.a)}
    for arm in commands:
        (args.out / arm).mkdir(parents=True, exist_ok=True)
    started = time.monotonic()
    timings = []
    for n in range(1, args.rounds + 1):
        order = list(commands)
        shift = (n - 1) % len(order)
        order = order[shift:] + order[:shift]
        for arm in order:
            begin = time.monotonic()
            result = subprocess.run(commands[arm], capture_output=True, text=True, timeout=args.timeout, check=False)
            timings.append(f"round {n} {arm} {time.monotonic() - begin:.1f}s")
            if result.returncode:
                print(f"{arm} round {n} exited {result.returncode}: {result.stderr[-400:]}", file=sys.stderr)
                return 2
            (args.out / arm / f"run-{n:03d}.txt").write_text(result.stdout if result.stdout.strip() else result.stderr)
        print(f"round {n}/{args.rounds} done", file=sys.stderr, flush=True)
    (args.out / "timings.txt").write_text("\n".join(timings) + f"\ntotal {time.monotonic() - started:.1f}s\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
