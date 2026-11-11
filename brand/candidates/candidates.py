"""Write the three Detent logo candidates (a.svg, b.svg, c.svg) into the directory given."""

from __future__ import annotations

import math
import sys
from pathlib import Path

GREEN = "#93c34d"
GRAY = "#a7a7a7"
OUT = Path(sys.argv[1])

R = 100.0  # hexagon circumradius (trace centerline)
AP = R * math.sqrt(3) / 2  # apothem
TRACE = 11.4  # Kronuz trace width at R = 100
PAD_R, PAD_W = 8.0, 8.0  # Kronuz pad: ring outer radius 12, hole radius 4

Pt = tuple[float, float]


def f(v: float) -> str:
    s = f"{v:.2f}".rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


def pt(p: Pt) -> str:
    return f"{f(p[0])} {f(p[1])}"


def polar(r: float, deg: float) -> Pt:
    a = math.radians(deg)
    return (r * math.cos(a), r * math.sin(a))


def add(p: Pt, q: Pt) -> Pt:
    return (p[0] + q[0], p[1] + q[1])


def sub(p: Pt, q: Pt) -> Pt:
    return (p[0] - q[0], p[1] - q[1])


def mul(p: Pt, s: float) -> Pt:
    return (p[0] * s, p[1] * s)


def dot(p: Pt, q: Pt) -> float:
    return p[0] * q[0] + p[1] * q[1]


def unit(p: Pt) -> Pt:
    n = math.hypot(*p)
    return (p[0] / n, p[1] / n)


def circle(c: Pt, r: float) -> str:
    x, y = c
    return (
        f"M{f(x - r)} {f(y)}A{f(r)} {f(r)} 0 1 0 {f(x + r)} {f(y)}"
        f"A{f(r)} {f(r)} 0 1 0 {f(x - r)} {f(y)}Z"
    )


def capsule(p0: Pt, p1: Pt, hw: float) -> str:
    u = unit(sub(p1, p0))
    n = (-u[1], u[0])
    a0, a1 = add(p0, mul(n, hw)), add(p1, mul(n, hw))
    b0, b1 = add(p0, mul(n, -hw)), add(p1, mul(n, -hw))
    return (
        f"M{pt(a0)}L{pt(a1)}A{f(hw)} {f(hw)} 0 0 0 {pt(b1)}"
        f"L{pt(b0)}A{f(hw)} {f(hw)} 0 0 0 {pt(a0)}Z"
    )


def knob(r: float, deg: float, slot_w: float, margin: float) -> str:
    """A disc with a capsule slot cut from its center toward `deg`."""
    hw = slot_w / 2
    u = polar(1, deg)
    return circle((0, 0), r) + capsule((0, 0), mul(u, r - hw - margin), hw)


def svg(view: tuple[float, float, float, float], parts: list[str]) -> str:
    x, y, w, h = view
    head = f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="{f(x)} {f(y)} {f(w)} {f(h)}">'
    return head + "\n" + "\n".join(f"  {p}" for p in parts) + "\n</svg>\n"


# Pointy-top hexagon, vertex k at screen angle -90 + 60k; edge k (V[k] to V[k+1]) faces -60 + 60k.
V = [polar(R, -90 + 60 * k) for k in range(6)]
STOP = -60.0  # the selected stop: the upper-right edge's direction


def candidate_a() -> str:
    """Kronuz's traces: six traces wind around the hexagon; their inner pads are the stops."""
    frac = (
        0.7  # each trace runs 70% of its edge, then turns to run parallel to the next
    )
    traces, outer, inner = [], [], []
    for k in range(6):
        vk, vn, vnn = V[k], V[(k + 1) % 6], V[(k + 2) % 6]
        start = add(vk, mul(unit(sub(vn, vk)), PAD_R))
        corner = add(vk, mul(sub(vn, vk), frac))
        d2 = unit(sub(vnn, vn))
        t = -dot(
            corner, d2
        )  # the inner pad sits where the run passes closest to the center
        inner.append(add(corner, mul(d2, t)))
        end = add(corner, mul(d2, t - PAD_R))
        traces.append(f"M{pt(start)}L{pt(corner)}L{pt(end)}")
        outer.append(vk)
    rho = math.hypot(*inner[0])
    sel = next(
        i
        for i, p in enumerate(inner)
        if abs(math.degrees(math.atan2(p[1], p[0])) - STOP) < 1
    )
    stop = inner.pop(sel)
    pads = "".join(circle(p, PAD_R) for p in outer + inner)
    dial_r = rho - (PAD_R + PAD_W / 2) - 6
    parts = [
        f'<path id="traces" fill="none" stroke="{GRAY}" stroke-width="{f(TRACE)}" d="{"".join(traces)}"/>',
        f'<path id="pads" fill="none" stroke="{GRAY}" stroke-width="{f(PAD_W)}" d="{pads}"/>',
        f'<path id="stop" fill="none" stroke="{GREEN}" stroke-width="{f(PAD_W)}" d="{circle(stop, PAD_R)}"/>',
        f'<path id="knob" fill="{GREEN}" fill-rule="evenodd" d="{knob(dial_r, STOP, TRACE, 7)}"/>',
    ]
    m = PAD_R + PAD_W / 2 + 1
    w, h = 2 * AP + 2 * m, 2 * R + 2 * m
    return svg((-w / 2, -h / 2, w, h), parts)


XAP_RIM = (
    "M-19.65 -96.19A39.3 40.83 0 0 1 19.65 -96.19L70.35 -65.78A39.3 40.83 0 0 1 90 -30.41"
    "L90 30.41A39.3 40.83 0 0 1 70.35 65.78L19.65 96.19A39.3 40.83 0 0 1 -19.65 96.19"
    "L-70.35 65.78A39.3 40.83 0 0 1 -90 30.41L-90 -30.41A39.3 40.83 0 0 1 -70.35 -65.78Z"
)
XAP_HEX = (
    "M-16.38 -80.16A32.75 34.03 0 0 1 16.38 -80.16L58.63 -54.81A32.75 34.03 0 0 1 75 -25.34"
    "L75 25.34A32.75 34.03 0 0 1 58.63 54.81L16.38 80.16A32.75 34.03 0 0 1 -16.38 80.16"
    "L-58.63 54.81A32.75 34.03 0 0 1 -75 25.34L-75 -25.34A32.75 34.03 0 0 1 -58.63 -54.81Z"
)


def candidate_b() -> str:
    """Xapiand's hexagon and rim, with a white knob and seven stops cut out of it."""
    sel = -45.0
    stops = [135 + 45 * k for k in range(7)]  # 7:30 to 4:30, the knob's travel
    parts = [
        f'<path id="rim" fill="#fff" d="{XAP_RIM}"/>',
        f'<path id="hexagon" fill="{GREEN}" d="{XAP_HEX}"/>',
        f'<path id="knob" fill="#fff" fill-rule="evenodd" d="{knob(42, sel, 16, 8)}"/>',
        f'<path id="stops" fill="#fff" d="{"".join(circle(polar(63, a), 7) for a in stops)}"/>',
    ]
    return svg((-90, -101.66, 180, 203.32), parts)


def candidate_c() -> str:
    """Six gray chevrons make the hexagon; the gaps between them are the notches."""
    w, gap = 14.0, 22.0
    half = gap / 2 + w / 2  # round caps reach w/2 into the gap
    d = ""
    for k in range(6):
        a, b, c = V[(k - 1) % 6], V[k], V[(k + 1) % 6]
        p = add(mul(add(a, b), 0.5), mul(unit(sub(b, a)), half))
        q = add(mul(add(b, c), 0.5), mul(unit(sub(c, b)), -half))
        d += f"M{pt(p)}L{pt(b)}L{pt(q)}"
    ball = polar(AP, STOP)
    parts = [
        f'<path id="ring" fill="none" stroke="{GRAY}" stroke-width="{f(w)}" stroke-linecap="round" d="{d}"/>',
        f'<path id="knob" fill="{GREEN}" fill-rule="evenodd" d="{knob(52, STOP, w, 9)}"/>',
        f'<circle id="stop" fill="{GREEN}" cx="{f(ball[0])}" cy="{f(ball[1])}" r="9"/>',
    ]
    m = w / 2 + 1  # miter tips at the vertices reach w/2/sin(60°) past R vertically
    tip = (w / 2) / math.sin(math.radians(60))
    W, H = 2 * AP + 2 * m, 2 * (R + tip) + 2
    return svg((-W / 2, -H / 2, W, H), parts)


OUT.mkdir(parents=True, exist_ok=True)
(OUT / "a.svg").write_text(candidate_a())
(OUT / "b.svg").write_text(candidate_b())
(OUT / "c.svg").write_text(candidate_c())
