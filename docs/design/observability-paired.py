"""Paired deltas of observability_bench JSON lines (docs/design/observability.md D2).

For each operation, name set, thread count and arm: the median, minimum and maximum over
repetitions of the arm's case p50 minus the `none` case p50 of the same repetition, and the
spread of `none` across repetitions. Also prints the sampling odds D3 and Q7 quote.
"""

from __future__ import annotations

import json
import statistics
import sys
from collections import defaultdict

PERIOD = 64


def main(path: str) -> int:
    by: dict[tuple[str, str, int, int], dict[str, float]] = defaultdict(dict)
    with open(path) as lines:
        for line in lines:
            record = json.loads(line)
            if "arm" in record:
                key = (record["op"], record["set"], record["threads"], record["rep"])
                by[key][record["arm"]] = record["p50_ns"]
    cases = sorted({key[:3] for key in by})
    reps = sorted({key[3] for key in by})
    arms = sorted({arm for found in by.values() for arm in found} - {"none"})
    print("| op | set | threads | arm | paired median ns | min ns | max ns |")
    print("|---|---|---:|---|---:|---:|---:|")
    for case in cases:
        for arm in arms:
            deltas = [
                by[case + (rep,)][arm] - by[case + (rep,)]["none"]
                for rep in reps
                if arm in by[case + (rep,)] and "none" in by[case + (rep,)]
            ]
            if deltas:
                print(
                    f"| {case[0]} | {case[1]} | {case[2]} | {arm} "
                    f"| {statistics.median(deltas):+.1f} | {min(deltas):+.1f} | {max(deltas):+.1f} |"
                )
    print("\n| op | set | threads | spread of none across repetitions ns |")
    print("|---|---|---:|---:|")
    for case in cases:
        values = [
            by[case + (rep,)]["none"] for rep in reps if "none" in by[case + (rep,)]
        ]
        print(
            f"| {case[0]} | {case[1]} | {case[2]} | {max(values) - min(values):.1f} |"
        )
    keep = (PERIOD - 1) / PERIOD
    print(
        f"\nP(a knob is never sampled), period {PERIOD}, r reads in each of p processes:"
    )
    for reads in (1, 2, 10):
        odds = ", ".join(f"p={p}: {keep ** (reads * p):.3f}" for p in (1, 10, 100))
        print(f"  r={reads}: {odds}")
    print("\nStandard error of a 50/50 arm split, percentage points:")
    for n in (640, 10_000, 100_000):
        print(f"  n={n}: {100 * (0.25 / (n / PERIOD)) ** 0.5:.1f}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
