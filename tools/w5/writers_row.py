#!/usr/bin/env python3
"""One sustained-writers run of one tree, printed as table rows for ab_compare.py.

  writers_row.py BUILD_DIR SOURCE_DIR [--duration 20]

Runs the tree's own tests/detent_system/run_writers.py (three authority voters over HTTPS, 16 producers, 1,000
records) and prints the HTTP requests the voters completed per second and the mean command latency. Run it
through ab_rounds.py for old and new, then compare with
  ab_compare.py compare --format table --higher-better ... (requests per second)
"""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=Path)
    parser.add_argument("source", type=Path)
    parser.add_argument("--duration", type=int, default=20)
    args = parser.parse_args()
    out = Path(tempfile.mkdtemp(prefix="w5-writers-", dir=".scratch")) / "run"
    try:
        result = subprocess.run(
            [
                sys.executable,
                str(args.source / "tests/detent_system/run_writers.py"),
                "--scenario",
                str(args.source / "tests/detent_system/presets/smoke.json"),
                "--binary",
                str(args.build / "src/detentd"),
                "--build",
                str(args.build),
                "--certificates",
                str(args.build / "tests/detent_authority/tls-fixture"),
                "--output",
                str(out),
                "--seed",
                "17",
                "--duration",
                str(args.duration),
            ],
            capture_output=True,
            text=True,
            check=False,
        )
        if result.returncode:
            print(result.stderr[-600:], file=sys.stderr)
            return 2
        verified = next(
            e
            for e in map(json.loads, (out / "events.jsonl").read_text().splitlines())
            if e.get("kind") == "verified"
        )
        completed = verified["http_final"]["completed"] - verified["http_initial"]["completed"]
        seconds = verified["sustained_seconds"]
        print(f"http_requests_per_s {completed / seconds:.3f}")
        print(
            f"command_latency_mean_s {verified['command_latency_total_seconds'] / max(1, completed):.5f}"
        )
        return 0
    finally:
        shutil.rmtree(out.parent, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
