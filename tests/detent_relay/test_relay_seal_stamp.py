"""The seal stamp on the wire, one check per row of its table (docs/mechanisms/relay-mirror-wait.md),
at a single-voter authority and at a relay mirroring it (bench/idle-wakeups' Pair).

Every next carries `seal`. With nothing new, an upstream answers it 204 at once while its own seal
is above the stamp, parked or new, and otherwise parks until a change or its wait; it keeps no seal
state. Every answer, 204 or 200, carries the upstream's seal (X-Detent-Seal, D165), checked at each
step against the upstream's layouts seal. A write to "b" makes the seal that moves no head of "a": the authority seals it after
seal_after (1 s), and the relay adopts that seal at its next sync. Every next here follows "a", so
only the seal can answer it early. The steps order the seal against each next by observing the
upstream's layouts seal, never by a race:
- current: a next stamped with the upstream's seal parks for its whole wait;
- older: a next stamped below it answers 204 at once;
- stale: the same stamp again answers 204 at once: the first answer left nothing behind;
- busy: of two nexts on one session, one parks and one answers 409 session_busy;
- parked: a next parked before the write answers 204 when the seal lands, before its wait;
- agent: a next stamped 2^64-1 (D164) parks through a seal, and a write to "a" answers it 200;
- ended: a closed session answers 410, and a next on a new session stamped with the seal parks.

    test_relay_seal_stamp.py BUILD
"""

from __future__ import annotations

import json
import subprocess
import sys
import threading
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "bench" / "idle-wakeups"))
from pair import CONTEXT, MIRROR, NAMES, READER, Pair

WAIT_MS = 1500  # a next that parks its whole wait answers no sooner than PARKED_S
PARKED_S = 1.2
AT_ONCE_S = 0.8  # an early 204, against waits of 1.5 s and more
AGENT = 2**64 - 1  # an agent's stamp: no seal is above it


def check(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


class Upstream:
    """One upstream's W5 routes and layouts seal, with the token it grants "a" and "b" to."""

    def __init__(self, pair: Pair, probe: Path, label: str, port: int, token: str) -> None:
        self.pair, self.probe, self.label, self.port, self.token = pair, probe, label, port, token

    def seal(self) -> int:
        status, body = self.pair.call(self.port, "/v2/history/layouts", {"namespaces": NAMES}, self.token)
        check(status == 200, f"{self.label}: layouts answered {status}")
        return int.from_bytes(body[24:32], "little")  # DTMIRR01, layout, entries, sequence, seal

    def heads(self) -> list[list[object]]:
        output = subprocess.check_output(
            [str(self.probe), self.pair.ca, str(self.port), self.token, CONTEXT["cluster"], "--heads", "a"],
            text=True,
            timeout=30,
        )
        incarnation, position = json.loads(output)["a"]
        return [["a", incarnation, position]]

    def open(self, names: list[list[object]]) -> str:
        body = {"version": 2, "profile": 1, "coding": "raw", "cluster": CONTEXT["cluster"], "namespaces": names}
        status, reply = self.pair.call(self.port, "/v2/distribution/session/open", body, self.token)
        check(status == 200, f"{self.label}: open answered {status}: {reply!r}")
        session: str = json.loads(reply)["session"]
        return session

    def next(self, session: str, names: list[list[object]], wait_ms: int, seal: int) -> tuple[int, float, int | None]:
        body = {
            "version": 2,
            "session": session,
            "cluster": CONTEXT["cluster"],
            "namespaces": names,
            "wait_ms": wait_ms,
            "seal": seal,
        }
        sent = time.monotonic()
        status, headers, _ = self.pair.exchange(self.port, "/v2/distribution/session/next", body, self.token)
        took = time.monotonic() - sent
        carried = headers.get("x-detent-seal")
        return status, took, int(carried) if carried is not None else None

    def close(self, session: str) -> int:
        status, _ = self.pair.call(
            self.port, "/v2/distribution/session/close", {"version": 2, "session": session}, self.token
        )
        return status

    def sealed_after(self, before: int) -> int:
        deadline = time.monotonic() + 30
        while (now := self.seal()) == before:
            check(time.monotonic() < deadline, f"{self.label}: the write was never sealed")
            time.sleep(0.01)
        return now


def write(pair: Pair, name: str) -> None:
    pair.sequence += 1
    receipt = pair.write(
        "/v2/transactions",
        {
            "generation": 1,
            "sequence": pair.sequence,
            "namespace": name,
            "operations": [{"key": f"{name}:k0", "value": {"default": pair.sequence}}],
        },
    )
    check(receipt.get("status") == "applied", f"transaction refused: {receipt}")


Answer = tuple[int, float, "int | None"]  # status, seconds, the answer's seal


def report(upstream: Upstream, step: str, answer: Answer, low: int, high: int | None = None) -> tuple[int, float]:
    """Prints the answer and checks that it carries the upstream's seal, between `low` and `high`."""
    status, took, carried = answer
    print(f"{upstream.label} {step}: {status} after {took * 1000:.0f} ms, seal {carried}", flush=True)
    high = low if high is None else high
    check(carried is not None and low <= carried <= high,
          f"{upstream.label} {step}: the answer's seal {carried}, not the upstream's {low}..{high}")
    return status, took


def parks(upstream: Upstream, step: str, answer: Answer, seal: int) -> None:
    status, took = report(upstream, step, answer, seal)
    check(status == 204 and took >= PARKED_S, f"{upstream.label} {step}: {status} after {took:.3f} s, not parked")


def at_once(upstream: Upstream, step: str, answer: Answer, seal: int) -> None:
    status, took = report(upstream, step, answer, seal)
    check(status == 204 and took < AT_ONCE_S, f"{upstream.label} {step}: {status} after {took:.3f} s, not at once")


def parked_across(upstream: Upstream, session: str, names: list[list[object]], wait_ms: int, stamp: int,
                  name: str) -> tuple[Answer, int, int]:
    """A next parked 0.2 s before a write to `name`; its answer, and the seal before and after."""
    answers: list[Answer] = []
    waiter = threading.Thread(target=lambda: answers.append(upstream.next(session, names, wait_ms, stamp)))
    waiter.start()
    time.sleep(0.2)
    before = upstream.seal()
    write(upstream.pair, name)
    waiter.join()
    return answers[0], before, upstream.seal()


def transitions(upstream: Upstream) -> None:
    names = upstream.heads()
    session = upstream.open(names)
    seal = upstream.seal()
    check(seal > 0, f"{upstream.label}: no seal yet")
    parks(upstream, "current", upstream.next(session, names, WAIT_MS, seal), seal)
    at_once(upstream, "older", upstream.next(session, names, 5000, seal - 1), seal)
    at_once(upstream, "stale", upstream.next(session, names, 5000, seal - 1), seal)

    answers: list[Answer] = []
    first = threading.Thread(target=lambda: answers.append(upstream.next(session, names, WAIT_MS, seal)))
    first.start()
    time.sleep(0.2)
    answers.append(upstream.next(session, names, WAIT_MS, seal))
    first.join()
    statuses = sorted(status for status, _, _ in answers)
    print(f"{upstream.label} busy: {statuses}", flush=True)
    check(statuses == [204, 409], f"{upstream.label} busy: {answers}")
    report(upstream, "busy, the parked one", next(a for a in answers if a[0] == 204), seal)

    stamp = upstream.seal()
    answer, before, after = parked_across(upstream, session, names, 8000, stamp, "b")
    status, took = report(upstream, "parked", answer, stamp + 1, after)
    check(after != before, f"{upstream.label} parked: answered before the seal")
    check(status == 204 and took < 5, f"{upstream.label} parked: {status} after {took:.3f} s")

    # D164: an agent's stamp. The write to b seals about 1 s in, inside the 4 s wait.
    answer, before, after = parked_across(upstream, session, names, 4000, AGENT, "b")
    status, took = report(upstream, "agent across a seal", answer, before + 1, after)
    check(status == 204 and took >= 3.5, f"{upstream.label} agent: {status} after {took:.3f} s, not parked")
    answer, before, after = parked_across(upstream, session, names, 8000, AGENT, "a")
    status, took = report(upstream, "agent and a change", answer, before, upstream.sealed_after(before))
    check(status == 200 and took < 5, f"{upstream.label} agent: {status} after {took:.3f} s, not the change")

    check(upstream.close(session) in (200, 204), f"{upstream.label}: close refused")
    status, _, _ = upstream.next(session, names, WAIT_MS, AGENT)
    print(f"{upstream.label} ended: {status}", flush=True)
    check(status == 410, f"{upstream.label} ended: a closed session answered {status}")
    names = upstream.heads()
    seal = upstream.seal()
    parks(upstream, "reopened", upstream.next(upstream.open(names), names, WAIT_MS, seal), seal)


def main() -> int:
    build = Path(sys.argv[1]).resolve()
    probe = build / "tests/detent_relay/test_relay_probe"
    with Pair(build) as pair:
        authority = Upstream(pair, probe, "authority", pair.authority_port, MIRROR)
        relay = Upstream(pair, probe, "relay", pair.relay_port, READER)
        transitions(authority)
        # The relay starts at rest: the authority's seal has held for 2.5 s (seal_after is 1 s) and
        # the relay serves it.
        deadline = time.monotonic() + 30
        seal, held = authority.seal(), time.monotonic()
        while time.monotonic() - held < 2.5 or relay.seal() != seal:
            check(time.monotonic() < deadline, "relay: the authority's seal never settled at the relay")
            if (now := authority.seal()) != seal:
                seal, held = now, time.monotonic()
            time.sleep(0.05)
        transitions(relay)
    return 0


if __name__ == "__main__":
    sys.exit(main())
