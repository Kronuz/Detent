"""Models for namespace-delivery D6's blocks; arithmetic and simulation, nothing measured.

Status: DRAFT (2026-11-04). Numbers for docs/design/namespace-delivery.md D6.

  seal    The seal trigger against consensus's LogFull for log mixes with few
          positioned entries (the review's NB-1 cases), as designed and as the
          old positioned-only trigger.
  layout  Key-level seal, squash and cut over one namespace of 40,000 keys
          (the 5M corpus at 1/125, 52 B per record, 34 B per tombstone): the
          churn models in days, and the review's key-set, bulk-load and
          mass-delete shapes in positions. Answers are persisted blocks served
          whole, the block holding R + 1 trimmed to revisions above R.

Run: python3 -I docs/design/namespace-delivery-blocks-model.py [seal|layout]
"""

from __future__ import annotations

import random
import sys
from collections.abc import Callable
from dataclasses import dataclass, field

# --- seal ------------------------------------------------------------------

SEAL_TX, SEAL_BYTES = 2048, 8 * 1024 * 1024  # positioned changes since the last seal
LOG_ENTRIES, LOG_BYTES = 65536, 64 * 1024 * 1024  # consensus Limits (types.h:58-59)
PROGRESS_ENTRIES, PROGRESS_BYTES = (
    LOG_ENTRIES // 8,
    LOG_BYTES // 8,
)  # log progress since the last seal
DUE_ENTRIES, DUE_BYTES = (
    LOG_ENTRIES // 2,
    LOG_BYTES // 2,
)  # checkpoint due (node.cc:511-513)


def seal_run(
    share: float, entry: int, positioned: int, progress: bool, n: int = 2_000_000
) -> str:
    """Replay entries; a checkpoint is due at half the log and may land only on a durable seal."""
    rng = random.Random(1)
    sizes = [0]  # prefix sums of entry bytes
    base = last_seal = 0
    since_tx = since_bytes = since_entries = since_log_bytes = 0
    for i in range(1, n + 1):
        is_pos = rng.random() < share
        size = positioned if is_pos else entry
        if i - base > LOG_ENTRIES or sizes[-1] - sizes[base] + size > LOG_BYTES:
            return (
                f"LogFull at entry {i:,}, last seal at {last_seal:,}: the group stops"
            )
        sizes.append(sizes[-1] + size)
        since_entries += 1
        since_log_bytes += size
        if is_pos:
            since_tx += 1
            since_bytes += size
        fire = since_tx >= SEAL_TX or since_bytes >= SEAL_BYTES
        if progress:
            fire = (
                fire
                or since_entries >= PROGRESS_ENTRIES
                or since_log_bytes >= PROGRESS_BYTES
            )
        if fire:
            last_seal = i
            since_tx = since_bytes = since_entries = since_log_bytes = 0
        if (
            i - base >= DUE_ENTRIES or sizes[i] - sizes[base] >= DUE_BYTES
        ) and last_seal > base:
            base = last_seal  # blocks durable at once, the best case for the gate
    return f"live for {n:,} entries; {n - base:,} entries above the checkpoint"


def seal_main() -> None:
    cases = (
        ("every entry a positioned 256 B write", 1.0, 256, 256),
        ("10% positioned, 90% NoChange 256 B", 0.10, 256, 256),
        ("3% positioned, 97% NoChange or refused 256 B", 0.03, 256, 256),
        ("20% positioned 4 KiB among refused 64 KiB batches", 0.20, 65536, 4096),
        ("1% positioned, a producer retrying a Conflict", 0.01, 2048, 2048),
        ("0% positioned, session churn only", 0.0, 128, 128),
    )
    for label, share, entry, positioned in cases:
        print(f"{label}:")
        print(
            f"   positioned-only trigger: {seal_run(share, entry, positioned, False)}"
        )
        print(f"   with log progress:       {seal_run(share, entry, positioned, True)}")


# --- layout ----------------------------------------------------------------

SCALE = 125
KEYS = 5_000_000 // SCALE
RECORD, TOMBSTONE = 52, 34
C = KEYS * RECORD
SEAL = 64  # positions per seal here; D6 seals every 2,048 group transactions (16 at this scale)
RATIO = 4
Recs = dict[int, tuple[int, bool]]  # key -> (position of its last write, is a delete)


def nbytes(recs: Recs) -> int:
    return sum(TOMBSTONE if t else RECORD for _, t in recs.values())


@dataclass
class Block:
    a: int
    b: int
    recs: Recs
    size: int


@dataclass
class Namespace:
    alpha: float
    cap: float = 2.0  # hard disk cap: blocks above the base at most cap * alpha * live
    pos: int = 0
    live: dict[int, int] = field(default_factory=dict)
    last: Recs = field(default_factory=dict)
    unsealed: Recs = field(default_factory=dict)
    base: dict[int, int] = field(default_factory=dict)
    base_p: int = 0
    blocks: list[Block] = field(default_factory=list)
    cuts: dict[str, int] = field(default_factory=lambda: {"cost": 0, "cap": 0})
    peak: int = 0
    peak_cut: int = 0
    written: int = 0

    def budget(self) -> float:
        return (
            self.alpha * len(self.live) * RECORD
        )  # alpha times |st(s)|, carried with each seal

    def write(self, key: int, delete: bool) -> None:
        if delete and key not in self.live:
            return  # an absent delete changes nothing (state.cc:952-953)
        self.pos += 1
        if delete:
            del self.live[key]
        else:
            self.live[key] = self.pos
        self.last[key] = self.unsealed[key] = (self.pos, delete)
        if self.pos % SEAL == 0:
            self.seal()

    def seal(self) -> None:
        recs, self.unsealed = self.unsealed, {}
        block = Block(self.pos - SEAL + 1, self.pos, recs, nbytes(recs))
        self.blocks.append(block)
        self.written += block.size
        limit = self.budget() / 4
        while len(self.blocks) >= 2:
            older, younger = self.blocks[-2], self.blocks[-1]
            if older.size >= RATIO * younger.size:
                break
            recs = dict(older.recs)
            recs.update(younger.recs)
            size = nbytes(recs)
            if size > limit and size > max(older.size, younger.size):
                break  # past the limit, only a merge that grows nothing proceeds
            self.blocks[-2:] = [Block(older.a, younger.b, recs, size)]
            self.written += size
        while len(self.blocks) >= 2:  # the newest block is never cut
            younger = sum(b.size for b in self.blocks[1:])
            if younger >= self.budget():
                rule = "cost"
            elif younger + self.blocks[0].size > self.cap * self.budget():
                rule = "cap"
            else:
                break
            self.cuts[rule] += 1
            before = self.disk()
            oldest = self.blocks.pop(0)
            for k, (p, t) in oldest.recs.items():
                if t:
                    self.base.pop(k, None)
                else:
                    self.base[k] = p
            self.base_p = oldest.b
            self.written += len(self.base) * RECORD
            self.peak_cut = max(self.peak_cut, before + len(self.base) * RECORD)
        self.peak = max(self.peak, self.disk())

    def disk(self) -> int:
        return len(self.base) * RECORD + sum(b.size for b in self.blocks)

    def answers(self, r: int) -> tuple[int, int, int]:
        """(exact missing bytes, whole-block answer bytes, server bytes read) for a store at r."""
        exact = sum(TOMBSTONE if t else RECORD for p, t in self.last.values() if p > r)
        sent = read = 0
        for b in self.blocks:
            if b.b <= r:
                continue
            read += b.size
            sent += (
                nbytes({k: v for k, v in b.recs.items() if v[0] > r})
                if b.a <= r
                else b.size
            )
        sent += nbytes(
            {k: v for k, v in self.unsealed.items() if v[0] > r}
        )  # the ring, deduplicated
        return exact, sent, read


Gen = Callable[[Namespace, random.Random], tuple[int, bool]]


@dataclass
class Report:
    reach: list[int] = field(default_factory=list)
    denied: float = 0.0  # worst copy / whole-block answer for a store below the cut
    over: list[float] = field(
        default_factory=list
    )  # whole-block answer / exact, above the cut
    read: list[float] = field(default_factory=list)  # bytes read / exact, above the cut
    above_copy: float = 0.0  # worst whole-block answer / copy, above the cut


def simulate(
    alpha: float, initial: int, steps: int, gen: Gen, ages: list[int], samples: int = 20
) -> tuple[Namespace, Report]:
    rng = random.Random(1)
    ns = Namespace(alpha)
    for k in range(initial):
        ns.live[k] = 0
    ns.base = dict(ns.live)
    rep = Report()
    for i in range(1, steps + 1):
        key, delete = gen(ns, rng)
        ns.write(key, delete)
        if i >= steps // 2 and i % (steps // (2 * samples)) == 0:
            rep.reach.append(ns.pos - ns.base_p)
            copy = len(ns.live) * RECORD
            stores = [ns.pos - age for age in ages]
            if ns.base_p:
                stores += [ns.base_p - 1, ns.base_p - 4 * SEAL]  # just below the cut
            for r in stores:
                if r < 0:
                    continue
                exact, sent, read = ns.answers(max(r, ns.base_p))
                if r < ns.base_p:
                    whole = ns.answers(r)[1] + nbytes(
                        {k: v for k, v in ns.last.items() if r < v[0] <= ns.base_p}
                    )
                    rep.denied = max(rep.denied, copy / max(whole, 1))
                elif exact:
                    rep.over.append(sent / exact)
                    rep.read.append(read / exact)
                    rep.above_copy = max(rep.above_copy, sent / max(copy, 1))
    return ns, rep


@dataclass(frozen=True)
class Churn:
    name: str
    per_day: int  # key writes per day at this scale
    hot_share: float
    turnover: (
        float  # share of writes that create a key, and the same share that delete one
    )


CHURNS = (
    Churn("moderate", 1_000_000 // SCALE, 0.8, 0.01),
    Churn("light", 100_000 // SCALE, 0.8, 0.01),
    Churn("hot-heavy", 10_000_000 // SCALE, 0.99, 0.0001),
)


def churn_gen(c: Churn) -> Gen:
    state = {"next": KEYS}

    def gen(ns: Namespace, rng: random.Random) -> tuple[int, bool]:
        roll = rng.random()
        if roll < c.turnover:
            state["next"] += 1
            return state["next"], False
        if roll < 2 * c.turnover:
            return rng.randrange(state["next"]), True
        if roll < 2 * c.turnover + c.hot_share:
            return rng.randrange(KEYS // 100), False
        return rng.randrange(state["next"]), False

    return gen


def keyset(frac: float) -> Gen:
    return lambda ns, rng: (rng.randrange(int(KEYS * frac)), False)


def bulk() -> Gen:
    state = {"next": 0}

    def gen(ns: Namespace, rng: random.Random) -> tuple[int, bool]:
        if rng.random() < 0.7 and state["next"] < KEYS:
            state["next"] += 1
            return state["next"] - 1, False
        return rng.randrange(max(1, state["next"])), False

    return gen


def mass_delete(ns: Namespace, rng: random.Random) -> tuple[int, bool]:
    if ns.pos < int(0.9 * KEYS):
        return ns.pos, True
    return KEYS - 1 - rng.randrange(KEYS // 10), False


def line(name: str, ns: Namespace, rep: Report, unit: float, label: str) -> None:
    per = unit if label == "days" else 10_000  # a day's positions, or 10,000
    mean = sum(rep.over) / len(rep.over) if rep.over else 0.0
    rmean = sum(rep.read) / len(rep.read) if rep.read else 0.0
    print(
        f"  {name}: cut reaches back {min(rep.reach) / unit:,.1f}-{max(rep.reach) / unit:,.1f} {label}; "
        f"cuts by cost {ns.cuts['cost']}, by cap {ns.cuts['cap']}"
    )
    print(
        f"     disk peak {ns.peak / C:.2f} C, during a cut {ns.peak_cut / C:.2f} C; "
        f"written {ns.written / C:.1f} C, {ns.written * per / ns.pos / C:.2f} C per {'day' if label == 'days' else '10,000 positions'}"
    )
    print(
        f"     above the cut: sent {mean:.2f}x missing on average, worst {max(rep.over, default=0):.2f}x; "
        f"read {rmean:.2f}x, worst {max(rep.read, default=0):.2f}x; worst sent/copy {rep.above_copy:.2f}"
    )
    print(f"     below the cut: worst copy / whole-block answer {rep.denied:.2f}")


def layout_main() -> None:
    days = {"moderate": 200, "light": 400, "hot-heavy": 150}
    for c in CHURNS:
        day = c.per_day
        ages = [day, 7 * day, 30 * day, 182 * day]
        for alpha in (0.5, 1.0, 2.0):
            ns, rep = simulate(alpha, KEYS, days[c.name] * day, churn_gen(c), ages)
            line(f"{c.name}, alpha {alpha:g}", ns, rep, day, "days")
    n = 600_000
    grid = [n // 100, n // 20, n // 10, n // 4]
    for frac in (0.15, 0.3, 0.5, 0.75, 0.95):
        ns, rep = simulate(1.0, KEYS, n, keyset(frac), grid)
        line(f"uniform key set of {frac:.2f} C, alpha 1", ns, rep, 1, "positions")
    ns, rep = simulate(1.0, 0, 120_000, bulk(), [1200, 6000, 12000, 30000])
    line("bulk load from an empty base, alpha 1", ns, rep, 1, "positions")
    ns, rep = simulate(1.0, KEYS, 200_000, mass_delete, [500, 1000, 2000, 4000, 20_000])
    line("mass delete of 90% of the keys, alpha 1", ns, rep, 1, "positions")


if __name__ == "__main__":
    which = sys.argv[1] if len(sys.argv) > 1 else "all"
    if which in ("seal", "all"):
        seal_main()
    if which in ("layout", "all"):
        layout_main()
