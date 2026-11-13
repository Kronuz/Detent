"""Summarize wheel_vs_stash.sh logs: Kronuz/wheel (w) against stash (b), the A/A twin b2.

Per row and size: each arm's median over rounds of each metric with its min-max. Arms are compared
round by round (each round runs every arm once): a ratio is the median over rounds of arm / b in
that round, with a distribution-free 95% interval from order statistics; the A/A band is b2 / b the
same way, the farther end of its interval from 1.00 on the deciding metric. The statistics are the
wheel's (bench/wheel_bench_summary.py, loaded by path). Memory rows (`mem`) print each arm's median.

usage: wheel_vs_stash_summary.py WHEEL_BENCH_SUMMARY.py LOG...
"""

from __future__ import annotations

import importlib.util
import statistics
import sys
from collections import defaultdict
from types import ModuleType

DECIDING = {"single": "score", "cross": "producer_ns"}
MEM = ["entry_bytes", "bytes_per_entry", "heap_per_armed_entry", "heap_per_rearm", "heap_after_expire"]


def load(path: str) -> ModuleType:
    spec = importlib.util.spec_from_file_location("wheel_bench_summary", path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def timing(lib: ModuleType, logs: list[str]) -> None:
    for (row, size, _), arms in sorted(lib.parse(logs).items()):
        decide = DECIDING[row]
        rounds = len({r for rs in arms.values() for r in rs})
        print(f"\n{row} {lib.SIZE[row]}={size} rounds={rounds}")
        for arm in sorted(arms):
            cells = []
            for metric in lib.METRICS[row] + ["ghz0", "ghz1"]:
                if metric in next(iter(arms[arm].values())):
                    xs = lib.column(arms, arm, metric)
                    cells.append(f"{metric}={statistics.median(xs):.2f} [{min(xs):.2f}-{max(xs):.2f}]")
            if row == "cross":
                cells.append(f"valid={all(r['valid'] == '1' for r in arms[arm].values())}")
            print(f"  {arm:3} runs={len(arms[arm])} " + " ".join(cells))
        metrics = [m for m in lib.PAIRED[row] if m in next(iter(arms["b"].values()))]
        for arm in ("b2", "w"):
            cells = []
            for metric in metrics:
                m, lo, hi = lib.median_interval(lib.paired(arms, arm, "b", metric))
                cells.append(f"{metric} {lib.fmt(m, lo, hi)}")
                if arm == "b2" and metric == decide:
                    band = max(abs(lo - 1), abs(hi - 1))
            print(f"  {arm}/b " + ", ".join(cells) + (f"  (A/A band on {decide} {band:.1%})" if arm == "b2" else ""))


def memory(logs: list[str]) -> None:
    runs: dict[tuple[int, str], list[dict[str, str]]] = defaultdict(list)
    for path in logs:
        with open(path) as f:
            for line in f:
                if line.startswith("row=mem "):
                    fields = dict(kv.split("=", 1) for kv in line.split())
                    runs[(int(fields["n"]), fields["arm"])].append(fields)
    for (n, arm), rs in sorted(runs.items()):
        cells = " ".join(f"{m}={statistics.median(float(r[m]) for r in rs):.2f}" for m in MEM)
        print(f"mem n={n} arm={arm} runs={len(rs)} {cells}")


def main(argv: list[str]) -> None:
    lib, logs = load(argv[0]), argv[1:]
    # a log holds either the timing rows or the mem rows
    mem = [p for p in logs if any(line.startswith("row=mem ") for line in open(p))]
    if other := [p for p in logs if p not in mem]:
        timing(lib, other)
    if mem:
        memory(mem)


if __name__ == "__main__":
    main(sys.argv[1:])
