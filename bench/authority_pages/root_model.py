"""A model of option 2's root path (docs/design/authority-pages.md, "Agent and relay rules").

One namespace, positions 1..n, one change each. The authority's root entries name the digest of the
state at the positions a pass reached. A relay mirrors records and root entries and answers an agent
at position a with the newest root entry r at or below its head h and the records a..h. The agent
keeps its state at its last verified root v and the records it published since, so it can check any
root at or past v. It publishes only behind a root it has checked, and a tail past it only while its
oldest unverified record was published at most BOUND_S seconds ago (rule 3's clock: zero when
nothing is unverified). ROOT_MODEL_RULES=strip models the earlier rules: a relay whose own build
differs from the root strips it, and an agent publishes a rootless answer unverified.
ROOT_MODEL_RULES=last-check models round 2's clock, which ran from the last check of a newer root;
any-check, its other reading, from any passing check, the held root's included.

The leader side models the pass interval (a minimum per namespace, measured from the last pass's
start) and step-down (a result of an old term is never proposed).
"""

from __future__ import annotations

import hashlib
import os
from dataclasses import dataclass, field

BOUND_S = 15.0  # the derived bound (docs/design/authority-pages.md, "Rule 3's bound")
RULES = os.environ.get("ROOT_MODEL_RULES", "")
STRIP = RULES == "strip"
LAST_CHECK = RULES in ("last-check", "any-check")

Change = tuple[str, int]
State = dict[str, int]


def digest(state: State) -> str:
    return hashlib.sha256(repr(sorted(state.items())).encode()).hexdigest()


def applied(state: State, changes: list[Change]) -> State:
    out = dict(state)
    for key, value in changes:
        out[key] = value
    return out


@dataclass
class Authority:
    changes: list[Change] = field(default_factory=list)  # position p is changes[p - 1]
    roots: dict[int, str] = field(default_factory=dict)  # committed root entries
    incarnation: int = 1

    def recreate(self) -> None:
        """The namespace is dropped and created again: a new incarnation, no records, no roots."""
        self.incarnation += 1
        self.changes, self.roots = [], {}

    def commit(self, *keys: str) -> None:
        for key in keys:
            self.changes.append((key, len(self.changes) + 1))

    def pass_root(self) -> None:
        """A pass reaches the head and its root entry commits."""
        p = len(self.changes)
        self.roots[p] = digest(applied({}, self.changes))


@dataclass
class Answer:
    rooted_at: (
        int  # the newest root entry the relay holds at or below its head; 0 when none
    )
    root: str | None
    changes: list[Change]  # a..h
    incarnation: int = 1


@dataclass
class Relay:
    authority: Authority
    wrong_at: int | None = None  # its mirror applies this position's value wrong
    roots_until: int | None = (
        None  # its mirror loses the root entries past this position
    )

    def answer(self, a: int) -> Answer:
        changes = list(self.authority.changes)
        if self.wrong_at is not None:
            key, value = changes[self.wrong_at - 1]
            changes[self.wrong_at - 1] = (key, value + 1000)
        held = {
            p: r
            for p, r in self.authority.roots.items()
            if self.roots_until is None or p <= self.roots_until
        }
        r = max(held, default=0)
        root = held.get(r)
        if STRIP and root is not None and digest(applied({}, changes[:r])) != root:
            root = None
        return Answer(r, root, changes[a:], self.authority.incarnation)


@dataclass
class Agent:
    verified: int = 0  # v: its last verified root's position
    verified_state: State = field(default_factory=dict)
    since: list[tuple[Change, float]] = field(
        default_factory=list
    )  # published past v, with when
    incarnation: int = 1
    checked_at_s: float = 0.0  # round 2's clock (LAST_CHECK only)
    failures: int = 0
    unverified: bool = False  # reported in health; the agent asks another source

    @property
    def position(self) -> int:
        return self.verified + len(self.since)

    @property
    def state(self) -> State:
        return applied(self.verified_state, [c for c, _ in self.since])

    def clock_s(self, now_s: float) -> float:
        """Rule 3's clock: the age of the oldest published, unverified record; zero when none."""
        if LAST_CHECK:
            return now_s - self.checked_at_s
        return now_s - self.since[0][1] if self.since else 0.0

    def take(self, answer: Answer, now_s: float) -> bool:
        """Applies an answer by the rules; returns whether it published anything new."""
        if answer.incarnation != self.incarnation:
            if answer.incarnation < self.incarnation:
                self.unverified = True  # a stale source
                return False
            fresh = Agent(
                incarnation=answer.incarnation
            )  # a new incarnation starts empty
            self.__dict__.update(fresh.__dict__)
        if answer.root is None:
            if STRIP:
                self.since += [(c, now_s) for c in answer.changes]
                return bool(answer.changes)
            self.unverified = True
            return False
        r, start = answer.rooted_at, self.position
        if r < self.verified:
            self.unverified = True  # a root older than one it checked: a stale source
            return False
        seq = self.since + [(c, now_s) for c in answer.changes]
        at_root = applied(self.verified_state, [c for c, _ in seq[: r - self.verified]])
        if digest(at_root) != answer.root:
            self.failures += 1
            self.unverified = (
                True  # keeps what it published; repairs from another source
            )
            return False
        if r > self.verified or RULES == "any-check":
            self.checked_at_s = now_s
        past = seq[
            r - self.verified :
        ]  # records past r: those it published, then the new ones
        held, new = past[: max(0, start - r)], past[max(0, start - r) :]
        self.verified, self.verified_state, self.since = r, at_root, held
        # Through r is verified; a tail past what it published goes out only within the bound.
        within = self.clock_s(now_s) <= BOUND_S
        if within:
            self.since = held + new
        self.unverified = not within
        return self.position > start

    def repair(self, source: Relay, now_s: float) -> bool:
        """A full copy from another source at its newest root, checked before it replaces the state."""
        fresh = Agent()
        fresh.take(source.answer(0), now_s)
        if fresh.unverified or fresh.verified == 0:
            return False
        self.__dict__.update(fresh.__dict__)
        return True


def pass_due_s(
    last_start_s: float | None, first_pending_s: float, interval_s: float
) -> float:
    """When a namespace's next pass may start: the interval is a minimum from the last pass's start,
    so a change to a namespace idle for longer than the interval passes on arrival."""
    if last_start_s is None:
        return first_pending_s
    return max(last_start_s + interval_s, first_pending_s)


@dataclass
class Leader:
    """The loop's side of step-down: a pass result carries the term it was built in."""

    term: int = 1
    leading: bool = True
    proposed: list[tuple[int, int]] = field(default_factory=list)  # (term, position)

    def step_down(self) -> None:
        self.leading = False

    def elected(self) -> None:
        self.term += 1
        self.leading = True

    def offer(self, built_term: int, position: int) -> bool:
        """A finished pass offers its root; only the current term's leader proposes it."""
        if not self.leading or built_term != self.term:
            return False
        self.proposed.append((built_term, position))
        return True
