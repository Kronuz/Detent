"""The seal stamp, one check per transition (docs/mechanisms/relay-mirror-wait.md), at a single-voter
authority and at a relay mirroring it (bench/idle-wakeups' Pair).

A W5 session holds the upstream's seal at its last answer, or at its open. A next on a session whose
stamp is older than the seal answers 204 at once, parked or new; any other parks until a change or
its wait. A write to "b" makes the seal that moves no head of "a": the authority seals it after
seal_after (1 s), and the relay adopts that seal at its next sync. Every next here follows "a", so
only the seal can answer it early. The steps order the seal against each next by observing the
upstream's layouts seal, never by a race:
- open: a next right after the open parks for its whole wait (the open stamped the session);
- busy: of two nexts on one session, one parks and one answers 409 session_busy;
- older: once the seal moved after the session's last answer, a new next answers 204 at once;
- answered: that 204 stamped the session, so the next one parks for its whole wait;
- parked: a next parked before the write answers 204 when the seal lands, before its wait;
- ended: a closed session answers 410, and a session opened again is stamped at its open.

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
AT_ONCE_S = 0.8  # an early 204 at the stamp, against waits of 1.5 s and more


def check(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


class Upstream:
    """One upstream's W5 routes and layouts seal, with the token it grants "a" and "b" to."""

    def __init__(
        self, pair: Pair, probe: Path, label: str, port: int, token: str
    ) -> None:
        self.pair, self.probe, self.label, self.port, self.token = (
            pair,
            probe,
            label,
            port,
            token,
        )

    def seal(self) -> int:
        status, body = self.pair.call(
            self.port, "/v2/history/layouts", {"namespaces": NAMES}, self.token
        )
        check(status == 200, f"{self.label}: layouts answered {status}")
        return int.from_bytes(
            body[24:32], "little"
        )  # DTMIRR01, layout, entries, sequence, seal

    def heads(self) -> list[list[object]]:
        output = subprocess.check_output(
            [
                str(self.probe),
                self.pair.ca,
                str(self.port),
                self.token,
                CONTEXT["cluster"],
                "--heads",
                "a",
            ],
            text=True,
            timeout=30,
        )
        incarnation, position = json.loads(output)["a"]
        return [["a", incarnation, position]]

    def open(self, names: list[list[object]]) -> str:
        body = {
            "version": 2,
            "profile": 1,
            "coding": "raw",
            "cluster": CONTEXT["cluster"],
            "namespaces": names,
        }
        status, reply = self.pair.call(
            self.port, "/v2/distribution/session/open", body, self.token
        )
        check(status == 200, f"{self.label}: open answered {status}: {reply!r}")
        session: str = json.loads(reply)["session"]
        return session

    def next(
        self, session: str, names: list[list[object]], wait_ms: int
    ) -> tuple[int, float]:
        body = {
            "version": 2,
            "session": session,
            "cluster": CONTEXT["cluster"],
            "namespaces": names,
            "wait_ms": wait_ms,
        }
        sent = time.monotonic()
        status, _ = self.pair.call(
            self.port, "/v2/distribution/session/next", body, self.token
        )
        return status, time.monotonic() - sent

    def close(self, session: str) -> int:
        status, _ = self.pair.call(
            self.port,
            "/v2/distribution/session/close",
            {"version": 2, "session": session},
            self.token,
        )
        return status

    def sealed_after(self, before: int) -> int:
        deadline = time.monotonic() + 30
        while (now := self.seal()) == before:
            check(
                time.monotonic() < deadline,
                f"{self.label}: the write to b was never sealed",
            )
            time.sleep(0.01)
        return now


def write_b(pair: Pair) -> None:
    pair.sequence += 1
    receipt = pair.write(
        "/v2/transactions",
        {
            "generation": 1,
            "sequence": pair.sequence,
            "namespace": "b",
            "operations": [{"key": "b:k0", "value": {"default": pair.sequence}}],
        },
    )
    check(receipt.get("status") == "applied", f"transaction refused: {receipt}")


def parks(upstream: Upstream, step: str, answer: tuple[int, float]) -> None:
    status, took = answer
    print(f"{upstream.label} {step}: {status} after {took * 1000:.0f} ms", flush=True)
    check(
        status == 204 and took >= PARKED_S,
        f"{upstream.label} {step}: {status} after {took:.3f} s, not parked",
    )


def at_once(upstream: Upstream, step: str, answer: tuple[int, float]) -> None:
    status, took = answer
    print(f"{upstream.label} {step}: {status} after {took * 1000:.0f} ms", flush=True)
    check(
        status == 204 and took < AT_ONCE_S,
        f"{upstream.label} {step}: {status} after {took:.3f} s, not at once",
    )


def transitions(upstream: Upstream) -> None:
    names = upstream.heads()
    session = upstream.open(names)
    parks(upstream, "open", upstream.next(session, names, WAIT_MS))

    answers: list[tuple[int, float]] = []
    first = threading.Thread(
        target=lambda: answers.append(upstream.next(session, names, WAIT_MS))
    )
    first.start()
    time.sleep(0.2)
    answers.append(upstream.next(session, names, WAIT_MS))
    first.join()
    statuses = sorted(status for status, _ in answers)
    print(f"{upstream.label} busy: {statuses}", flush=True)
    check(statuses == [204, 409], f"{upstream.label} busy: {answers}")

    write_b(upstream.pair)
    upstream.sealed_after(upstream.seal())
    at_once(upstream, "older", upstream.next(session, names, 5000))
    parks(upstream, "answered", upstream.next(session, names, WAIT_MS))

    parked: list[tuple[int, float]] = []
    waiter = threading.Thread(
        target=lambda: parked.append(upstream.next(session, names, 8000))
    )
    waiter.start()
    time.sleep(0.2)
    before = upstream.seal()
    write_b(upstream.pair)
    waiter.join()
    status, took = parked[0]
    print(f"{upstream.label} parked: {status} after {took * 1000:.0f} ms", flush=True)
    check(
        upstream.seal() != before, f"{upstream.label} parked: answered before the seal"
    )
    check(
        status == 204 and took < 5,
        f"{upstream.label} parked: {status} after {took:.3f} s",
    )

    check(upstream.close(session) in (200, 204), f"{upstream.label}: close refused")
    status, _ = upstream.next(session, names, WAIT_MS)
    print(f"{upstream.label} ended: {status}", flush=True)
    check(status == 410, f"{upstream.label} ended: a closed session answered {status}")
    parks(upstream, "reopened", upstream.next(upstream.open(names), names, WAIT_MS))


def main() -> int:
    build = Path(sys.argv[1]).resolve()
    probe = build / "tests/detent_relay/test_relay_probe"
    with Pair(build) as pair:
        transitions(Upstream(pair, probe, "authority", pair.authority_port, MIRROR))
        transitions(Upstream(pair, probe, "relay", pair.relay_port, READER))
    return 0


if __name__ == "__main__":
    sys.exit(main())
