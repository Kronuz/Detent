"""Alternate two builds' model benchmarks: five pairs per workload, interference-guarded.

Usage: container_pair.py OUTPUT_DIR NAME=BENCHMARK_DIR NAME=BENCHMARK_DIR
Set KNOBS=N for a dry run size and DRY=1 to skip the interference guard.
"""

from __future__ import annotations

import hashlib
import json
import os
import subprocess
import sys
import time
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from logging_compare import check_interference

ARMS: dict[str, Path] = {}
LIFECYCLE = [[], ["--random"], ["--long"], ["--long", "--random"]]
LOOKUP = [[], ["--hot"], ["--long"], ["--long", "--hot"]]


def ancestors() -> set[str]:
    pids, pid = set(), os.getpid()
    while pid > 1:
        pids.add(str(pid))
        pid = int(
            subprocess.check_output(["ps", "-o", "ppid=", "-p", str(pid)], text=True)
        )
    return pids


def check(destination: Path) -> None:
    if os.environ.get("DRY"):
        return
    try:
        check_interference(destination)
    except RuntimeError as error:
        mine = ancestors()
        lines = [
            line for line in str(error).splitlines()[1:] if line.split()[0] not in mine
        ]
        if lines:
            raise RuntimeError("\n".join(lines)) from None


def guarded(cmd: list[str], out: Path, label: str) -> tuple[str, int]:
    for attempt in range(12):
        try:
            check(out / f"{label}-{attempt}-before.txt")
            text = subprocess.run(
                cmd, capture_output=True, text=True, check=True, timeout=600
            ).stdout
            check(out / f"{label}-{attempt}-after.txt")
            return text, attempt
        except RuntimeError as error:
            (out / f"{label}-{attempt}-rejected.txt").write_text(str(error))
            time.sleep(45)
    raise RuntimeError(f"{label}: interference persisted")


def main() -> None:
    out = Path(sys.argv[1])
    for spec in sys.argv[2:4]:
        name, directory = spec.split("=", 1)
        ARMS[name] = Path(directory).resolve()
    out.mkdir(parents=True, exist_ok=False)
    run_started = time.time()
    samples: list[dict[str, Any]] = []
    for bench, configs in (
        ("detent_model_lifecycle", LIFECYCLE),
        ("detent_model_lookup", LOOKUP),
    ):
        for config in configs:
            for pair in range(5):
                arms = list(ARMS.items())
                if pair % 2:
                    arms.reverse()
                for arm, directory in arms:
                    label = f"{bench}-{'_'.join(a.strip('-') for a in config) or 'short'}-{pair}-{arm}"
                    started = time.time()
                    extra = os.environ.get("KNOBS")
                    knobs = ["--knobs", extra] if extra else []
                    text, retries = guarded(
                        [str(directory / bench), *config, *knobs], out, label
                    )
                    rows = [
                        json.loads(line)
                        for line in text.splitlines()
                        if line.startswith("{")
                    ]
                    samples.append(
                        {
                            "bench": bench,
                            "config": config,
                            "pair": pair,
                            "arm": arm,
                            "retries": retries,
                            "started": started,
                            "seconds": time.time() - started,
                            "rows": rows,
                        }
                    )
                    print(label, retries, flush=True)
                    (out / "samples.json").write_text(
                        json.dumps(samples, indent=1) + "\n"
                    )
    binaries = {
        arm: {
            b: hashlib.sha256((d / b).read_bytes()).hexdigest()
            for b in ("detent_model_lifecycle", "detent_model_lookup")
        }
        for arm, d in ARMS.items()
    }
    (out / "report.json").write_text(
        json.dumps(
            {
                "binaries": binaries,
                "run_seconds": time.time() - run_started,
                "samples": samples,
            },
            indent=1,
        )
        + "\n"
    )


if __name__ == "__main__":
    main()
