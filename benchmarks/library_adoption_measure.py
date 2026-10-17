"""Measure frozen Detent benchmark executables on macOS."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import time
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class Workload:
    name: str
    program: str
    arguments: tuple[str, ...]
    samples: int = 5


def workloads() -> list[Workload]:
    cases: list[Workload] = []
    for program in ("detent_read_bench", "detent_agent_shm_bench"):
        for threads in (1, 4, 8):
            cases.append(
                Workload(f"{program}-{threads}", program, (str(threads), "2000000"))
            )
    cases.append(Workload("hash", "detent_hash_bench", ("5000000",)))
    for count in (1000, 100000, 500000):
        for codec in ("json", "messagepack"):
            cases.append(
                Workload(
                    f"codec-{codec}-{count}",
                    "detent_distribution_codec_bench",
                    (codec, str(count)),
                )
            )
        cases.append(
            Workload(f"delta-{count}", "detent_delta_decode_bench", (str(count),))
        )
    for count in (100000, 500000):
        for locality in ("uniform", "hot"):
            for overrides in (2, 16):
                cases.append(
                    Workload(
                        f"authority-{count}-{locality}-{overrides}",
                        "detent_authority_bench",
                        (str(count), locality, str(overrides)),
                    )
                )
        for program in (
            "detent_local_image_bench",
            "detent_paged_snapshot_bench",
            "detent_paged_recovery_bench",
            "detent_paged_collection_bench",
        ):
            cases.append(Workload(f"{program}-{count}", program, (str(count),), 3))
        for producers in (16, 64):
            cases.append(
                Workload(
                    f"durable-{count}-{producers}",
                    "detent_authority_durable_bench",
                    ("{directory}", str(count), str(producers)),
                    3,
                )
            )
    cases.append(
        Workload(
            "durable-heavy-128-1",
            "detent_authority_durable_bench",
            ("{directory}", "128", "1", "heavy"),
            3,
        )
    )
    return cases


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--arm", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--samples", type=int)
    parser.add_argument("--case", action="append", dest="selected_cases")
    args = parser.parse_args()
    arm: Path = args.arm.resolve()
    output: Path = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    runtime = output / "runtime"
    runtime.mkdir()
    environment = dict(os.environ, TMPDIR=str(runtime))
    cases = workloads()
    selected: list[str] | None = args.selected_cases
    if selected is not None:
        names = {case.name for case in cases}
        unknown = set(selected) - names
        if unknown:
            raise ValueError(f"unknown cases: {sorted(unknown)}")
        cases = [case for case in cases if case.name in selected]
    (output / "processes-before.txt").write_text(
        subprocess.check_output(["ps", "-axo", "pid,ppid,%cpu,comm"], text=True)
    )
    with (output / "samples.jsonl").open("x") as records:
        for case in cases:
            binary = arm / "bin" / case.program
            digest = hashlib.sha256(binary.read_bytes()).hexdigest()
            samples: int = args.samples if args.samples is not None else case.samples
            if samples < 1:
                raise ValueError("sample count must be positive")
            for sample in range(samples):
                label = f"{case.name}-{sample}"
                directory = runtime / label
                arguments = tuple(
                    a.replace("{directory}", str(directory)) for a in case.arguments
                )
                command = [str(binary), *arguments]
                started = time.monotonic()
                with (
                    (output / f"{label}.stdout").open("x") as stdout,
                    (output / f"{label}.stderr").open("x") as stderr,
                ):
                    result = subprocess.run(
                        ["/usr/bin/time", "-l", *command],
                        stdout=stdout,
                        stderr=stderr,
                        cwd=arm,
                        env=environment,
                        timeout=900,
                        check=False,
                    )
                record = {
                    "workload": case.name,
                    "sample": sample,
                    "command": command,
                    "binary_sha256": digest,
                    "wall_seconds": time.monotonic() - started,
                    "returncode": result.returncode,
                    "stdout": f"{label}.stdout",
                    "stderr": f"{label}.stderr",
                }
                records.write(json.dumps(record) + "\n")
                records.flush()
                print(json.dumps(record), flush=True)
                if result.returncode:
                    raise RuntimeError(f"benchmark failed: {label}")
    (output / "processes-after.txt").write_text(
        subprocess.check_output(["ps", "-axo", "pid,ppid,%cpu,comm"], text=True)
    )


if __name__ == "__main__":
    main()
