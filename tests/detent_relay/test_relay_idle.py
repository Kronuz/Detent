"""An idle relay costs neither itself nor its authority a loop turn per poll (relay-mirror-idle-wake).

A single-voter authority and a relay mirroring it at the default 250 ms interval (bench/idle-wakeups'
Pair). At rest the relay holds one W5 next parked at the authority: over 2 s the relay makes no sync
and the authority's owner keeps to its 250 ms health turns (4 a second), where a parked next that
counts as work turned it every millisecond (about 780 a second). A publish still wakes both: the
relay syncs its entries within 2 s, against its 30 s deadline, and the write's seal, which moves no
head, answers the relay's next parked again at once: it fetches the sealed file within 5 s.

    test_relay_idle.py BUILD
"""

from __future__ import annotations

import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "bench" / "idle-wakeups"))
from pair import Pair  # noqa: E402

TURNS_PER_S = 60  # the health turns and their rescans, with room for a loaded machine


def check(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    with Pair(Path(sys.argv[1]).resolve()) as pair:
        time.sleep(1)  # the relay's first wait parks

        def sample() -> tuple[int, int, int, float]:
            authority = pair.health(pair.authority_port) or {}
            relay = pair.health(pair.relay_port) or {}
            mirror = relay["mirror"]
            return (
                authority["owner_turn_ns"]["count"],
                mirror["syncs"],
                mirror["fetched_files"],
                time.monotonic(),
            )

        turns, syncs, _, at = sample()
        time.sleep(2)
        turns_after, syncs_after, fetched, at_after = sample()
        rate = (turns_after - turns) / (at_after - at)
        print(
            f"idle: authority {rate:.1f} owner turns/s, relay {syncs_after - syncs} syncs in 2 s",
            flush=True,
        )
        check(rate <= TURNS_PER_S, f"the authority turned {rate:.1f} times a second")
        check(syncs_after - syncs <= 1, f"the relay synced {syncs_after - syncs} times")
        pair.sequence += 1
        written = time.monotonic()
        pair.write(
            "/v2/transactions",
            {
                "generation": 1,
                "sequence": pair.sequence,
                "namespace": "a",
                "operations": [{"key": "a:k0", "value": {"default": -1}}],
            },
        )
        while sample()[1] == syncs_after:
            check(time.monotonic() - written < 2, "the publish did not wake the relay")
            time.sleep(0.01)
        print(
            f"wake: the relay synced after {(time.monotonic() - written) * 1000:.0f} ms"
        )
        while sample()[2] == fetched:
            check(
                time.monotonic() - written < 5,
                "the write's seal did not reach the relay",
            )
            time.sleep(0.01)
        print(
            f"seal: the relay fetched the sealed file after {(time.monotonic() - written) * 1000:.0f} ms"
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())
