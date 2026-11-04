#!/usr/bin/env python3
"""Daemon publish and get latency over HTTP, one printed row per measure (a unit-format run for ab_compare.py).

  daemon_bench.py --detentd BINARY [--port N] [--knobs 2000] [--puts 300] [--gets 3000] [--clients 4]

Starts detentd on a fresh state directory and, over keep-alive connections, publishes `--puts` snapshots of
`--knobs` knobs (PUT, each with the next version), then reads the snapshot back (identity, zstd, and the 304 of
a conditional read) and `/v1/health`. The rows are the p50 and p99 of each request kind in microseconds, and the
total of `--clients` concurrent readers as the time per request. The client is Python's http.client, the same
for both builds, so the A/A control carries its noise.
"""

from __future__ import annotations

import argparse
import http.client
import json
import shutil
import socket
import statistics
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path


def snapshot(version: int, count: int) -> bytes:
    knobs: dict[str, object] = {}
    for i in range(count):
        if i % 3 == 0:
            knobs[f"ns{i % 5}:flag_{i}"] = {
                "type": "bool",
                "default": i % 2 == 0,
                "rollout_pct": (i * 7) % 101,
                "overrides": {"internal": True},
            }
        elif i % 3 == 1:
            knobs[f"ns{i % 5}:int_{i}"] = {"type": "int", "default": i * 1000 + version}
        else:
            knobs[f"ns{i % 5}:plain_{i}"] = {"type": "bool", "default": False}
    return json.dumps(
        {"schema_version": 1, "version": version, "knobs": knobs}, separators=(",", ":")
    ).encode()


def timed(
    connection: http.client.HTTPConnection,
    method: str,
    path: str,
    body: bytes | None = None,
    headers: dict[str, str] | None = None,
) -> tuple[float, int, int]:
    start = time.perf_counter()
    connection.request(method, path, body=body, headers=headers or {})
    response = connection.getresponse()
    data = response.read()
    return (time.perf_counter() - start) * 1e6, response.status, len(data)


def percentile(values: list[float], p: float) -> float:
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, int(len(ordered) * p))]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--detentd", type=Path, required=True)
    parser.add_argument("--port", type=int, default=18511)
    parser.add_argument("--knobs", type=int, default=2000)
    parser.add_argument("--puts", type=int, default=300)
    parser.add_argument("--gets", type=int, default=3000)
    parser.add_argument("--clients", type=int, default=4)
    parser.add_argument("--scratch", type=Path, default=None)
    args = parser.parse_args()
    state = Path(tempfile.mkdtemp(prefix="w5-daemon-", dir=args.scratch))
    log = (state / "daemon.log").open("wb")
    daemon = subprocess.Popen(
        [
            str(args.detentd),
            "--port",
            str(args.port),
            "--persist-path",
            str(state / "snapshot.json"),
            "--reactors",
            "4",
            "--color",
            "never",
        ],
        stdout=log,
        stderr=subprocess.STDOUT,
        stdin=subprocess.DEVNULL,
        cwd=state,
    )
    try:
        for _ in range(100):
            try:
                socket.create_connection(("127.0.0.1", args.port), timeout=0.2).close()
                break
            except OSError:
                time.sleep(0.1)
        else:
            print("detentd did not start", file=sys.stderr)
            return 2
        connection = http.client.HTTPConnection("127.0.0.1", args.port, timeout=30)
        bodies = [snapshot(v, args.knobs) for v in range(1, args.puts + 2)]
        puts: list[float] = []
        for body in bodies[: args.puts]:
            micros, status, _ = timed(
                connection, "PUT", "/v1/snapshot", body, {"Content-Type": "application/json"}
            )
            if status != 200:
                print(f"PUT answered {status}", file=sys.stderr)
                return 2
            puts.append(micros)
        rows = {"put": puts[10:]}
        for name, headers in (
            ("get-identity", {}),
            ("get-zstd", {"Accept-Encoding": "zstd"}),
            ("get-304", {"If-None-Match": f'"{args.puts}"'}),
            ("health", None),
        ):
            path = "/v1/health" if headers is None else "/v1/snapshot"
            values: list[float] = []
            sizes = set()
            for _ in range(args.gets):
                micros, status, size = timed(connection, "GET", path, None, headers or {})
                if status not in (200, 304):
                    print(f"GET {name} answered {status}", file=sys.stderr)
                    return 2
                values.append(micros)
                sizes.add((status, size))
            rows[name] = values[50:]
        # Concurrent readers: wall time per request over all clients.
        done = threading.Barrier(args.clients + 1)
        counts = [0] * args.clients

        def reader(slot: int) -> None:
            c = http.client.HTTPConnection("127.0.0.1", args.port, timeout=30)
            done.wait()
            for _ in range(args.gets):
                _, status, _ = timed(c, "GET", "/v1/snapshot", None, {"Accept-Encoding": "zstd"})
                counts[slot] += status == 200
            done.wait()

        threads = [threading.Thread(target=reader, args=(i,)) for i in range(args.clients)]
        for t in threads:
            t.start()
        done.wait()
        begin = time.perf_counter()
        done.wait()
        elapsed = time.perf_counter() - begin
        for t in threads:
            t.join()
        if sum(counts) != args.clients * args.gets:
            print("a concurrent read failed", file=sys.stderr)
            return 2
        for name, values in rows.items():
            print(f"{name}-p50 {statistics.median(values):.1f} us")
            print(f"{name}-p99 {percentile(values, 0.99):.1f} us")
        print(
            f"concurrent-{args.clients}-clients-per-request {elapsed * 1e6 / (args.clients * args.gets):.1f} us"
        )
        return 0
    finally:
        daemon.terminate()
        try:
            daemon.wait(timeout=10)
        except subprocess.TimeoutExpired:
            daemon.kill()
        log.close()
        shutil.rmtree(state, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
