from __future__ import annotations

import hashlib
import json
import os
import subprocess
from datetime import UTC, datetime
from pathlib import Path
from typing import Any

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]


def check_steps(steps: list[dict[str, Any]], observe: bool) -> None:
    if not observe:
        if steps:
            raise RuntimeError("Disabled observer emits data")
        return
    table = {(row["file"], row["step"]): row for row in steps}
    if len(table) != len(steps):
        raise RuntimeError("Duplicate accumulated stage")
    for file, length in (
        ("selected", 176),
        ("storage", 64),
        ("image", 320),
        ("metadata", 192),
        ("lease", 128),
    ):
        required = ["open", "checks", "acl", "decode"]
        if file != "lease":
            required += ["close", "total"]
        for step in required:
            if table.get((file, step), {}).get("calls") != 1:
                raise RuntimeError(f"Missing or repeated stage: {file}/{step}")
        if table.get((file, "stat"), {}).get("calls") != (2 if file == "lease" else 1):
            raise RuntimeError(f"Held stat count differs: {file}")
        read = table.get((file, "read"), {})
        if read.get("calls", 0) < 1 or read.get("returned_bytes") != length:
            raise RuntimeError(f"Record byte accounting differs: {file}")
    for file, step in (
        ("general", "total"),
        ("general", "attempt"),
        ("directory", "total"),
        ("directory", "stat"),
        ("lease", "named"),
        ("lease", "binding"),
        ("image", "checksum"),
        ("image", "binding"),
        ("metadata", "binding"),
        ("metadata", "limits"),
    ):
        if table.get((file, step), {}).get("calls") != 1:
            raise RuntimeError(f"Attachment stage differs: {file}/{step}")
    if table.get(("general", "retry"), {}).get("calls", 0) or table.get(
        ("lease", "retry"), {}
    ).get("calls", 0):
        raise RuntimeError("Static-corpus attachment retried")
    if table.get(("lease", "lock"), {}).get("calls", 0) < 1:
        raise RuntimeError("Lease lock was not observed")


def main() -> None:
    output = HERE / "result.json"
    if output.exists():
        raise ValueError("Refuse to overwrite a diagnostic batch")
    validation: dict[str, Any] = json.loads((HERE / "validation.json").read_text())
    if validation["status"] != "passed":
        raise ValueError("Observer validation has not passed")
    manifest: dict[str, Any] = json.loads(
        (REPO / "docs/measurements/prepared-managed-5m-20261026.json").read_text()
    )
    cases = manifest["cases"][:20]
    environment = {
        key: value
        for key, value in os.environ.items()
        if not key.startswith(("DETENT_", "DYLD_", "Malloc")) and key != "LD_PRELOAD"
    }
    environment["LC_ALL"] = "C"
    rows: list[dict[str, Any]] = []
    report: dict[str, Any] = {
        "status": "incomplete",
        "started_utc": datetime.now(UTC).isoformat(),
        "method": "One four-arm diagnostic: private/canonical resources crossed with metadata observer off/on, first 20 unchanged recorded keys, rotating order and matching per-arm warmup. Fixed prepared 5M generation and expected values. All processes enable the same malloc-event counter. Temporary source/archive copies only. Inner times include instrumentation; outer faults are process-wide and may include retirement workers. No acceptance timings or production optimization claims.",
        "hashes": {
            name: hashlib.sha256((HERE / name).read_bytes()).hexdigest()
            for name in (
                "profile.hh",
                "build.py",
                "validate.py",
                "run.py",
                "validation.json",
            )
        },
        "binaries": {
            arm: hashlib.sha256((HERE / arm / "probe").read_bytes()).hexdigest()
            for arm in ("before", "after")
        },
        "samples": rows,
    }
    try:
        directory = REPO / ".scratch/fast-client-build/managed-5m-r3/generations"
        for index, case in enumerate(cases):
            arms = (
                ("before", False),
                ("before", True),
                ("after", False),
                ("after", True),
            )
            offset = index % len(arms)
            for arm, observe in arms[offset:] + arms[:offset]:
                env = dict(environment, METADATA_OBSERVER=str(int(observe)))
                command = [
                    str(HERE / arm / "probe"),
                    "--read",
                    str(directory),
                    str(case["key"]),
                    str(case["expected_int"]),
                    "--allocations",
                ]
                subprocess.run(
                    command, check=True, env=env, capture_output=True, timeout=10
                )
                measured = subprocess.run(
                    command,
                    check=True,
                    env=env,
                    capture_output=True,
                    text=True,
                    timeout=10,
                )
                value: dict[str, Any] = json.loads(measured.stdout)
                steps: list[dict[str, Any]] = json.loads(measured.stderr)
                if value["records"] != 5000000:
                    raise RuntimeError("Corpus cardinality differs")
                check_steps(steps, observe)
                rows.append(
                    {
                        "arm": arm,
                        "observer": observe,
                        "key": case["key"],
                        "phases": value["phases"],
                        "steps": steps,
                    }
                )
        for case in cases:
            for arm in ("before", "after"):
                group = [
                    row
                    for row in rows
                    if row["key"] == case["key"] and row["arm"] == arm
                ]
                if len(group) != 2:
                    raise RuntimeError("Incomplete observer pair")
                allocations = [
                    {
                        phase["phase"]: phase["caller_allocation_events"]
                        for phase in row["phases"]
                    }
                    for row in group
                ]
                if allocations[0] != allocations[1] or any(
                    value is None for value in allocations[0].values()
                ):
                    raise RuntimeError("Observer changes caller allocation counts")
        report["status"] = "passed"
    except BaseException as error:
        report.update(status="failed", error=repr(error))
        raise
    finally:
        report["finished_utc"] = datetime.now(UTC).isoformat()
        output.write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
