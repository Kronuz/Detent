"""Summarize stash_niche.sh logs by docs/design/stash-niche.md's rule.

Per row (horizon, keys, producers): each arm's median over runs of insert, drain and cost per
key with its min-max; the noise floor (the larger of the two arms' median consecutive relative
differences on cost per key); r = the median over pairs of (w cost) / (b cost); the memory gate
(total bytes per key, entry plus heap, from `nichemem` lines); and the outcome: win, loss or
inconclusive. Pass every log of a row split over jobs, and the memory logs.
"""

from __future__ import annotations

import statistics
import sys
from collections import defaultdict
from itertools import pairwise

THRESHOLD = 1.10
Key = tuple[int, int, int, str]


def parse(
    paths: list[str],
) -> tuple[
    dict[Key, list[dict[str, str]]], dict[Key, list[float]], dict[Key, list[float]]
]:
    runs: dict[Key, list[dict[str, str]]] = defaultdict(list)
    heap: dict[Key, list[float]] = defaultdict(list)
    total: dict[Key, list[float]] = defaultdict(list)
    for path in paths:
        with open(path) as log:
            for line in log:
                if not line.startswith("row="):
                    continue
                fields = dict(kv.split("=", 1) for kv in line.split())
                key = (
                    int(fields["h_ms"]),
                    int(fields["n"]),
                    int(fields["p"]),
                    fields["arm"],
                )
                if fields["row"] == "niche":
                    runs[key].append(fields)
                elif fields["row"] == "nichemem":
                    heap[key].append(float(fields["heap_per_key"]))
                    total[key].append(
                        float(fields["heap_per_key"]) + float(fields["entry_bytes"])
                    )
    return runs, heap, total


def floor(xs: list[float]) -> float:
    if len(xs) < 2:
        return 0.0
    m = statistics.median(xs)
    return statistics.median(abs(b - a) for a, b in pairwise(xs)) / m if m else 0.0


def cell(rs: list[dict[str, str]], metric: str) -> str:
    xs = [float(r[metric]) for r in rs]
    return f"{metric}={statistics.median(xs):.1f} [{min(xs):.1f}-{max(xs):.1f}]"


def over_p(table: dict[Key, list[float]], h: int, n: int, arm: str) -> list[float]:
    return [
        v
        for (hh, nn, _, a), vs in table.items()
        if (hh, nn, a) == (h, n, arm)
        for v in vs
    ]


def main(paths: list[str]) -> None:
    runs, heap, total = parse(paths)
    for h, n, p in sorted({k[:3] for k in runs}):
        b, w = runs[(h, n, p, "b")], runs[(h, n, p, "w")]
        print(f"\nh_ms={h} n={n} p={p}")
        for arm, rs in (("b", b), ("w", w)):
            ok = all(r["valid"] == "1" for r in rs)
            capped = sum(r["capped"] == "1" for r in rs)
            extra = ""
            if arm == "w":
                extra = (
                    f" wakes={statistics.median(int(r['calls']) for r in rs)}"
                    f" empty={statistics.median(int(r['empty_calls']) for r in rs)}"
                )
            print(
                f"  {arm} runs={len(rs)} {cell(rs, 'insert_ns')} {cell(rs, 'drain_ns')} {cell(rs, 'cost_ns')} "
                f"rss_growth_mb={statistics.median(float(r['rss_growth_mb']) for r in rs):.1f} valid={ok} capped={capped}{extra}"
            )
        pairs = list(zip(b, w, strict=False))
        ratios = [float(wr["cost_ns"]) / float(br["cost_ns"]) for br, wr in pairs]
        r = statistics.median(ratios)
        phi = max(
            floor([float(x["cost_ns"]) for x in b]),
            floor([float(x["cost_ns"]) for x in w]),
        )
        ins = statistics.median(
            float(wr["insert_ns"]) / float(br["insert_ns"]) for br, wr in pairs
        )
        drn = statistics.median(
            float(wr["drain_ns"]) / float(br["drain_ns"]) for br, wr in pairs
        )
        tb, tw = over_p(total, h, n, "b"), over_p(total, h, n, "w")
        hb, hw = over_p(heap, h, n, "b"), over_p(heap, h, n, "w")
        gate, mem_ok = "", True
        if tb and tw:
            mem_ratio = statistics.median(tb) / statistics.median(tw)
            mem_ok = mem_ratio <= THRESHOLD
            gate = (
                f" bytes_per_key(entry+heap) b={statistics.median(tb):.1f} w={statistics.median(tw):.1f}"
                f" b/w={mem_ratio:.2f}x gate={'ok' if mem_ok else 'FAIL'}"
                f" (heap alone b={statistics.median(hb):.1f} w={statistics.median(hw):.1f})"
            )
        ok = all(x["valid"] == "1" for x in b + w)
        # Reported beside the rule's outcome: the same row with the memory gate ignored.
        if not ok:
            time_only = outcome = "invalid"
        else:
            time_only = (
                "stash loss"
                if r < THRESHOLD
                else "stash win"
                if r - 1 > phi
                else "inconclusive"
            )
            outcome = (
                "stash loss" if time_only == "stash loss" or not mem_ok else time_only
            )
        print(
            f"  pairs={len(pairs)} r(w/b cost)={r:.3f} [{min(ratios):.3f}-{max(ratios):.3f}] "
            f"insert w/b={ins:.3f} drain w/b={drn:.3f} floor={phi:.1%}{gate}: {outcome} (time only: {time_only})"
        )


if __name__ == "__main__":
    main(sys.argv[1:])
