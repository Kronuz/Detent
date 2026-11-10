"""First-read gate (reader.md T2 through the pre-S6 Client): baseline and Dense, 1 and 1,000
namespaces, publisher running and stopped, plus an A/A repeat, in one interleaved run.

Each arm times 200 fresh processes (40 keys x 5 rounds), each after its own warmup process, from
before `detent::Client` construction to its first checked value (`first_live_ns`, steady_clock),
over a fresh APFS clone of the prepared store whose files are read into the page cache first.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import platform
import random
import signal
import socket
import statistics
import struct
import subprocess
import time
from collections.abc import Generator
from contextlib import contextmanager
from pathlib import Path
from typing import Any

ROUNDS = 5
# (code, namespaces, publisher); ABBA within each publisher state, then the A/A repeats.
ARMS = [
    ("baseline", 1, "stopped"),
    ("dense", 1, "stopped"),
    ("dense", 1000, "stopped"),
    ("baseline", 1000, "stopped"),
    ("baseline", 1, "running"),
    ("dense", 1, "running"),
    ("dense", 1000, "running"),
    ("baseline", 1000, "running"),
    ("dense", 1, "stopped"),
    ("baseline", 1, "stopped"),
]
RECORDS = {1: 5_000_000, 1000: 5_099_900}


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def cases(recipe: Path) -> list[tuple[str, int]]:
    """Forty integer keys of the 5M recipe, as dense_reader_measure.py selects them."""
    with recipe.open("rb") as stream:
        magic, count, _producers, _revision, length = struct.unpack(
            "<8s4Q", stream.read(40)
        )
        if magic != b"DTMIXED1" or count != 5_000_000 or length:
            raise ValueError("requires the preserved 5M mixed recipe")
        result: list[tuple[str, int]] = []
        for ordinal in random.Random(0x524541444552).sample(range(count), 200):
            stream.seek(40 + ordinal * 8)
            generation = struct.unpack("<q", stream.read(8))[0]
            if generation < 0 or (ordinal + generation) % 2 == 0:
                continue
            magnitude = (generation * 104729 + ordinal * 17) % (1 << 60)
            value = -magnitude if (ordinal + generation) & 2 else magnitude
            result.append((f"primary:key{ordinal}", value))
            if len(result) == 40:
                return result
    raise ValueError("fewer than forty integer cases")


def warm(directory: Path) -> None:
    for path in sorted(directory.iterdir()):
        if path.is_file():
            with path.open("rb") as stream:
                while stream.read(1 << 20):
                    pass


def probe(binary: Path, store: Path, key: str, value: int) -> dict[str, Any]:
    result = subprocess.run(
        [str(binary), "--read", str(store), key, str(value)],
        capture_output=True,
        text=True,
        timeout=60,
        check=False,
    )
    if result.returncode:
        raise RuntimeError(f"probe exit {result.returncode}: {result.stderr[-400:]}")
    rows = [json.loads(x) for x in result.stdout.splitlines() if x.startswith("{")]
    if len(rows) != 1:
        raise RuntimeError("probe printed no single result")
    return rows[0]


def namespaces(count: int) -> list[str]:
    return ["primary"] + [f"side{i}" for i in range(1, count)]


@contextmanager
def publisher(
    agent: Path, arm: Path, ca: Path, count: int, template: dict[str, Any]
) -> Generator[float, None, None]:
    """The real detent-agent over the arm's store, its upstream bound but never listening."""
    managed = dict(template)
    managed.pop("image_format", None)
    managed.update(
        namespaces=namespaces(count),
        ca=str(ca),
        address="127.0.0.1",
        hostname="localhost",
    )
    managed["image_limits"] = dict(managed["image_limits"], records=6_000_000)
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as unavailable:
        unavailable.bind(("127.0.0.1", 0))
        managed["port"] = unavailable.getsockname()[1]
        (arm / "managed.json").write_text(json.dumps(managed, indent=2) + "\n")
        (arm / "managed.json").chmod(0o600)
        (arm / "runtime").mkdir(mode=0o700, exist_ok=True)
        config = arm / "agent.json"
        config.write_text(
            json.dumps(
                {
                    "managed_path": "managed.json",
                    "generation_dir": "generations",
                    "runtime_dir": "runtime",
                }
            )
            + "\n"
        )
        log = arm / "publisher.log"
        started = time.monotonic()
        with log.open("wb") as stream:
            process = subprocess.Popen(
                [str(agent), "--config", str(config)],
                stdin=subprocess.DEVNULL,
                stdout=stream,
                stderr=subprocess.STDOUT,
            )
            try:
                while True:
                    if process.poll() is not None:
                        raise RuntimeError(
                            f"publisher exited before readiness; see {log}"
                        )
                    if (
                        b"managed local store ready, selected generation"
                        in log.read_bytes()
                    ):
                        break
                    if time.monotonic() - started > 300:
                        raise TimeoutError(f"publisher not ready in 300 s; see {log}")
                    time.sleep(0.05)
                yield time.monotonic() - started
                if process.poll() is not None:
                    raise RuntimeError(f"publisher exited during the arm; see {log}")
            finally:
                if process.poll() is None:
                    process.send_signal(signal.SIGTERM)
                    try:
                        process.wait(timeout=60)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()


def measure(
    binary: Path, store: Path, selected: list[tuple[str, int]], records: int
) -> list[dict[str, Any]]:
    samples: list[dict[str, Any]] = []
    for round_number in range(ROUNDS):
        for index, (key, value) in enumerate(selected):
            probe(binary, store, key, value)
            sample = probe(binary, store, key, value)
            if sample["records"] != records:
                raise RuntimeError("first-read cardinality differs")
            samples.append(sample | {"round": round_number, "case": index, "key": key})
    return samples


def run_json(args: list[str]) -> dict[str, Any]:
    result = subprocess.run(
        args, capture_output=True, text=True, timeout=120, check=False
    )
    if result.returncode:
        raise RuntimeError(
            f"{args[1]} exit {result.returncode}: {result.stderr[-400:]}"
        )
    return json.loads(result.stdout.splitlines()[-1])


# (code, namespaces, depth): the gate's stores at the job's path, then 1-namespace clones at a
# 4-component path, to price the directory trust walk per path component.
PROFILE_ARMS = [
    ("baseline", 1, "job"),
    ("dense", 1, "job"),
    ("dense", 1000, "job"),
    ("baseline", 1000, "job"),
    ("dense", 1, "shallow"),
    ("baseline", 1, "shallow"),
]


def profile(
    prepared: Path, out: Path, selected: list[tuple[str, int]], shallow: Path
) -> dict[str, Any]:
    """Per arm: warm per-operation means, then per key a warmup process, one --read sample and one
    --phases sample, each a fresh process, publisher stopped."""
    arms: list[dict[str, Any]] = []
    for order, (code, count, depth) in enumerate(PROFILE_ARMS):
        arm = out / f"{order:02d}-{code}-{count}ns-{depth}"
        arm.mkdir()
        store = arm / "generations" if depth == "job" else shallow / "generations"
        subprocess.run(["rm", "-rf", str(shallow / "generations")], check=True)
        subprocess.run(
            ["cp", "-cR", str(prepared / code / f"store-{count}ns"), str(store)],
            check=True,
        )
        warm(store)
        binary = prepared / code / "prepared_client_probe"
        phases = prepared / code / "first_read_phases"
        row: dict[str, Any] = {
            "order": order,
            "code": code,
            "namespaces": count,
            "depth": depth,
        }
        row["micro"] = run_json([str(phases), "--micro", str(store), "2000"])
        reads: list[dict[str, Any]] = []
        split: list[dict[str, Any]] = []
        for _ in range(ROUNDS):
            for key, value in selected:
                probe(binary, store, key, value)
                reads.append(probe(binary, store, key, value))
                name_space, name = key.split(":", 1)
                split.append(
                    run_json(
                        [
                            str(phases),
                            "--phases",
                            str(store),
                            name_space,
                            name,
                            str(value),
                        ]
                    )
                )
        with (arm / "samples.jsonl").open("w") as stream:
            for sample in reads:
                stream.write(json.dumps({"mode": "read"} | sample) + "\n")
            for sample in split:
                stream.write(json.dumps({"mode": "phases"} | sample) + "\n")
        row["read"] = summary(reads)
        row["phases"] = {
            k: statistics.median(s[k] for s in split)
            for k in split[0]
            if k.endswith(("_ns", "_us", "_faults")) or k in ("components", "mappings")
        }
        row["phases"]["below_1ms"] = sum(s["total_ns"] < 1_000_000 for s in split)
        arms.append(row)
        print(json.dumps(row), flush=True)
        subprocess.run(["rm", "-rf", str(store)], check=True)
    return {"arms": arms}


def summary(samples: list[dict[str, Any]]) -> dict[str, Any]:
    first = sorted(s["first_live_ns"] for s in samples)
    return {
        "samples": len(first),
        "below_1ms": sum(v < 1_000_000 for v in first),
        "median_ns": statistics.median(first),
        "p99_ns": first[min(len(first) - 1, int(0.99 * len(first)))],
        "maximum_ns": first[-1],
        "median_attach_ns": statistics.median(s["attach_ns"] for s in samples),
        "median_lookup_ns": statistics.median(s["lookup_ns"] for s in samples),
        "median_cpu_us": statistics.median(s["process_cpu_us"] for s in samples),
        "major_faults": sum(s["major_faults"] for s in samples),
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--prepared", type=Path, required=True, help="the preparation job's output"
    )
    parser.add_argument("--recipe", type=Path, required=True)
    parser.add_argument(
        "--managed", type=Path, required=True, help="managed.json template"
    )
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--publisher",
        choices=("stopped", "running"),
        help="only the arms in this publisher state (default: all)",
    )
    parser.add_argument(
        "--profile",
        type=Path,
        help="attribute the first read instead (a scratch directory for the shallow-path arms)",
    )
    args = parser.parse_args()
    out: Path = args.output
    out.mkdir(parents=True, exist_ok=False)
    selected = cases(args.recipe)
    if args.profile:
        args.profile.mkdir(mode=0o700, parents=True, exist_ok=True)
        try:
            report = profile(args.prepared, out, selected, args.profile)
        finally:
            subprocess.run(["rm", "-rf", str(args.profile)], check=True)
        report |= {
            "platform": platform.platform(),
            "toolchain": (args.prepared / "toolchain.txt").read_text(),
        }
        (out / "result.json").write_text(json.dumps(report, indent=2) + "\n")
        return
    template: dict[str, Any] = json.loads(args.managed.read_text())
    ca = args.prepared / "ca.pem"
    report: dict[str, Any] = {
        "platform": platform.platform(),
        "machine": platform.machine(),
        "toolchain": (args.prepared / "toolchain.txt").read_text(),
        "read_path": "pre-S6 detent::Client (prepared_client_probe --read), first_live_ns",
        "rounds": ROUNDS,
        "publisher": args.publisher or "all",
        "inputs": {},
        "arms": [],
        "status": "incomplete",
    }
    for code in ("baseline", "dense"):
        for name in ("prepared_client_probe", "detent-agent", "commit.txt"):
            path = args.prepared / code / name
            report["inputs"][f"{code}/{name}"] = digest(path)
        report["inputs"][f"{code}/commit"] = (
            (args.prepared / code / "commit.txt").read_text().strip()
        )
    try:
        for order, (code, count, state) in enumerate(ARMS):
            if args.publisher and state != args.publisher:
                continue
            arm = out / f"{order:02d}-{code}-{count}ns-{state}"
            arm.mkdir()
            store = arm / "generations"
            subprocess.run(
                [
                    "cp",
                    "-cR",
                    str(args.prepared / code / f"store-{count}ns"),
                    str(store),
                ],
                check=True,
            )
            row: dict[str, Any] = {
                "order": order,
                "code": code,
                "namespaces": count,
                "publisher": state,
            }
            binary = args.prepared / code / "prepared_client_probe"
            try:
                if state == "running":
                    with publisher(
                        args.prepared / code / "detent-agent", arm, ca, count, template
                    ) as ready:
                        row["publisher_ready_s"] = ready
                        warm(store)
                        samples = measure(binary, store, selected, RECORDS[count])
                else:
                    warm(store)
                    samples = measure(binary, store, selected, RECORDS[count])
                with (arm / "samples.jsonl").open("w") as stream:
                    for sample in samples:
                        stream.write(json.dumps(sample) + "\n")
                row |= summary(samples)
                row["gate_200_of_200"] = row["below_1ms"] == 200
            except (
                OSError,
                RuntimeError,
                ValueError,
                KeyError,
                subprocess.SubprocessError,
            ) as error:
                # An arm that cannot run is recorded, not fatal.
                row["error"] = str(error)
            report["arms"].append(row)
            print(json.dumps(row), flush=True)
            subprocess.run(["rm", "-rf", str(store), str(arm / "runtime")], check=True)
        report["status"] = (
            "completed"
            if all("error" not in a for a in report["arms"])
            else "arm-errors"
        )
    finally:
        (out / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    if report["status"] != "completed":
        raise SystemExit(2)


if __name__ == "__main__":
    main()
