"""Measure preserved baseline and dense stores through the public readers."""

from __future__ import annotations

import argparse
import hashlib
import json
import platform
import random
import statistics
import struct
import subprocess
import time
from pathlib import Path
from typing import Any


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def run(command: list[str], log: Path) -> list[dict[str, Any]]:
    started = time.monotonic_ns()
    result = subprocess.run(command, capture_output=True, text=True, timeout=600, check=False)
    log.write_text(result.stdout + result.stderr)
    if result.returncode:
        raise RuntimeError(f"exit {result.returncode}: {command}; see {log}")
    elapsed = time.monotonic_ns() - started
    rows: list[dict[str, Any]] = []
    for line in result.stdout.splitlines():
        if line.startswith("{"):
            row: dict[str, Any] = json.loads(line)
            row["process_wall_ns"] = elapsed
            rows.append(row)
    return rows


def cases(recipe: Path, records: int, primary: bool = False) -> list[tuple[str, int]]:
    with recipe.open("rb") as stream:
        magic, count, producers, _revision, length = struct.unpack(
            "<8s4Q", stream.read(40)
        )
        if magic != b"DTMIXED1" or count != records or length:
            raise ValueError("requires the preserved four-namespace mixed recipe")
        ordinals = random.Random(0x524541444552).sample(range(records), 200)
        result: list[tuple[str, int]] = []
        for ordinal in ordinals:
            stream.seek(40 + ordinal * 8)
            generation = struct.unpack("<q", stream.read(8))[0]
            if generation < 0 or (ordinal + generation) % 2 == 0:
                continue
            magnitude = (generation * 104729 + ordinal * 17) % (1 << 60)
            value = -magnitude if (ordinal + generation) & 2 else magnitude
            namespace = "primary" if primary else f"load{ordinal % producers}"
            result.append((f"{namespace}:key{ordinal}", value))
            if len(result) == 40:
                return result
    raise ValueError("fewer than forty integer cases")


def warm_files(directory: Path) -> None:
    for path in sorted(directory.iterdir()):
        if path.is_file():
            with path.open("rb") as stream:
                while stream.read(1024 * 1024):
                    pass


def first_reads(
    probe: Path, store: Path, recipe: Path, records: int, rounds: int, out: Path
) -> dict[str, Any]:
    selected = cases(recipe, 5_000_000, "ns" in store.name)
    warm_files(store)
    samples: list[dict[str, Any]] = []
    with (out / f"first-{store.parent.name}-{store.name}.jsonl").open("x") as stream:
        for round_number in range(rounds):
            for index, (key, value) in enumerate(selected):
                command = [str(probe), "--read", str(store), key, str(value)]
                log = out / "last-first-process.log"
                run(command, log)
                rows = run(command, log)
                if len(rows) != 1 or rows[0]["records"] != records:
                    raise ValueError("first-read cardinality differs")
                sample = rows[0] | {"round": round_number, "case": index, "key": key}
                samples.append(sample)
                stream.write(json.dumps(sample) + "\n")
                stream.flush()
    values = [sample["first_live_ns"] for sample in samples]
    return {
        "samples": len(values),
        "below_1ms": sum(value < 1_000_000 for value in values),
        "median_ns": statistics.median(values),
        "maximum_ns": max(values),
        "major_faults": sum(sample["major_faults"] for sample in samples),
        "publisher": "stopped",
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--trials", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--mode", choices=("smoke", "confirm", "first"), required=True)
    args = parser.parse_args()
    out: Path = args.output
    out.mkdir(parents=True, exist_ok=False)
    report: dict[str, Any] = {
        "mode": args.mode,
        "platform": platform.platform(),
        "machine": platform.machine(),
        "started_ns": time.time_ns(),
        "inputs": {},
        "first": {},
        "status": "incomplete",
    }
    try:
        for name, root in (("baseline", args.baseline), ("candidate", args.candidate)):
            for binary in ("prepared_client_probe", "mixed_oracle"):
                path = root / binary
                architecture = subprocess.check_output(["file", str(path)], text=True)
                if platform.machine() not in architecture:
                    raise ValueError(f"binary architecture differs: {architecture}")
                report["inputs"][f"{name}-{binary}"] = {
                    "path": str(path),
                    "sha256": digest(path),
                    "file": architecture.strip(),
                }
        if args.mode != "first":
            size = "100k" if args.mode == "smoke" else "5m"
            records = 100_000 if size == "100k" else 5_000_000
            trial = "100000-r3" if size == "100k" else "5000000-restore-r12"
            recipe = args.trials / trial / "expected-values.bin"
            order = ["baseline", "baseline", "candidate", "baseline"]
            if args.mode == "confirm":
                order += [
                    "candidate",
                    "baseline",
                    "baseline",
                    "candidate",
                    "candidate",
                    "baseline",
                ]
            report["order"] = order
            for index, name in enumerate(order):
                root = args.baseline if name == "baseline" else args.candidate
                store = root / f"store-{size}"
                selected = (store / "selected").read_bytes()
                identity = [
                    selected[begin:end].hex()
                    for begin, end in ((72, 88), (88, 104), (104, 120), (120, 152))
                ]
                calls = "10000" if args.mode == "smoke" else "1000000"
                command = [
                    str(root / "mixed_oracle"),
                    str(store),
                    str(recipe),
                    *identity,
                    str(records),
                    "4294967296",
                    "4294967296",
                    "12000000",
                    "16384",
                    "4294967296",
                    "4294967296",
                    "1000",
                    "256",
                    "--benchmark",
                    calls,
                    calls,
                ]
                warm_files(store)
                rows = run(command, out / f"warm-{index}-{name}.jsonl")
                if not rows or rows[-1].get("records") != records:
                    raise ValueError("oracle did not verify the expected corpus")
        if args.mode in ("smoke", "first"):
            rounds = 1 if args.mode == "smoke" else 5
            recipe = args.trials / "5000000-restore-r12" / "expected-values.bin"
            for name, root in (
                ("baseline", args.baseline),
                ("candidate", args.candidate),
            ):
                for suffix in ("5m", "1ns", "1000ns"):
                    store = root / f"store-{suffix}"
                    if not store.exists():
                        if name == "candidate" or suffix == "5m":
                            raise ValueError(f"required first-read arm missing: {store}")
                        continue
                    records = 5_099_900 if suffix == "1000ns" else 5_000_000
                    # Recipes describe the unchanged five million primary records.
                    report["first"][f"{name}-{suffix}"] = first_reads(
                        root / "prepared_client_probe",
                        store,
                        recipe,
                        records,
                        rounds,
                        out,
                    )
        if args.mode == "first":
            for suffix in ("1ns", "1000ns"):
                arm = report["first"][f"candidate-{suffix}"]
                if arm["samples"] != 200 or arm["below_1ms"] != 200:
                    report["status"] = "failed-first-read-gate"
                    raise ValueError(f"candidate-{suffix} misses the 200/200 first-read gate")
        report["status"] = "completed"
    finally:
        report["ended_ns"] = time.time_ns()
        (out / "result.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
