"""Models for namespace-delivery D6's blocks; arithmetic and simulation, nothing measured.

Status: DRAFT (2026-11-04). Numbers for docs/design/namespace-delivery.md D6.

  seal    The seal trigger against consensus's LogFull for log mixes with few
          positioned entries (the review's NB-1 cases), as designed and as the
          old positioned-only trigger.
  layout  Key-level seal, roll-up, squash and cut over one namespace: 40,000
          keys (the 5M corpus at 1/125, 52 B per record, 34 B per tombstone)
          for the churn models in days and the review's key-set, bulk-load
          and mass-delete shapes, and 1,200 keys for small namespaces. Answers
          are persisted blocks served whole, the block holding R + 1 trimmed
          to revisions above R.
  files   Seal files, roll-ups and tables over a group of namespaces, against
          the seal-file packing the roll-up replaces: bytes written, files
          created, and dead bytes held.

Run: python3 -I docs/design/namespace-delivery-blocks-model.py [seal|layout|files]
"""

from __future__ import annotations

import random
import sys
from bisect import bisect_left
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
ROLLUP = 16  # seals between per-namespace roll-ups of level-0 sections
CARRY_BYTES, CARRY_MAX = (
    4096,
    2,
)  # a smaller roll-up stays a seal-file section, twice at most
KEYS_5M = 5_000_000
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
    cap: float = 2.0  # disk cap: blocks above the base at most cap * alpha * live
    seal_every: int = SEAL
    rollup: int = ROLLUP
    pos: int = 0
    seals: int = 0
    level0: list[Block] = field(default_factory=list)  # sections of seal files
    carries: int = 0
    over_cap: float = 0.0  # worst blocks above the base / (cap * alpha * live)
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
        if self.pos % self.seal_every == 0:
            self.seal()

    def seal(self) -> None:
        recs, self.unsealed = self.unsealed, {}
        size = nbytes(recs)
        self.level0.append(Block(self.pos - self.seal_every + 1, self.pos, recs, size))
        self.written += size
        self.seals += 1
        limit = self.budget() / 4
        if self.seals % self.rollup and sum(b.size for b in self.level0) < limit:
            return  # roll up every `rollup` seals, or once level-0 reaches L
        merged: Recs = {}
        for b in self.level0:
            merged.update(b.recs)
        rolled = Block(self.level0[0].a, self.pos, merged, nbytes(merged))
        periodic = self.seals % self.rollup == 0
        if (
            periodic
            and rolled.size < min(CARRY_BYTES, limit)
            and self.carries < CARRY_MAX
        ):
            self.carries += 1  # carried: one section of this seal file
            self.level0 = [rolled]
            self.written += rolled.size
            return
        self.carries = 0
        self.blocks.append(rolled)
        self.level0 = []
        fresh = {id(self.blocks[-1])}
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
            fresh.add(id(self.blocks[-1]))
        self.written += sum(b.size for b in self.blocks if id(b) in fresh)
        before = self.disk()
        cut = False
        while len(self.blocks) >= 2:  # the newest block is never cut
            younger = sum(b.size for b in self.blocks[1:])
            if younger >= self.budget():
                rule = "cost"
            elif younger + self.blocks[0].size > self.cap * self.budget():
                rule = "cap"
            else:
                break
            self.cuts[rule] += 1
            cut = True
            oldest = self.blocks.pop(0)
            for k, (p, t) in oldest.recs.items():
                if t:
                    self.base.pop(k, None)
                else:
                    self.base[k] = p
            self.base_p = oldest.b
        if cut:  # one base written per roll-up
            self.written += len(self.base) * RECORD
            self.peak_cut = max(self.peak_cut, before + len(self.base) * RECORD)
        held = sum(b.size for b in self.blocks)
        self.over_cap = max(self.over_cap, held / max(self.cap * self.budget(), 1))
        self.peak = max(self.peak, self.disk())

    def disk(self) -> int:
        return len(self.base) * RECORD + sum(b.size for b in self.blocks + self.level0)

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
        suffix: Recs = {}  # level-0 sections merged at answer time
        for b in self.level0:
            if b.b > r:
                read += b.size
                suffix.update(b.recs)
        sent += nbytes({k: v for k, v in suffix.items() if v[0] > r})
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
    alpha: float,
    initial: int,
    steps: int,
    gen: Gen,
    ages: list[int],
    samples: int = 20,
    seal_every: int = SEAL,
) -> tuple[Namespace, Report]:
    rng = random.Random(1)
    ns = Namespace(alpha, seal_every=seal_every)
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
            stores += [ns.pos - k * ns.seal_every for k in (2, 8, 16, 32)]
            if ns.base_p:
                stores += [
                    ns.base_p - 1,
                    ns.base_p - 4 * ns.seal_every,
                ]  # just below the cut
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


def keyset_of(keys: int, frac: float) -> Gen:
    return lambda ns, rng: (rng.randrange(int(keys * frac)), False)


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


def mass_delete(keys: int) -> Gen:
    def gen(ns: Namespace, rng: random.Random) -> tuple[int, bool]:
        if ns.pos < int(0.9 * keys):
            return ns.pos, True
        return keys - 1 - rng.randrange(keys // 10), False

    return gen


def line(
    name: str, ns: Namespace, rep: Report, unit: float, label: str, c: int = C
) -> None:
    per = unit if label == "days" else 10_000  # a day's positions, or 10,000
    mean = sum(rep.over) / len(rep.over) if rep.over else 0.0
    rmean = sum(rep.read) / len(rep.read) if rep.read else 0.0
    print(
        f"  {name}: cut reaches back {min(rep.reach) / unit:,.1f}-{max(rep.reach) / unit:,.1f} {label}; "
        f"cuts by cost {ns.cuts['cost']}, by cap {ns.cuts['cap']}; "
        f"worst blocks / cap {ns.over_cap:.2f}"
    )
    print(
        f"     disk peak {ns.peak / c:.2f} C, during a cut {ns.peak_cut / c:.2f} C; "
        f"written {ns.written / c:.1f} C, {ns.written * per / ns.pos / c:.2f} C per {'day' if label == 'days' else '10,000 positions'}"
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
    ages = [500, 1000, 2000, 4000, 20_000]
    ns, rep = simulate(1.0, KEYS, 200_000, mass_delete(KEYS), ages)
    line("mass delete of 90% of the keys, alpha 1", ns, rep, 1, "positions")
    small = 1200  # the 5M corpus over 4,096 namespaces
    for every in (60, 600):
        ages = [100, 300, 1000, 3000]
        ns, rep = simulate(
            1.0, small, 60_000, mass_delete(small), ages, seal_every=every
        )
        line(
            f"{small:,} keys, seals of {every} positions, 90% deleted, alpha 1",
            ns,
            rep,
            1,
            "positions",
            small * RECORD,
        )
        ns, rep = simulate(
            1.0, small, 60_000, keyset_of(small, 1.0), ages, seal_every=every
        )
        line(
            f"{small:,} keys, seals of {every} positions, uniform rewrites, alpha 1",
            ns,
            rep,
            1,
            "positions",
            small * RECORD,
        )


# --- files -----------------------------------------------------------------

MIB = 1 << 20
TABLE_EVERY = (
    16  # seals between `layout` tables, about one checkpoint interval at T8's rate
)


@dataclass
class Sec:
    keys: frozenset[int]
    size: int
    file: int
    ns: int


@dataclass
class Group:
    """Many namespaces under one seal sequence, with the physical placement of blocks in files."""

    packing: (
        bool  # the 2026-11-04 fold's packing: outputs under 1 MiB stay in seal files
    )
    sizes: list[int]
    zipf: float
    next_file: int = 0
    files: dict[int, list[int]] = field(
        default_factory=dict
    )  # file -> [bytes, live bytes, is a seal file]
    members: dict[int, set[int]] = field(default_factory=dict)  # file -> section ids
    secs: dict[int, Sec] = field(default_factory=dict)
    next_sec: int = 0
    written: dict[str, int] = field(
        default_factory=lambda: dict.fromkeys(("level-0", "merge", "base", "copy"), 0)
    )
    created: int = 0
    samples: list[tuple[int, int, int]] = field(
        default_factory=list
    )  # (files, bytes, live bytes)

    def new_file(self, seal_file: bool) -> int:
        self.next_file += 1
        self.files[self.next_file] = [0, 0, int(seal_file)]
        self.members[self.next_file] = set()
        self.created += 1
        return self.next_file

    def place(self, keys: frozenset[int], file: int, ns: int) -> int:
        self.next_sec += 1
        size = len(keys) * RECORD
        self.secs[self.next_sec] = Sec(keys, size, file, ns)
        self.files[file][0] += size
        self.files[file][1] += size
        self.members[file].add(self.next_sec)
        return self.next_sec

    def drop(self, sid: int) -> None:
        sec = self.secs.pop(sid)
        self.files[sec.file][1] -= sec.size
        self.members[sec.file].discard(sid)

    def output(self, keys: frozenset[int], seal_file: int, kind: str, ns: int) -> int:
        """Packing keeps outputs under 1 MiB in the seal file; the roll-up gives every output its own file."""
        self.written[kind] += len(keys) * RECORD
        if self.packing and len(keys) * RECORD < MIB:
            return self.place(keys, seal_file, ns)
        return self.place(keys, self.new_file(False), ns)


def settle(
    g: Group, ns: int, items: list[int | frozenset[int]], base: int, sf: int
) -> tuple[list[int], int]:
    """Squash and cut one namespace in memory, then write only the blocks and base that result."""

    def keys(x: int | frozenset[int]) -> frozenset[int]:
        return g.secs[x].keys if isinstance(x, int) else x

    budget = (
        g.sizes[ns] * RECORD
    )  # alpha 1 and no deletes: |st(s)| is the namespace's size
    dropped: list[int] = []
    while len(items) >= 2:
        older, younger = items[-2], items[-1]
        if len(keys(older)) >= RATIO * len(keys(younger)):
            break
        merged = keys(older) | keys(younger)
        if len(merged) * RECORD > budget / 4 and len(merged) > max(
            len(keys(older)), len(keys(younger))
        ):
            break
        dropped += [x for x in (older, younger) if isinstance(x, int)]
        items[-2:] = [merged]
    base_keys: frozenset[int] | None = None
    while len(items) >= 2 and sum(len(keys(x)) for x in items[1:]) * RECORD >= budget:
        oldest = items.pop(0)
        base_keys = (keys(base) if base_keys is None else base_keys) | keys(oldest)
        if isinstance(oldest, int):
            dropped.append(oldest)
    out = [x if isinstance(x, int) else g.output(x, sf, "merge", ns) for x in items]
    if base_keys is not None:
        dropped.append(base)
        base = g.output(base_keys, sf, "base", ns)
    for sid in dropped:
        g.drop(sid)
    return out, base


@dataclass(frozen=True)
class Shape:
    name: str
    period: int = 16  # roll-up period in seals; 0 is the packing
    stagger: bool = False  # namespace N rolls up when (ordinal + N) % period == 0
    carry_bytes: int = 0  # a rolled-up level-0 below this stays one seal-file section
    carry_max: int = 0  # periods in a row a namespace may carry


SHAPES = (
    Shape("packing (2026-11-04 fold)", period=0),
    Shape("roll-up every 16 seals, a file per output"),
    Shape(
        "staggered, carry under 4 KiB for 2 periods",
        stagger=True,
        carry_bytes=4096,
        carry_max=2,
    ),
    Shape("staggered, period 64", period=64, stagger=True),
)


@dataclass
class Run:
    written: dict[str, int]
    per_seal: float  # files created per seal, second half
    largest: int  # largest files created at one seal: one sync group
    on_disk: int  # most files on disk at any seal, before the collector
    dead: float  # worst dead share at any seal, before the collector


def files_run(
    shape: Shape, sizes: list[int], zipf: float, seals: int = 800, txs: int = 2048
) -> Run:
    rng = random.Random(1)
    g = Group(shape.period == 0, sizes, zipf)
    n = len(sizes)
    cum: list[float] = []
    total = 0.0
    for r in range(n):
        total += 1 / (r + 1) ** zipf
        cum.append(total)
    first = g.new_file(True)
    bases = [
        g.output(frozenset(range(k)), first, "base", i) for i, k in enumerate(sizes)
    ]
    for kind in g.written:
        g.written[kind] = 0  # the first bases are not churn
    g.written["carry"] = 0
    ladders: list[list[int]] = [[] for _ in range(n)]
    level0: list[list[int]] = [[] for _ in range(n)]
    touched_period = [False] * n
    carries = [0] * n
    created: list[int] = []
    on_disk = 0
    dead = 0.0
    for seal in range(
        1, seals + 1
    ):  # `seal` is the seal ordinal, kept in apply's state
        before = g.created
        touched: dict[int, set[int]] = {}
        for _ in range(txs):
            ns = min(bisect_left(cum, rng.random() * total), n - 1)
            k = sizes[ns]
            key = (
                rng.randrange(max(1, k // 100))
                if rng.random() < 0.8
                else rng.randrange(k)
            )
            touched.setdefault(ns, set()).add(key)
        sf = g.new_file(True)
        for ns, keys in touched.items():
            g.written["level-0"] += len(keys) * RECORD
            level0[ns].append(g.place(frozenset(keys), sf, ns))
            touched_period[ns] = True
        for ns in range(n):
            if not level0[ns]:
                continue
            if g.packing:
                items: list[int | frozenset[int]] = [*ladders[ns], *level0[ns]]
                level0[ns] = []
                ladders[ns], bases[ns] = settle(g, ns, items, bases[ns], sf)
                continue
            limit = sizes[ns] * RECORD / 4
            held = sum(g.secs[sid].size for sid in level0[ns])
            periodic = (seal + (ns if shape.stagger else 0)) % shape.period == 0
            if not periodic and held < limit:
                continue
            merged = frozenset().union(*(g.secs[sid].keys for sid in level0[ns]))
            for sid in level0[ns]:
                g.drop(sid)
            small = len(merged) * RECORD < min(shape.carry_bytes, limit)
            if (
                periodic
                and small
                and touched_period[ns]
                and carries[ns] < shape.carry_max
            ):
                carries[ns] += 1  # written back as one section of this seal file
                g.written["carry"] += len(merged) * RECORD
                level0[ns] = [g.place(merged, sf, ns)]
            else:
                carries[ns] = 0
                level0[ns] = []
                ladders[ns], bases[ns] = settle(
                    g, ns, [*ladders[ns], merged], bases[ns], sf
                )
            touched_period[ns] = False
        if g.packing:  # copy live sections forward out of seal files under half live
            for f, (size, live, is_seal) in list(g.files.items()):
                if f == sf or not is_seal or not live or 2 * live >= size:
                    continue
                for sid in list(g.members[f]):
                    sec = g.secs[sid]
                    g.drop(sid)
                    g.written["copy"] += sec.size
                    moved = g.place(sec.keys, sf, sec.ns)
                    for lst in (ladders[sec.ns], level0[sec.ns]):
                        if sid in lst:
                            lst[lst.index(sid)] = moved
                    if bases[sec.ns] == sid:
                        bases[sec.ns] = moved
        created.append(g.created - before)
        if seal > seals // 2:
            size = sum(v[0] for v in g.files.values())
            on_disk = max(on_disk, len(g.files))
            dead = max(dead, (size - sum(v[1] for v in g.files.values())) / size)
        if seal % (shape.period or TABLE_EVERY) == 0:  # a table, then the collector
            for f in [f for f, v in g.files.items() if not v[1] and f != sf]:
                del g.files[f]  # it holds no section the table reaches
                del g.members[f]
    half = created[seals // 2 :]
    return Run(dict(g.written), sum(half) / len(half), max(half), on_disk, dead)


def files_main() -> None:
    populations = (
        ("4,096 namespaces of 1,220 keys", [KEYS_5M // 4096] * 4096),
        ("64 namespaces of 78,125 keys", [KEYS_5M // 64] * 64),
    )
    seals = 800
    raw = seals * 2048 * RECORD
    for label, sizes in populations:
        for zipf in (0.8, 1.0, 1.3):
            for shape in SHAPES:
                r = files_run(shape, sizes, zipf, seals)
                w = r.written
                print(f"{label}, zipf {zipf}, {shape.name}:")
                print(
                    f"   written {sum(w.values()) / raw:.2f}x the raw changes: level-0 {w['level-0'] / raw:.2f}, "
                    f"merges {w['merge'] / raw:.2f}, base rewrites {w['base'] / raw:.2f}, "
                    f"carried {w['carry'] / raw:.2f}, copied forward {w['copy'] / raw:.2f}"
                )
                print(
                    f"   files per seal {r.per_seal:.1f}, largest sync group {r.largest:,} files "
                    f"(Linux: {r.largest + 1:,} flushes; macOS: 1); "
                    f"files on disk at most {r.on_disk:,}; dead share at most {r.dead:.1%}"
                )


if __name__ == "__main__":
    which = sys.argv[1] if len(sys.argv) > 1 else "all"
    if which in ("seal", "all"):
        seal_main()
    if which in ("layout", "all"):
        layout_main()
    if which in ("files", "all"):
        files_main()
