"""Summarizes observability_bench JSON lines (docs/design/observability.md D2).

Prints one Markdown row per operation, name set, thread count and arm: the median over
repetitions of the case p50 (median over threads of each thread's p50 of batch means), its
difference from the `none` arm, and the A/A band (largest |none2 - none| over repetitions).
Exits 1 when a timed loop allocated or an exact counting arm lost a count.
"""

from __future__ import annotations

import json
import statistics
import sys
from collections import defaultdict
from typing import Any

EXACT = {
    "shared",
    "metrics_atomic",
    "metrics_contended",
    "stripe_rmw",
    "tls",
    "knob_shared",
    "knob_rows8",
    "knob_rows64",
    "knob_ptr_rows8",
    "knob_rows8_mapped",
}
LOSSY = {"stripe_plain", "knob_sampled"}


def main(path: str) -> int:
    cases: list[dict[str, Any]] = []
    soaks: list[dict[str, Any]] = []
    config: dict[str, Any] = {}
    with open(path) as lines:
        for line in lines:
            record = json.loads(line)
            if "arm" in record:
                cases.append(record)
            elif "soak" in record:
                soaks.append(record)
            elif "config" in record:
                config = record["config"]
    failures: list[str] = []
    groups: dict[tuple[str, str, int], dict[str, list[dict[str, Any]]]] = defaultdict(
        lambda: defaultdict(list)
    )
    for case in cases:
        groups[(case["op"], case["set"], case["threads"])][case["arm"]].append(case)
        where = f"{case['arm']} {case['op']}/{case['set']}/{case['threads']}t rep {case['rep']}"
        if case["allocations"]:
            failures.append(f"{where}: {case['allocations']} allocations")
        if case["arm"] in EXACT and case["counted"] != case["calls"]:
            failures.append(f"{where}: counted {case['counted']} of {case['calls']}")
    print(f"config: {json.dumps(config)}\n")
    print(
        "| op | set | threads | arm | p50 ns | delta ns | delta % | A/A band ns "
        "| p99 ns | worst thread p50 ns | counted / calls | minor faults |"
    )
    print("|---|---|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|")
    for (op, name_set, threads), arms in sorted(groups.items()):
        base = arms.get("none", [])
        base_p50 = (
            statistics.median(c["p50_ns"] for c in base) if base else float("nan")
        )
        band = float("nan")
        if base and arms.get("none2"):
            by_rep = {c["rep"]: c["p50_ns"] for c in base}
            diffs = [
                abs(c["p50_ns"] - by_rep[c["rep"]])
                for c in arms["none2"]
                if c["rep"] in by_rep
            ]
            band = max(diffs) if diffs else float("nan")
        for arm, runs in arms.items():
            p50 = statistics.median(c["p50_ns"] for c in runs)
            p99 = statistics.median(c["p99_ns"] for c in runs)
            worst = max(c["p50_max_ns"] for c in runs)
            delta = p50 - base_p50
            counted = sum(max(c["counted"], 0) for c in runs)
            calls = sum(c["calls"] for c in runs)
            ratio = f"{counted / calls:.4f}" if runs[0]["counted"] >= 0 else "-"
            faults = sum(c["minor_faults"] for c in runs)
            print(
                f"| {op} | {name_set} | {threads} | {arm} | {p50:.1f} | {delta:+.1f} "
                f"| {100 * delta / base_p50:+.1f} | {band:.1f} | {p99:.1f} | {worst:.1f} "
                f"| {ratio} | {faults} |"
            )
    if soaks:
        print(
            "\n| soak arm | seconds | p50 ns | worst batch ns | minor faults | counted |"
        )
        print("|---|---:|---:|---:|---:|---:|")
        for soak in soaks:
            print(
                f"| {soak['soak']} | {soak['seconds']} | {soak['p50_ns']:.1f} "
                f"| {soak['worst_batch_ns']:.1f} | {soak['minor_faults']} | {soak['counted']} |"
            )
    for failure in failures:
        print(f"FAIL {failure}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
