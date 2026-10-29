from __future__ import annotations

import hashlib
import json
import os
import subprocess
from datetime import UTC, datetime
from pathlib import Path
from typing import Any

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]


def main() -> None:
    output = HERE / "result.json"
    if output.exists():
        raise ValueError("Refuse to replace a prototype screen")
    manifest: dict[str, Any] = json.loads(
        (REPO / "docs/measurements/prepared-managed-5m-20261026.json").read_text()
    )
    cases = manifest["cases"][:20]
    if len(cases) != 20:
        raise ValueError("Expected the unchanged first twenty keys")
    arms = ("private-current", "private-proposed", "shared-current", "shared-proposed")
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
        "method": "One counterbalanced four-arm screen: private/canonical resources crossed with current/inline ancestor owners, first twenty unchanged keys of the same prepared 5M generation. Matching fresh-process warmup for every arm, rotated order. Same malloc-event counter and explicit ancestry-check call tracing in all arms; original production archives unchanged. Identical trace requirement covers explicit directory open/stat/ACL/readlink checks; descriptor cleanup is tested separately. This is a prototype screen, not uninstrumented acceptance.",
        "hashes": {
            name: hashlib.sha256((HERE / name).read_bytes()).hexdigest()
            for name in ("trace.hh", "build.py", "run.py")
        },
        "binaries": {
            arm: hashlib.sha256((HERE / arm / "probe").read_bytes()).hexdigest()
            for arm in arms
        },
        "samples": rows,
    }
    reference: list[dict[str, Any]] | None = None
    try:
        directory = REPO / ".scratch/fast-client-build/managed-5m-r3/generations"
        for index, case in enumerate(cases):
            offset = index % len(arms)
            for arm in arms[offset:] + arms[:offset]:
                command = [
                    str(HERE / arm / "probe"),
                    "--read",
                    str(directory),
                    str(case["key"]),
                    str(case["expected_int"]),
                    "--allocations",
                ]
                subprocess.run(
                    command,
                    check=True,
                    env=environment,
                    capture_output=True,
                    timeout=10,
                )
                measured = subprocess.run(
                    command,
                    check=True,
                    env=environment,
                    capture_output=True,
                    text=True,
                    timeout=10,
                )
                value: dict[str, Any] = json.loads(measured.stdout)
                trace: list[dict[str, Any]] = json.loads(measured.stderr)
                if value["records"] != 5000000 or not trace:
                    raise RuntimeError("Missing corpus identity or ancestry trace")
                if reference is None:
                    reference = trace
                if trace != reference:
                    raise RuntimeError(f"Ancestry checks differ in {arm}")
                if any(row["status"] != 1 or row["error"] != 0 for row in trace):
                    raise RuntimeError("Unexpected failed ancestry operation")
                rows.append(
                    {
                        "arm": arm,
                        "key": case["key"],
                        "phases": value["phases"],
                        "trace": trace,
                    }
                )
        report["status"] = "passed"
    except BaseException as error:
        report.update(status="failed", error=repr(error))
        raise
    finally:
        report["finished_utc"] = datetime.now(UTC).isoformat()
        output.write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
