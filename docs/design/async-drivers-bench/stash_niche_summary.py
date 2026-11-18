"""Summarize stash_niche.sh logs: Kronuz/wheel (w) against stash (b), the A/A twin b2.

Per row (horizon, keys, producers): each arm's median over rounds of insert, drain and cost per key
with its min-max; the ratios of b2 and w to b, round by round (each round runs every arm once), as
the median with a distribution-free 95% interval, and the A/A band (b2 / b on cost). The statistics
are the wheel's (bench/wheel_bench_summary.py, loaded by path). Memory rows (`nichemem`): total bytes
per key, entry plus heap, median per arm. Pass every log of a row split over jobs.

usage: stash_niche_summary.py WHEEL_BENCH_SUMMARY.py LOG...
"""

from __future__ import annotations

import statistics
import sys
from collections import defaultdict

from wheel_vs_stash_summary import load

METRICS = ["insert_ns", "drain_ns", "cost_ns"]
Key = tuple[int, int, int]


def main(argv: list[str]) -> None:
    lib = load(argv[0])
    runs: dict[Key, dict[str, dict[int, dict[str, str]]]] = defaultdict(lambda: defaultdict(dict))
    total: dict[tuple[int, int, str], list[float]] = defaultdict(list)
    heap: dict[tuple[int, int, str], list[float]] = defaultdict(list)
    for path in argv[1:]:
        current = -1
        with open(path) as f:
            for line in f:
                if line.startswith("round="):
                    current = int(line.split()[0].split("=", 1)[1])
                if not line.startswith(("row=niche ", "row=nichemem ")):
                    continue
                fields = dict(kv.split("=", 1) for kv in line.split())
                if fields["row"] == "niche":
                    runs[(int(fields["h_ms"]), int(fields["n"]), int(fields["p"]))][fields["arm"]][current] = fields
                else:
                    key = (int(fields["h_ms"]), int(fields["n"]), fields["arm"])
                    heap[key].append(float(fields["heap_per_key"]))
                    total[key].append(float(fields["heap_per_key"]) + float(fields["entry_bytes"]))
    for (h, n, p), arms in sorted(runs.items()):
        print(f"\nh_ms={h} n={n} p={p} rounds={len({r for rs in arms.values() for r in rs})}")
        for arm in sorted(arms):
            rs = list(arms[arm].values())
            cells = []
            for metric in METRICS:
                xs = [float(r[metric]) for r in rs]
                cells.append(f"{metric}={statistics.median(xs):.1f} [{min(xs):.1f}-{max(xs):.1f}]")
            extra = ""
            if arm == "w":
                extra = f" wakes={statistics.median(int(r['calls']) for r in rs)} empty={statistics.median(int(r['empty_calls']) for r in rs)}"
            print(f"  {arm:3} runs={len(rs)} " + " ".join(cells)
                  + f" rss_growth_mb={statistics.median(float(r['rss_growth_mb']) for r in rs):.1f}"
                  + f" valid={all(r['valid'] == '1' for r in rs)} capped={sum(r['capped'] == '1' for r in rs)}{extra}")
        for arm in ("b2", "w"):
            cells = []
            for metric in METRICS:
                m, lo, hi = lib.median_interval(lib.paired(arms, arm, "b", metric))
                cells.append(f"{metric} {lib.fmt(m, lo, hi)}")
                if arm == "b2" and metric == "cost_ns":
                    band = max(abs(lo - 1), abs(hi - 1))
            print(f"  {arm}/b " + ", ".join(cells) + (f"  (A/A band on cost_ns {band:.1%})" if arm == "b2" else ""))
    for h, n in sorted({k[:2] for k in total}):
        cells = []
        for arm in ("b", "w"):
            if (h, n, arm) in total:
                cells.append(f"{arm} total={statistics.median(total[(h, n, arm)]):.2f} heap={statistics.median(heap[(h, n, arm)]):.2f}")
        print(f"\nmem h_ms={h} n={n} bytes per key (entry plus heap): " + "  ".join(cells))


if __name__ == "__main__":
    main(sys.argv[1:])
