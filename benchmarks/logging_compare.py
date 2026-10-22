"""Compare daemon CPU, resident memory and log volume for conditional polling."""

from __future__ import annotations

import argparse
import hashlib
import http.client
import json
import os
import platform
import socket
import statistics
import subprocess
import sys
import time
from dataclasses import asdict, dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tests/detent_system"))
from fleet import parse_cpu_time


@dataclass(frozen=True)
class Sample:
    arm: str
    case: str
    pair: int
    requests: int
    cpu_seconds: float
    request_wall_seconds: float
    resident_bytes: int
    log_bytes: int
    request_logs: int
    response_logs: int
    http_exchanges: int


def check_interference(destination: Path) -> None:
    """Reject build/test processes and other work using at least half a CPU."""
    snapshot = subprocess.check_output(["ps", "-axo", "pid,ppid,%cpu,comm"], text=True)
    destination.write_text(snapshot)
    blockers = []
    for line in snapshot.splitlines()[1:]:
        fields = line.split(maxsplit=3)
        if len(fields) != 4:
            continue
        name = Path(fields[3]).name
        if (
            (int(fields[0]) != os.getpid() and float(fields[2]) >= 50.0)
            or name.endswith(("_test", "-test", "_bench", "-bench"))
            or name
            in {
                "ninja",
                "make",
                "gmake",
                "cmake",
                "ctest",
                "cc",
                "c++",
            }
            or name.startswith(("clang", "gcc", "g++", "cc1", "test_", "fuzz_"))
        ):
            blockers.append(line)
    if blockers:
        raise RuntimeError(
            "Competing build/test or CPU-heavy processes; timings rejected:\n"
            + "\n".join(blockers)
        )


def counters(pid: int) -> tuple[float, int]:
    clock, resident = subprocess.check_output(
        ["ps", "-o", "time=,rss=", "-p", str(pid)], text=True
    ).split()
    return parse_cpu_time(clock), int(resident) * 1024


def exchange(
    connection: http.client.HTTPConnection,
    method: str,
    path: str,
    body: bytes | None = None,
    etag: str | None = None,
) -> tuple[int, str | None]:
    connection.request(
        method, path, body=body, headers={"If-None-Match": etag} if etag else {}
    )
    reply = connection.getresponse()
    reply.read()
    return reply.status, reply.getheader("ETag")


def measure(
    binary: Path,
    folder: Path,
    arm: str,
    case: str,
    pair: int,
    requests: int,
    snapshot: bytes,
) -> Sample:
    folder.mkdir()
    configuration = folder / "config.json"
    configuration.write_text("{}\n")
    with socket.socket() as reservation:
        reservation.bind(("127.0.0.1", 0))
        port = reservation.getsockname()[1]
    flags = ["--color", "always" if case == "verbose-rgb" else "never"]
    if case != "filtered":
        flags.append("-v")
    logfile = folder / "daemon.log"
    environment = {**os.environ, "NO_COLOR": "1", "COLORTERM": "truecolor"}
    for name in (
        "DETENTD_PORT",
        "DETENTD_PERSIST_PATH",
        "DETENTD_REACTORS",
        "DETENTD_WORKERS",
    ):
        environment.pop(name, None)
    with logfile.open("wb") as output:
        process = subprocess.Popen(
            [
                str(binary),
                "--config",
                str(configuration),
                "--port",
                str(port),
                "--persist-path",
                str(folder / "state.json"),
                "--runtime-dir",
                str(folder / "run"),
                *flags,
            ],
            stdout=output,
            stderr=output,
            env=environment,
        )
        connection = http.client.HTTPConnection("127.0.0.1", port, timeout=10)
        try:
            for _ in range(100):
                if process.poll() is not None:
                    raise RuntimeError(f"daemon exited: {process.returncode}")
                try:
                    if exchange(connection, "GET", "/v1/health")[0] != 200:
                        raise RuntimeError("health refused")
                    break
                except OSError:
                    connection.close()
                    time.sleep(0.05)
            else:
                raise RuntimeError("daemon did not become ready")
            if exchange(connection, "PUT", "/v1/snapshot", snapshot)[0] != 200:
                raise RuntimeError("snapshot refused")
            status, etag = exchange(connection, "GET", "/v1/snapshot")
            if status != 200 or etag is None:
                raise RuntimeError("snapshot unavailable")
            for _ in range(100):
                if exchange(connection, "GET", "/v1/snapshot", etag=etag)[0] != 304:
                    raise RuntimeError("warm conditional response changed")
            time.sleep(0.25)
            before_cpu, _ = counters(process.pid)
            start = time.perf_counter()
            for _ in range(requests):
                if exchange(connection, "GET", "/v1/snapshot", etag=etag)[0] != 304:
                    raise RuntimeError("measured conditional response changed")
            wall = time.perf_counter() - start
            time.sleep(0.25)
            after_cpu, resident = counters(process.pid)
        finally:
            connection.close()
            if process.poll() is None:
                process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
                raise RuntimeError("daemon shutdown timed out") from None
        if process.returncode != 0:
            raise RuntimeError(f"daemon shutdown exit: {process.returncode}")
    logged = logfile.read_bytes()
    request_logs = logged.count("🌎".encode())
    response_logs = logged.count("💊".encode()) + logged.count("💫".encode())
    if (
        arm == "after"
        and case != "filtered"
        and (request_logs != requests + 103 or response_logs != requests + 103)
    ):
        raise RuntimeError("verbose candidate dropped HTTP log entries")
    return Sample(
        arm,
        case,
        pair,
        requests,
        after_cpu - before_cpu,
        wall,
        resident,
        len(logged),
        request_logs,
        response_logs,
        requests + 103,
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--before", type=Path, required=True)
    parser.add_argument("--after", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--requests", type=int, default=20000)
    parser.add_argument("--knobs", type=int, default=100000)
    parser.add_argument("--pairs", type=int, default=3)
    args = parser.parse_args()
    before: Path = args.before.resolve()
    after: Path = args.after.resolve()
    output: Path = args.output.resolve()
    requests: int = args.requests
    knobs: int = args.knobs
    pairs: int = args.pairs
    if requests < 1 or knobs < 1 or pairs < 1:
        raise ValueError("requests, knobs and pairs must be positive")
    output.mkdir(parents=True, exist_ok=False)
    check_interference(output / "processes-before.txt")
    snapshot = json.dumps(
        {
            "schema_version": 1,
            "version": 1,
            "knobs": {
                f"bench:key_{index}": {"type": "bool", "default": False}
                for index in range(knobs)
            },
        },
        separators=(",", ":"),
    ).encode()
    samples: list[Sample] = []
    for case in ("filtered", "verbose-plain", "verbose-rgb"):
        for pair in range(pairs):
            arms = [("before", before), ("after", after)]
            if pair % 2:
                arms.reverse()
            for arm, binary in arms:
                check_interference(output / f"processes-{case}-{pair}-{arm}-before.txt")
                sample = measure(
                    binary,
                    output / f"{case}-{pair}-{arm}",
                    arm,
                    case,
                    pair,
                    requests,
                    snapshot,
                )
                check_interference(output / f"processes-{case}-{pair}-{arm}-after.txt")
                samples.append(sample)
                with (output / "samples.jsonl").open("a") as stream:
                    stream.write(json.dumps(asdict(sample)) + "\n")
                print(json.dumps(asdict(sample)), flush=True)
    summary = {
        case: {
            arm: {
                "median_cpu_seconds": statistics.median(
                    row.cpu_seconds
                    for row in samples
                    if row.case == case and row.arm == arm
                ),
                "median_resident_bytes": statistics.median(
                    row.resident_bytes
                    for row in samples
                    if row.case == case and row.arm == arm
                ),
            }
            for arm in ("before", "after")
        }
        for case in ("filtered", "verbose-plain", "verbose-rgb")
    }
    report = {
        "host": {"platform": platform.platform(), "cpus": os.cpu_count()},
        "requests_per_run": requests,
        "knobs": knobs,
        "pairs_per_case": pairs,
        "before_sha256": hashlib.sha256(before.read_bytes()).hexdigest(),
        "after_sha256": hashlib.sha256(after.read_bytes()).hexdigest(),
        "summary": summary,
        "samples": [asdict(row) for row in samples],
        "limits": [
            "Synthetic 100k-style boolean state and one persistent polling connection, not fleet capacity.",
            "Uses daemon reactor/worker defaults. CPU is live ps time with 10ms resolution.",
            "CPU interval includes a 250ms logger drain; later shutdown CPU is excluded.",
            "RSS is one post-workload resident sample, not peak memory or allocation accounting.",
            "Log volume/counts include startup, seed, warmup and drained shutdown, unlike timed CPU.",
            "Exchange counts include 100 warmups and three setup requests; the baseline has no HTTP logs and filtering/backpressure can omit events.",
            "Baseline already enables severity tint; candidate adds depth options and HTTP metadata logging.",
        ],
    }
    check_interference(output / "processes-after.txt")
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
