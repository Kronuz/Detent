"""Bounded multi-producer HTTPS pressure for the real authority fixture."""

from __future__ import annotations

import concurrent.futures
import json
import time
from collections import Counter
from collections.abc import Callable
from typing import Any

PRODUCERS_PER_PHASE = 256
PRODUCERS = 3 * PRODUCERS_PER_PHASE
Request = Callable[[str, dict[str, Any], str], tuple[int, dict[str, Any]]]


def credentials() -> list[dict[str, Any]]:
    return [
        {
            "token": f"{index + 65536:032x}",
            "producer": f"load{index}",
            "namespaces": [f"load{index}"],
        }
        for index in range(PRODUCERS)
    ]


def run_pressure(request: Request) -> dict[int, dict[str, Any]]:
    retained: dict[int, dict[str, Any]] = {}
    writer_credentials = credentials()
    for phase, concurrency in enumerate((16, 64, 256)):
        counts: Counter[str] = Counter()
        durations: list[float] = []
        began = time.monotonic()

        def execute(index: int) -> tuple[int, int, dict[str, Any], float]:
            token = writer_credentials[index]["token"]
            started = time.monotonic()
            try:
                code, reply = request(
                    "/v2/open",
                    {"expected_generation": 0, "nonce": f"{index + 1:032x}"},
                    token,
                )
                if code != 200:
                    return index, code, reply, time.monotonic() - started
                assert reply["generation"] == 1, reply
                code, reply = request(
                    "/v2/transactions",
                    {
                        "generation": 1,
                        "sequence": 1,
                        "namespace": f"load{index}",
                        "operations": [
                            {
                                "key": f"load{index}:key",
                                "value": {"type": "int", "default": index},
                                "expected_revision": 0,
                                "absent_namespace_revision": 0,
                            }
                        ],
                    },
                    token,
                )
                return index, code, reply, time.monotonic() - started
            except OSError as error:
                return (
                    index,
                    0,
                    {"transport_error": type(error).__name__},
                    time.monotonic() - started,
                )

        with concurrent.futures.ThreadPoolExecutor(max_workers=concurrency) as executor:
            for index, code, reply, elapsed in executor.map(
                execute,
                range(phase * PRODUCERS_PER_PHASE, (phase + 1) * PRODUCERS_PER_PHASE),
            ):
                durations.append(elapsed)
                if code == 200:
                    assert reply["generation"] == 1 and reply["sequence"] == 1, reply
                    if index in retained:
                        assert reply == retained[index], (
                            "retry changed an acknowledged receipt"
                        )
                    retained[index] = reply
                    counts["acknowledged"] += 1
                elif code in (429, 503):
                    counts["retryable"] += 1
                elif code == 0:
                    counts["transport_unknown"] += 1
                else:
                    raise AssertionError((index, code, reply))
        assert sum(counts.values()) == PRODUCERS_PER_PHASE
        durations.sort()
        print(
            json.dumps(
                {
                    "phase": "multi_producer_pressure",
                    "producers": PRODUCERS_PER_PHASE,
                    "concurrency": concurrency,
                    "elapsed_seconds": time.monotonic() - began,
                    "producer_outcomes": dict(counts),
                    "client_open_plus_write_p50_seconds": durations[
                        len(durations) // 2
                    ],
                    "client_open_plus_write_max_seconds": durations[-1],
                },
                sort_keys=True,
            ),
            flush=True,
        )
    return retained


def verify_receipts(request: Request, retained: dict[int, dict[str, Any]]) -> None:
    writer_credentials = credentials()
    for index, expected in retained.items():
        code, actual = request(
            "/v2/result",
            {"generation": 1, "sequence": 1},
            writer_credentials[index]["token"],
        )
        assert code == 200 and actual == expected, (index, code, actual, expected)


def assert_drained(sample: Callable[[], dict[str, Any]]) -> dict[str, Any]:
    """Require a cached snapshot with no retained publisher or retired-log credit."""
    keys = (
        "requests",
        "reserved_body_bytes",
        "reserved_reply_bytes",
        "connections_active",
        "retired_log_bytes",
        "retired_log_entries",
    )
    deadline = time.monotonic() + 5
    while True:
        health = sample()
        if all(health[key] == 0 for key in keys):
            return health
        assert time.monotonic() < deadline, {key: health[key] for key in keys}
        time.sleep(0.3)
