"""A model of option 2's root path (docs/design/authority-pages.md, "Agent and relay rules").

One namespace, positions 1..n, one change each. The authority's root entries name the digest of the
state at the positions a pass reached. A relay mirrors records and root entries and answers an agent
at position a with the newest root entry r at or below its head h and the records a..h. The agent
keeps its state at its last verified root v and the records it published since, so it can check any
root at or past v. It publishes only behind a root it has checked, and an unverified tail only while
that check is within BOUND_S seconds. ROOT_MODEL_RULES=strip models the earlier rules: a relay whose
own build differs from the root strips it, and an agent publishes a rootless answer unverified.
"""

from __future__ import annotations

import hashlib
import os
from dataclasses import dataclass, field

BOUND_S = 10.0
STRIP = os.environ.get("ROOT_MODEL_RULES") == "strip"

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
        return Answer(r, root, changes[a:])


@dataclass
class Agent:
    verified: int = 0  # v: its last verified root's position
    verified_state: State = field(default_factory=dict)
    verified_at_s: float = 0.0
    since: list[Change] = field(default_factory=list)  # published past v
    failures: int = 0
    unverified: bool = False  # reported in health; the agent asks another source

    @property
    def position(self) -> int:
        return self.verified + len(self.since)

    @property
    def state(self) -> State:
        return applied(self.verified_state, self.since)

    def take(self, answer: Answer, now_s: float) -> bool:
        """Applies an answer by the rules; returns whether it published anything new."""
        if answer.root is None:
            if STRIP:
                self.since += answer.changes
                return bool(answer.changes)
            self.unverified = True
            return False
        r, start = answer.rooted_at, self.position
        if r < self.verified:
            self.unverified = True  # a root older than one it checked: a stale source
            return False
        seq = self.since + answer.changes
        at_root = applied(self.verified_state, seq[: r - self.verified])
        if digest(at_root) != answer.root:
            self.failures += 1
            self.unverified = (
                True  # keeps what it published; repairs from another source
            )
            return False
        if r > self.verified:
            self.verified_at_s = now_s
        # Through r is verified; the tail past it publishes only within the bound.
        fresh = now_s - self.verified_at_s <= BOUND_S
        end = start + len(answer.changes) if fresh else max(start, r)
        self.since = seq[r - self.verified : end - self.verified]
        self.verified, self.verified_state = r, at_root
        self.unverified = not fresh
        return end > start

    def repair(self, source: Relay, now_s: float) -> bool:
        """A full copy from another source at its newest root, checked before it replaces the state."""
        fresh = Agent()
        fresh.take(source.answer(0), now_s)
        if fresh.unverified or fresh.verified == 0:
            return False
        self.verified, self.verified_state, self.since = (
            fresh.verified,
            fresh.verified_state,
            fresh.since,
        )
        self.verified_at_s, self.unverified = fresh.verified_at_s, False
        return True
