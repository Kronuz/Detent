"""T9's crossing by the pre-registered rule (docs/perf/namespace-measure.md, "T9: the cut point").

Reads the JSON lines that namespace_measure_t9 prints (one per run), from job logs or files, and
prints the medians, the fit, α at 1 and 10 Gb/s on raw bytes (the rule) and on framed bytes (the
sensitivity row), and the policy value.

Run: python3 -I docs/perf/namespace-measure-t9-fit.py LOG ...
"""

from __future__ import annotations

import json
import math
import statistics
import sys
from collections.abc import Callable
from fractions import Fraction
from pathlib import Path
from typing import Any

LINKS = {"1 Gb/s": 1e9 / 8, "10 Gb/s": 10e9 / 8}  # bytes per second


def runs(paths: list[str]) -> list[dict[str, Any]]:
    out: list[dict[str, Any]] = []
    for p in paths:
        for line in Path(p).read_text().splitlines():
            line = line.strip()
            if line.startswith('{"arm"'):
                out.append(json.loads(line))
    return out


def fit(xs: list[float], ys: list[float]) -> tuple[float, float, float]:
    """Least squares y = a + b x; returns (a, b, R²)."""
    mx, my = statistics.fmean(xs), statistics.fmean(ys)
    sxx = sum((x - mx) ** 2 for x in xs)
    b = sum((x - mx) * (y - my) for x, y in zip(xs, ys, strict=True)) / sxx
    a = my - b * mx
    ss = sum((y - my) ** 2 for y in ys)
    res = sum((y - a - b * x) ** 2 for x, y in zip(xs, ys, strict=True))
    return a, b, 1 - res / ss if ss else 1.0


def piecewise(points: list[tuple[float, float]], x: float) -> float:
    """Linear interpolation of sorted (x, y) medians, extended past either end by its slope."""
    if x <= points[0][0]:
        (x0, y0), (x1, y1) = points[0], points[1]
    elif x >= points[-1][0]:
        (x0, y0), (x1, y1) = points[-2], points[-1]
    else:
        i = next(i for i in range(1, len(points)) if points[i][0] >= x)
        (x0, y0), (x1, y1) = points[i - 1], points[i]
    return y0 + (y1 - y0) * (x - x0) / (x1 - x0)


def crossing(
    t_blocks: Callable[[float], float], copy: float, t_base: float
) -> float | None:
    """The smallest x in (0, 2 C] with T_blocks(x) = T_base, by bisection on a fine grid.

    None when T_blocks already exceeds T_base at the grid's first point; inf when it never does."""
    grid = [copy * 2 * i / 4000 for i in range(1, 4001)]
    previous = None
    for x in grid:
        gap = t_blocks(x) - t_base
        if gap >= 0:
            if previous is None:
                return None
            lo, hi = previous, x
            for _ in range(60):
                mid = (lo + hi) / 2
                if t_blocks(mid) - t_base >= 0:
                    hi = mid
                else:
                    lo = mid
            return (lo + hi) / 2
        previous = x
    return math.inf


def main(paths: list[str]) -> None:
    rs = runs(paths)
    deltas = [r for r in rs if r["arm"] == "delta"]
    fulls = [r for r in rs if r["arm"] == "full"]
    if not deltas or not fulls:
        raise SystemExit("need delta and full runs")
    copy = statistics.median(r["x_raw"] for r in fulls)
    copy_framed = statistics.median(r["x_framed"] for r in fulls)
    t_full = statistics.median(r["apply_wall_s"] for r in fulls)
    print(
        f"full copy: C = {copy:,.0f} B raw, {copy_framed:,.0f} B framed; "
        f"T_full median {t_full:.2f} s (range {min(r['apply_wall_s'] for r in fulls):.2f}-"
        f"{max(r['apply_wall_s'] for r in fulls):.2f}), CPU {statistics.median(r['apply_cpu_s'] for r in fulls):.2f} s, "
        f"runs {len(fulls)}; relay encode CPU {statistics.median(r['encode_cpu_s'] for r in fulls):.2f} s"
    )
    by: dict[float, list[dict[str, Any]]] = {}
    for r in deltas:
        by.setdefault(r["fraction"], []).append(r)
    medians: list[tuple[float, float]] = []
    for f in sorted(by):
        group = by[f]
        x = statistics.median(r["x_raw"] for r in group)
        t = statistics.median(r["apply_wall_s"] for r in group)
        medians.append((x, t))
        walls = [r["apply_wall_s"] for r in group]
        print(
            f"f {f:g}: x = {x:,.0f} B ({x / copy:.3f} C), framed {statistics.median(r['x_framed'] for r in group):,.0f} B; "
            f"T_apply median {t:.2f} s (range {min(walls):.2f}-{max(walls):.2f}), CPU "
            f"{statistics.median(r['apply_cpu_s'] for r in group):.2f} s; decode/plan/publish "
            f"{statistics.median(r['decode_s'] for r in group):.2f}/{statistics.median(r['plan_s'] for r in group):.2f}/"
            f"{statistics.median(r['publish_s'] for r in group):.2f} s; relay encode CPU "
            f"{statistics.median(r['encode_cpu_s'] for r in group):.2f} s; runs {len(group)}"
        )
    a, b, r2 = fit([r["x_raw"] for r in deltas], [r["apply_wall_s"] for r in deltas])
    worst = max(abs(t - (a + b * x)) / t for x, t in medians)
    linear = r2 >= 0.98 and worst <= 0.10
    print(
        f"fit: T_apply = {a:.3f} s + {b * copy:.3f} s per C; R² {r2:.4f}; worst median off the line {worst:.1%}; "
        f"rule uses the {'linear fit' if linear else 'piecewise-linear medians'}"
    )

    def apply_at(x: float) -> float:
        return a + b * x if linear else piecewise(medians, x)

    framed_ratio = statistics.median(r["x_framed"] / r["x_raw"] for r in deltas)
    alphas: dict[str, float] = {}
    for name, link in LINKS.items():
        for label, scale, base in (
            ("raw", 1.0, copy),
            ("framed (sensitivity)", framed_ratio, copy_framed),
        ):
            t_base = base / link + t_full

            def blocks(x: float, scale: float = scale, link: float = link) -> float:
                return x * scale / link + apply_at(x)

            x_star = crossing(blocks, copy, t_base)
            if x_star is None:
                alpha = (
                    max((t_base - a) / (1 / link + b) / copy if linear else 0.0, 0.0)
                    or 1 / 8
                )
                note = "block answers slower than the copy at every measured x"
            elif math.isinf(x_star):
                alpha, note = 2.0, "no crossing up to 2 C: clamped at 2"
            else:
                alpha, note = x_star / copy, "crossing"
            print(f"{name}, {label}: T_base {t_base:.2f} s; alpha {alpha:.3f} ({note})")
            if label == "raw":
                alphas[name] = alpha
    low = min(alphas.values())
    policy = Fraction(max(1, math.floor(low * 8)), 8)
    print(
        f"policy alpha (smallest over links, rounded down to 1/8): {policy.numerator}/{policy.denominator} = {float(policy):.3f}"
    )


if __name__ == "__main__":
    main(sys.argv[1:])
