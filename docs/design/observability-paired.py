"""Paired deltas of observability_bench JSON lines (docs/design/observability.md D2).

For each operation, name set, thread count and arm: the median, minimum and maximum over
repetitions of the arm's case p50 minus the `none` case p50 of the same repetition, and the
spread of `none` across repetitions. Also prints the sampling odds D3 and Q7 quote.

Interleaved cases (`--interleave 1`, A8 and OB1's gate) already carry each repetition's delta
(the mean of its on-first and off-first block-pair medians); for them it prints the median,
minimum and maximum over repetitions and the gate (at most 5 ns at 1 thread, 10 ns at 8 and 64).
Exits 1 when an exact arm lost a count, an "ever read" arm missed a name, or a timed loop
allocated.
"""

from __future__ import annotations

import json
import statistics
import sys
from collections import defaultdict
from typing import Any

PERIOD = 64


GATE = {1: 5.0, 8: 10.0, 64: 10.0}


def interleaved(pairs: list[dict[str, Any]]) -> int:
    failures: list[str] = []
    groups: dict[tuple[int, str, str, int, str], list[dict[str, Any]]] = defaultdict(
        list
    )
    for record in pairs:
        key = (
            record["bare"],
            record["op"],
            record["set"],
            record["threads"],
            record["pair"],
        )
        groups[key].append(record)
        where = f"{record['pair']} {record['op']}/{record['set']}/{record['threads']}t rep {record['rep']}"
        if record["allocations"]:
            failures.append(f"{where}: {record['allocations']} allocations")
        if record["expected"] >= 0 and record["counted"] != record["expected"]:
            failures.append(
                f"{where}: counted {record['counted']}, expected {record['expected']}"
            )
    for bare in sorted({key[0] for key in groups}):
        title = "counting code alone (--bare 1)" if bare else "around the S6 lookup"
        print(
            f"\nInterleaved on/off, {title}: median over repetitions of each repetition's delta "
            "(the mean of its on-first and off-first block-pair medians); order: half their gap"
        )
        print(
            "| op | set | threads | arm | delta ns | min ns | max ns | order ns | off p50 ns | gate ns | verdict | rows per thread |"
        )
        print("|---|---|---:|---|---:|---:|---:|---:|---:|---:|---|---:|")
        for key in sorted(k for k in groups if k[0] == bare):
            records = groups[key]
            deltas = [r["delta_ns"] for r in records]
            median = statistics.median(deltas)
            gate = GATE.get(key[3])
            verdict = (
                "" if bare or gate is None else ("pass" if median <= gate else "FAIL")
            )
            rows = max(r["thread_rows"] for r in records)
            print(
                f"| {key[1]} | {key[2]} | {key[3]} | {key[4]} | {median:+.1f} | {min(deltas):+.1f} "
                f"| {max(deltas):+.1f} | {statistics.median(r['order_ns'] for r in records):+.1f} "
                f"| {statistics.median(r['off_p50_ns'] for r in records):.1f} "
                f"| {gate if gate is not None else ''} | {verdict} | {rows} |"
            )
    for failure in failures:
        print(f"FAILED: {failure}")
    return 1 if failures else 0


def main(path: str) -> int:
    by: dict[tuple[str, str, int, int], dict[str, float]] = defaultdict(dict)
    pairs: list[dict[str, Any]] = []
    with open(path) as lines:
        for line in lines:
            record = json.loads(line)
            if "pair" in record:
                pairs.append(record)
            elif "arm" in record:
                key = (record["op"], record["set"], record["threads"], record["rep"])
                by[key][record["arm"]] = record["p50_ns"]
    if pairs:
        status = interleaved(pairs)
        if not by:
            return status
    else:
        status = 0
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
    return status


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
