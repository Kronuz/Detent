from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import time
from datetime import UTC, datetime
from pathlib import Path
from typing import Any

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--smoke", action="store_true")
    arguments = parser.parse_args()
    output = HERE / ("smoke.json" if arguments.smoke else "screen.json")
    if output.exists():
        raise ValueError("Refuse to replace a prototype screen")
    manifest: dict[str, Any] = json.loads(
        (REPO / "docs/measurements/prepared-managed-5m-20261026.json").read_text()
    )
    cases = manifest["cases"][:20]
    if len(cases) != 20:
        raise ValueError("Expected twenty unchanged keys")
    if arguments.smoke:
        cases = cases[:1]
    arms = ("private-current", "private-capsule", "shared-current", "shared-capsule")
    environment = {
        key: value
        for key, value in os.environ.items()
        if not key.startswith(("DETENT_", "DYLD_", "Malloc")) and key != "LD_PRELOAD"
    }
    environment["LC_ALL"] = "C"
    rows: list[dict[str, Any]] = []
    report: dict[str, Any] = {
        "status": "incomplete",
        "scope": "one-key correctness smoke"
        if arguments.smoke
        else "twenty-key prototype screen",
        "started_utc": datetime.now(UTC).isoformat(),
        "method": "One rotated four-arm private/canonical-resource by current/capsule screen. Independent APFS clones of frozen5M corpus, equal-depth/equal-length paths. First20 original keys. Matching fresh-process warmup per sample. Same malloc-event observer in all arms. No metadata attribution instrumentation. End-to-end phase sum is factory+client+lookup. Prototype screening only, not uninstrumented200/200 acceptance.",
        "hashes": {
            name: digest(HERE / name)
            for name in ("build_screen.py", "run_screen.py", "corpus-clones.json")
        },
        "binaries": {arm: digest(HERE / arm / "probe") for arm in arms},
        "samples": rows,
    }
    try:
        for index, case in enumerate(cases):
            offset = index % len(arms)
            for arm in arms[offset:] + arms[:offset]:
                directory = (
                    HERE
                    / ("capsul" if arm.endswith("capsule") else "legacy")
                    / "generations"
                )
                command = [
                    str(HERE / arm / "probe"),
                    "--read",
                    str(directory),
                    str(case["key"]),
                    str(case["expected_int"]),
                    "--allocations",
                ]
                start = time.perf_counter_ns()
                subprocess.run(
                    command,
                    check=True,
                    env=environment,
                    capture_output=True,
                    timeout=10,
                )
                warmup_ns = time.perf_counter_ns() - start
                start = time.perf_counter_ns()
                measured = subprocess.run(
                    command,
                    check=True,
                    env=environment,
                    capture_output=True,
                    text=True,
                    timeout=10,
                )
                process_wall_ns = time.perf_counter_ns() - start
                value: dict[str, Any] = json.loads(measured.stdout)
                if (
                    value["records"] != 5000000
                    or not value["allocation_logging"]
                    or measured.stderr
                ):
                    raise RuntimeError("Unexpected corpus, observer or stderr")
                if [phase["phase"] for phase in value["phases"]] != [
                    "factory",
                    "client",
                    "lookup",
                ]:
                    raise RuntimeError("Missing measurement phases")
                rows.append(
                    {
                        "arm": arm,
                        "key": case["key"],
                        "expected_int": case["expected_int"],
                        "warmup_ns": warmup_ns,
                        "process_wall_ns": process_wall_ns,
                        "phases": value["phases"],
                    }
                )
        report["status"] = "completed"
    except BaseException as error:
        report.update(status="failed", error=repr(error))
        raise
    finally:
        report["finished_utc"] = datetime.now(UTC).isoformat()
        output.write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
