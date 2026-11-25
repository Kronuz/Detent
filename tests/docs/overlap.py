#!/usr/bin/env python3
"""Run run_e2e.py twice at once against one build; both must pass on different ports.

overlap.py --build-dir DIR
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--build-dir", type=Path, required=True)
    args = ap.parse_args()
    cmd = [
        sys.executable,
        "-I",
        str(HERE / "run_e2e.py"),
        "--build-dir",
        str(args.build_dir),
    ]
    runs = [
        subprocess.Popen(
            cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True
        )
        for _ in range(2)
    ]
    outputs = [r.communicate()[0] for r in runs]
    ports: list[str] = []
    for i, (run, out) in enumerate(zip(runs, outputs, strict=True)):
        match = re.search(r"^run port (\d+)", out, re.MULTILINE)
        ports.append(match.group(1) if match else "?")
        print(
            f"run {i}: port {ports[-1]} exit {run.returncode}: {out.strip().splitlines()[-1] if out.strip() else ''}"
        )
        if run.returncode:
            print(out)
    leftovers = subprocess.run(
        ["lsof", "-ti", f"tcp:{ports[0]},{ports[1]}", "-sTCP:LISTEN"],
        capture_output=True,
        text=True,
        check=False,
    ).stdout.split()
    ok = (
        all(r.returncode == 0 for r in runs)
        and ports[0] != ports[1]
        and "?" not in ports
        and not leftovers
    )
    print(
        f"overlap: ports {ports[0]} and {ports[1]}, leftover listeners {leftovers or 'none'}:",
        "ok" if ok else "FAILED",
    )
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
