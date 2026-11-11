"""Summarize wheel_vs_stash.sh logs by docs/design/wheel-evidence.md's rule.

Per row, size and arm: the median over rounds of each metric with its min-max; the A/A band
(a against a2 for single, b against b2 for cross); and each unified candidate's ratio, with
pass, fail or inconclusive (within the band of the 1.10 threshold).
"""

from __future__ import annotations

import statistics
import sys
from collections import defaultdict

THRESHOLD = 1.10
METRICS: dict[str, list[str]] = {
    "single": ["arm_ns", "rearm_ns", "cancel_ns", "expire_ns", "score"],
    "cross": ["producer_ns", "span_ms", "max_late_ms", "last_after_deadline_ms"],
    "mem": [
        "entry_bytes",
        "bytes_per_entry",
        "heap_per_armed_entry",
        "heap_per_rearm",
        "heap_after_expire",
    ],
}
SIZE = {"single": "n", "cross": "p", "mem": "n"}
PAIR = {"single": ("a", "a2", "score"), "cross": ("b", "b2", "producer_ns")}


def parse(paths: list[str]) -> dict[tuple[str, int, str], list[dict[str, str]]]:
    runs: dict[tuple[str, int, str], list[dict[str, str]]] = defaultdict(list)
    for path in paths:
        with open(path) as f:
            for line in f:
                if not line.startswith("row="):
                    continue
                fields = dict(kv.split("=", 1) for kv in line.split())
                row = fields["row"]
                runs[(row, int(fields[SIZE[row]]), fields["arm"])].append(fields)
    return runs


def spread(values: list[float]) -> float:
    m = statistics.median(values)
    return (max(values) - min(values)) / m if m else 0.0


def main(paths: list[str]) -> None:
    runs = parse(paths)
    by_size: dict[tuple[str, int], dict[str, list[dict[str, str]]]] = defaultdict(dict)
    for (row, size, arm), rs in runs.items():
        by_size[(row, size)][arm] = rs
    for (row, size), arms in sorted(by_size.items()):
        print(f"\n{row} {SIZE[row]}={size}")
        medians: dict[str, float] = {}
        values: dict[str, list[float]] = {}
        for arm in sorted(arms):
            cells = []
            for metric in METRICS[row]:
                xs = [float(r[metric]) for r in arms[arm]]
                cells.append(
                    f"{metric}={statistics.median(xs):.2f} [{min(xs):.2f}-{max(xs):.2f}]"
                )
            if row == "cross":
                cells.append(f"valid={all(r['valid'] == '1' for r in arms[arm])}")
            print(f"  {arm:3} runs={len(arms[arm])} " + " ".join(cells))
            if row in PAIR:
                key = PAIR[row][2]
                values[arm] = [float(r[key]) for r in arms[arm]]
                medians[arm] = statistics.median(values[arm])
        if row not in PAIR:
            continue
        first, second, key = PAIR[row]
        if first not in medians or second not in medians:
            continue
        band = max(
            abs(medians[first] - medians[second]) / medians[first],
            spread(values[first]),
            spread(values[second]),
        )
        print(
            f"  A/A band on {key}: {band:.1%} ({first} {medians[first]:.2f}, {second} {medians[second]:.2f})"
        )
        candidates = [a for a in ("a", "b", "c", "d", "e", "f") if a in medians]
        if row == "single":
            best = min(candidates, key=lambda a: medians[a])
            reference, label = medians[best], f"best ({best})"
            unified = [a for a in ("b", "c", "d", "e", "f") if a in medians]
        else:
            reference, label = medians["b"], "stash (b)"
            unified = [a for a in ("c", "d", "e", "f") if a in medians]
        for arm in unified:
            ratio = medians[arm] / reference
            if abs(ratio - THRESHOLD) < band:
                verdict = "inconclusive"
            elif ratio <= THRESHOLD:
                verdict = "pass"
            else:
                verdict = "fail"
            print(
                f"  {arm}: {key} {medians[arm]:.2f} = {ratio:.3f}x {label}: {verdict}"
            )


if __name__ == "__main__":
    main(sys.argv[1:])
