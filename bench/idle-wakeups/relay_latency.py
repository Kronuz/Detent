"""Change-to-relay latency: from an authority publish to the relay answering it (relay-mirror-idle-wake A2).

A single-voter authority and a relay mirroring it (pair.Pair, the relay at --interval-ms). Each
sample parks a W5 next at the relay at namespace "a"'s head (again after a 204, as a W5 reader
does), waits a random 0 .. --spread-ms so the publish lands at any phase of the relay's own waits,
writes one transaction, and times its acknowledgement to the next's 200. The heads come from the
probe (relay_probe --heads), which runs outside the timed span. Prints one JSON line: p50, p90,
p99 and max in ms.

    relay_latency.py --build BUILD --probe RELAY_PROBE --label NAME [--samples 40] [--interval-ms 250]
"""

from __future__ import annotations

import argparse
import json
import random
import statistics
import subprocess
import threading
import time
from pathlib import Path

from pair import CONTEXT, READER, Pair


def heads(pair: Pair, probe: Path) -> dict[str, list[object]]:
    output = subprocess.check_output(
        [
            str(probe),
            pair.ca,
            str(pair.relay_port),
            READER,
            CONTEXT["cluster"],
            "--heads",
            "a",
        ],
        text=True,
        timeout=30,
    )
    result: dict[str, list[object]] = json.loads(output)
    return result


def sample(pair: Pair, probe: Path, spread_ms: int) -> float:
    incarnation, position = heads(pair, probe)["a"]
    names = [["a", incarnation, position]]
    status, body = pair.call(
        pair.relay_port,
        "/v2/distribution/session/open",
        {
            "version": 2,
            "profile": 1,
            "coding": "raw",
            "cluster": CONTEXT["cluster"],
            "namespaces": names,
        },
        READER,
    )
    if status != 200:
        raise RuntimeError(f"open answered {status}: {body!r}")
    session = json.loads(body)["session"]
    answered: list[tuple[int, float]] = []

    def park() -> None:
        # A 204 (a seal, or the deadline) parks again at the same heads, as a W5 reader does.
        for _ in range(8):
            status, _ = pair.call(
                pair.relay_port,
                "/v2/distribution/session/next",
                {
                    "version": 2,
                    "session": session,
                    "cluster": CONTEXT["cluster"],
                    "namespaces": names,
                    "wait_ms": 9000,
                },
                READER,
            )
            if status != 204:
                break
        answered.append((status, time.monotonic()))

    waiter = threading.Thread(target=park)
    waiter.start()
    time.sleep(0.05 + random.uniform(0, spread_ms / 1000))  # the next is parked by then
    pair.sequence += 1
    receipt = pair.write(
        "/v2/transactions",
        {
            "generation": 1,
            "sequence": pair.sequence,
            "namespace": "a",
            "operations": [{"key": "a:k0", "value": {"default": pair.sequence}}],
        },
    )
    acknowledged = time.monotonic()
    if receipt.get("status") != "applied":
        raise RuntimeError(f"transaction refused: {receipt}")
    waiter.join()
    status, at = answered[0]
    if status != 200:
        raise RuntimeError(f"the parked next answered {status}, not the change")
    return (at - acknowledged) * 1000


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--label", required=True)
    parser.add_argument("--samples", type=int, default=40)
    parser.add_argument("--interval-ms", type=int, default=250)
    parser.add_argument("--spread-ms", type=int, default=500)
    args = parser.parse_args()
    random.seed(1)  # the same phases for every build
    with Pair(args.build.resolve(), interval_ms=args.interval_ms) as pair:
        latencies = sorted(
            sample(pair, args.probe.resolve(), args.spread_ms)
            for _ in range(args.samples)
        )
    cuts = statistics.quantiles(latencies, n=100, method="inclusive")
    print(
        json.dumps(
            {
                "label": args.label,
                "interval_ms": args.interval_ms,
                "samples": len(latencies),
                "p50_ms": round(cuts[49], 2),
                "p90_ms": round(cuts[89], 2),
                "p99_ms": round(cuts[98], 2),
                "max_ms": round(latencies[-1], 2),
            },
            sort_keys=True,
        ),
        flush=True,
    )


if __name__ == "__main__":
    main()
