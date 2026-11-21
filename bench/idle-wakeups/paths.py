"""Throughput and latency of the authority and relay request paths (loop-idle-waits A3).

On one detent-authority and one relay (pair.py), --clients processes each hold one keep-alive connection
and, for --seconds per path, send requests back to back:
  authority  POST /v2/read of one key at detent-authority (the owner's read path);
  relay      POST /v2/distribution/session/next, version 2, every name from no cursor, on one
             session per client (the relay's mirror answer path); a 409 session_busy is retried
             and counted.
It prints one JSON line per path: requests per second and p50/p99 latency in microseconds.

    paths.py --build BUILD --label NAME [--clients 4] [--seconds 10]
"""

from __future__ import annotations

import argparse
import json
import multiprocessing
import ssl
import time
from pathlib import Path
from typing import Any

from pair import CONTEXT, NAMES, READER, WRITER, Pair


def client(
    path: str, port: int, ca: str, seconds: float, start: float
) -> tuple[list[int], int]:
    import http.client

    context = ssl.create_default_context(cafile=ca)
    connection = http.client.HTTPSConnection(
        "localhost", port, context=context, timeout=10
    )
    names = [[name] for name in NAMES]

    def post(route: str, token: str, body: dict[str, Any]) -> tuple[int, bytes]:
        connection.request(
            "POST",
            route,
            json.dumps(body),
            {"Authorization": f"Bearer {token}", "Content-Type": "application/json"},
        )
        response = connection.getresponse()
        return response.status, response.read()

    if path == "authority":
        route, token = "/v2/read", WRITER
        body: dict[str, Any] = CONTEXT | {"namespace": "a", "keys": ["a:k0"]}
    else:
        status, opened = post(
            "/v2/distribution/session/open",
            READER,
            {"version": 2, "profile": 1, "coding": "raw", "namespaces": names},
        )
        if status != 200:
            raise RuntimeError(f"session open answered {status}: {opened[:200]!r}")
        route, token = "/v2/distribution/session/next", READER
        body = {
            "version": 2,
            "session": json.loads(opened)["session"],
            "namespaces": names,
            "wait_ms": 0,
        }
    while time.monotonic() < start:
        time.sleep(0.001)
    latencies: list[int] = []
    busy = 0
    deadline = start + seconds
    while time.monotonic() < deadline:
        began = time.perf_counter_ns()
        status, answer = post(route, token, body)
        if status == 409:
            busy += 1
            continue
        if status != 200:
            raise RuntimeError(f"{route} answered {status}: {answer[:200]!r}")
        latencies.append((time.perf_counter_ns() - began) // 1000)
    connection.close()
    return latencies, busy


def measure(pair: Pair, path: str, clients: int, seconds: float) -> dict[str, Any]:
    port = pair.authority_port if path == "authority" else pair.relay_port
    start = time.monotonic() + 1.0
    with multiprocessing.get_context("spawn").Pool(clients) as pool:
        results = pool.starmap(
            client, [(path, port, pair.ca, seconds, start)] * clients
        )
    latencies = sorted(value for values, _ in results for value in values)
    count = len(latencies)
    return {
        "path": path,
        "requests_per_s": round(count / seconds, 1),
        "p50_us": latencies[count // 2] if count else None,
        "p99_us": latencies[min(count - 1, count * 99 // 100)] if count else None,
        "busy": sum(busy for _, busy in results),
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--label", required=True)
    parser.add_argument("--clients", type=int, default=4)
    parser.add_argument("--seconds", type=float, default=10)
    args = parser.parse_args()
    with Pair(args.build.resolve()) as pair:
        for path in ("authority", "relay"):
            row = {"label": args.label} | measure(
                pair, path, args.clients, args.seconds
            )
            print(json.dumps(row, sort_keys=True), flush=True)


if __name__ == "__main__":
    main()
