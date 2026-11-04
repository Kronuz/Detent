"""Arithmetic model of namespace-delivery blocks; nothing here is measured.

Status: DRAFT (2026-11-03). Sizes for docs/design/namespace-delivery.md D6,
from a stated churn model over the 5M corpus treated as one namespace.
Run: python3 -I docs/design/namespace-delivery-blocks-model.py
"""

from __future__ import annotations

import math
import sys
from collections.abc import Iterator
from dataclasses import dataclass

STATE = 261_337_255  # measured: 5M snapshot logical wire bytes (benchmarks/system-capacity-5m-20261024.json)
KEYS = 5_000_000
RECORD = STATE / KEYS  # 52.3 B per live record, the measured average
TOMBSTONE = 34.0  # estimate: key (~24 B), revision, framing
HOT = 50_000  # 1% of the keys
RING = 4096  # authority ring, transactions (change_history.hh:9)
RATIO = 4  # adjacent blocks merge while the older is under RATIO times the younger
MB = 1e6


@dataclass(frozen=True)
class Churn:
    name: str
    # Key writes per day, one key per transaction (the worst case for the ring).
    writes: float
    hot_share: float  # share of writes to the HOT keys; the rest are uniform over KEYS
    creates: float  # new keys per day
    deletes: float  # deleted keys per day

    def missing(self, days: float) -> float:
        """Expected bytes of distinct keys written in a window: values plus tombstones."""
        hot = HOT * (1 - math.exp(-self.hot_share * self.writes * days / HOT))
        cold = KEYS * (1 - math.exp(-(1 - self.hot_share) * self.writes * days / KEYS))
        live = min(KEYS, hot + cold + self.creates * days)
        return live * RECORD + self.deletes * days * TOMBSTONE

    def raw(self, days: float) -> float:
        """Bytes of every change in a window, uncompacted (what D6's old W counted)."""
        return (
            self.writes + self.creates
        ) * days * RECORD + self.deletes * days * TOMBSTONE

    def seal(self) -> float:
        """Days between seals: half the ring, or an hour when that comes first."""
        return min(1 / 24, (RING // 2) / (self.writes + self.creates + self.deletes))


CHURNS = (
    Churn("moderate", 1_000_000, 0.8, 10_000, 10_000),
    Churn("light", 100_000, 0.8, 1_000, 1_000),
    Churn("hot-heavy", 10_000_000, 0.99, 1_000, 1_000),
)


Block = tuple[float, float, float]


def simulate(
    churn: Churn, alpha: float, samples: list[float], cap: float = 4
) -> Iterator[tuple[float, list[Block], float]]:
    """Yield (base end age, blocks above the base, bytes written per day) at each sample day.

    Blocks are (start age, end age, bytes). The base is the live state, without tombstones.
    After each seal, adjacent blocks merge while the older is under RATIO times the younger
    and the merge stays within alpha * STATE / cap. Then the oldest block joins the base while
    the answer from its end is at least alpha * STATE (the cost cut) or the blocks above the
    base hold more than alpha * STATE (the disk bound). Seals are at least a minute apart here,
    which only coarsens the newest block.
    """
    step = max(churn.seal(), 1 / 1440)
    stack: list[Block] = []
    base_end = 0.0
    t = 0.0
    written = 0.0
    pending = sorted(samples)
    while pending:
        size = churn.missing(step)
        stack.append((t, t + step, size))
        t += step
        written += size
        while len(stack) >= 2 and stack[-2][2] < RATIO * stack[-1][2]:
            merged = (
                stack[-2][0],
                stack[-1][1],
                churn.missing(stack[-1][1] - stack[-2][0]),
            )
            if merged[2] > alpha * STATE / cap:
                break  # the cut drops whole blocks
            stack[-2:] = [merged]
            written += merged[2]
        # Cut by cost (the answer from the oldest block's end, deduplicated, is at least alpha * STATE)
        # or by disk (blocks above the base hold more than alpha * STATE).
        while len(stack) >= 2 and (
            churn.missing(t - stack[0][1]) >= alpha * STATE
            or sum(b[2] for b in stack) > alpha * STATE
        ):
            base_end = stack.pop(0)[1]
            written += STATE
        if t >= pending[0]:
            pending.pop(0)
            yield t - base_end, [(t - a, t - b, n) for a, b, n in stack], written / t


def raw(churn: Churn, blocks: list[Block], age: float) -> float:
    """Unmerged answer over the missing bytes: R's block trimmed, every younger block whole."""
    sent = 0.0
    for start, end, size in blocks:
        if end < age:
            sent += churn.missing(age - end) if start > age else size
    return sent / churn.missing(age)


def main() -> None:
    # No block outgrows alpha * STATE / cap.
    cap = float(sys.argv[1]) if len(sys.argv) > 1 else 4.0
    ages = (("1 day", 1.0), ("1 week", 7.0), ("1 month", 30.0), ("6 months", 182.0))
    for churn in CHURNS:
        reach = STATE / churn.raw(1.0)
        print(
            f"== {churn.name}: {churn.writes:,.0f} writes/day ({churn.hot_share:.0%} to {HOT:,} keys), "
            f"{churn.creates:,.0f} creates and {churn.deletes:,.0f} deletes/day; seal every "
            f"{churn.seal() * 1440:.1f} min; old W = 1 x C reaches back {reach:.2f} days"
        )
        for label, age in ages:
            print(
                f"   missing after {label:8s}: {churn.missing(age) / MB:7.1f} MB = {churn.missing(age) / STATE:.2f} C"
            )
        for alpha in (0.5, 1.0, 2.0):
            cut_lo, cut_hi, disk_hi, blocks_hi, per_day = math.inf, 0.0, 0.0, 0, 0.0
            raw_hi, copy_hi = 1.0, 1.0
            # 40 instants over 10 days after a year: the merge sawtooth
            for cut, blocks, per_day in simulate(
                churn, alpha, [365 + k * 0.25 for k in range(40)], cap
            ):
                cut_lo, cut_hi = min(cut_lo, cut), max(cut_hi, cut)
                disk_hi = max(disk_hi, STATE + sum(b[2] for b in blocks))
                blocks_hi = max(blocks_hi, len(blocks))
                raw_hi = max(
                    [raw_hi] + [raw(churn, blocks, a) for _, a in ages if a < cut]
                )
                copy_hi = max(copy_hi, 1 + sum(b[2] for b in blocks) / STATE)
            answers = ", ".join(
                f"{label} {churn.missing(age) / MB:.0f}"
                if age < cut_lo
                else f"{label} copy {STATE / MB:.0f}"
                for label, age in ages
            )
            print(
                f"  alpha {alpha:g}: cut {cut_lo:6.1f}-{cut_hi:6.1f} days back; disk at most "
                f"{disk_hi / MB:5.0f} MB = {disk_hi / STATE:.2f} C in base + {blocks_hi} blocks; "
                f"block bytes written {per_day / MB:.0f} MB/day"
            )
            print(f"            answers (MB): {answers}")
            print(
                f"            blocks sent unmerged: at most {raw_hi:.2f}x the missing bytes; "
                f"base plus blocks for a full copy: at most {copy_hi:.2f} C"
            )


if __name__ == "__main__":
    main()
