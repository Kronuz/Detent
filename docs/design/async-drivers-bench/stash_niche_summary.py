"""Summarize stash_niche.sh logs by docs/design/stash-niche.md's rule.

Per row (horizon, keys, producers): each arm's median over runs of insert, drain and cost per
key with its min-max; the noise floor (the larger of the two arms' median consecutive relative
differences on cost per key); r = the median over pairs of (f cost) / (b cost); the memory gate
from `nichemem` lines; and the outcome: win, loss or inconclusive.
"""

from __future__ import annotations

import statistics
import sys
from collections import defaultdict

THRESHOLD = 1.10


def parse(
    paths: list[str],
) -> tuple[dict[tuple[int, int, int, str], list[dict[str, str]]], dict]:
    runs: dict[tuple[int, int, int, str], list[dict[str, str]]] = defaultdict(list)
    mem: dict[tuple[int, int, int, str], list[float]] = defaultdict(list)
    for path in paths:
        with open(path) as f:
            for line in f:
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
                    mem[key].append(float(fields["heap_per_key"]))
    return runs, mem


def floor(xs: list[float]) -> float:
    if len(xs) < 2:
        return 0.0
    m = statistics.median(xs)
    return (
        statistics.median(abs(b - a) for a, b in zip(xs, xs[1:], strict=False)) / m
        if m
        else 0.0
    )


def cell(rs: list[dict[str, str]], metric: str) -> str:
    xs = [float(r[metric]) for r in rs]
    return f"{metric}={statistics.median(xs):.1f} [{min(xs):.1f}-{max(xs):.1f}]"


def main(paths: list[str]) -> None:
    runs, mem = parse(paths)
    rows = sorted({k[:3] for k in runs})
    for h, n, p in rows:
        b, f = runs[(h, n, p, "b")], runs[(h, n, p, "f")]
        print(f"\nh_ms={h} n={n} p={p}")
        for arm, rs in (("b", b), ("f", f)):
            valid = all(r["valid"] == "1" for r in rs)
            capped = sum(r["capped"] == "1" for r in rs)
            extra = (
                f" f_calls={statistics.median(int(r['calls']) for r in rs)} empty={statistics.median(int(r['empty_calls']) for r in rs)}"
                if arm == "f"
                else ""
            )
            print(
                f"  {arm} runs={len(rs)} {cell(rs, 'insert_ns')} {cell(rs, 'drain_ns')} {cell(rs, 'cost_ns')} "
                f"rss_growth_mb={statistics.median(float(r['rss_growth_mb']) for r in rs):.1f} valid={valid} capped={capped}{extra}"
            )
        pairs = list(zip(b, f, strict=False))
        ratios = [float(fr["cost_ns"]) / float(br["cost_ns"]) for br, fr in pairs]
        r = statistics.median(ratios)
        phi = max(
            floor([float(x["cost_ns"]) for x in b]),
            floor([float(x["cost_ns"]) for x in f]),
        )
        ins = statistics.median(
            float(fr["insert_ns"]) / float(br["insert_ns"]) for br, fr in pairs
        )
        drn = statistics.median(
            float(fr["drain_ns"]) / float(br["drain_ns"]) for br, fr in pairs
        )
        mb = [
            v
            for (hh, nn, pp, a), vs in mem.items()
            if (hh, nn, a) == (h, n, "b")
            for v in vs
        ]
        mf = [
            v
            for (hh, nn, pp, a), vs in mem.items()
            if (hh, nn, a) == (h, n, "f")
            for v in vs
        ]
        gate = ""
        mem_ok = True
        if mb and mf:
            mem_ratio = statistics.median(mb) / statistics.median(mf)
            mem_ok = mem_ratio <= THRESHOLD
            gate = f" heap_per_key b={statistics.median(mb):.1f} f={statistics.median(mf):.1f} b/f={mem_ratio:.2f}x gate={'ok' if mem_ok else 'FAIL'}"
        valid = all(x["valid"] == "1" for x in b + f)
        if not valid:
            outcome = "invalid"
        elif r < THRESHOLD or not mem_ok:
            outcome = "stash loss"
        elif r - 1 > phi:
            outcome = "stash win"
        else:
            outcome = "inconclusive"
        print(
            f"  pairs={len(pairs)} r(f/b cost)={r:.3f} [{min(ratios):.3f}-{max(ratios):.3f}] "
            f"insert f/b={ins:.3f} drain f/b={drn:.3f} floor={phi:.1%}{gate}: {outcome}"
        )


if __name__ == "__main__":
    main(sys.argv[1:])
