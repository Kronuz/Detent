from __future__ import annotations

import hashlib
import json
import os
import subprocess
from datetime import UTC, datetime
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]


def main() -> None:
    output = HERE / "result.json"
    if output.exists():
        raise ValueError("Refuse to replace a diagnostic batch")
    manifest: dict[str, list[dict[str, str | int]]] = json.loads(
        (REPO / "docs/measurements/prepared-managed-5m-20261026.json").read_text()
    )
    cases = manifest["cases"][:20]
    env = {
        key: value
        for key, value in os.environ.items()
        if not key.startswith(("DETENT_", "DYLD_", "Malloc")) and key != "LD_PRELOAD"
    }
    env["LC_ALL"] = "C"
    directory = REPO / ".scratch/fast-client-build/managed-5m-r3/generations"
    rows: list[dict[str, object]] = []
    report: dict[str, object] = {
        "status": "incomplete",
        "started_utc": datetime.now(UTC).isoformat(),
        "method": "One bounded diagnostic: first 20 recorded keys, one counterbalanced round, own matching warmup process per arm. Temporary instrumented copies of lazy_page_access only; production archives unchanged. Per-step caller CPU, process-wide faults; instrumentation is not acceptance timing.",
        "source_sha256": hashlib.sha256((HERE / "profile.hh").read_bytes()).hexdigest(),
        "binaries": {
            arm: hashlib.sha256((HERE / arm / "probe").read_bytes()).hexdigest()
            for arm in ("before", "after")
        },
        "samples": rows,
    }
    try:
        for index, case in enumerate(cases):
            for arm in ("before", "after") if index % 2 == 0 else ("after", "before"):
                command = [
                    str(HERE / arm / "probe"),
                    "--read",
                    str(directory),
                    str(case["key"]),
                    str(case["expected_int"]),
                ]
                subprocess.run(
                    command, check=True, capture_output=True, env=env, timeout=30
                )
                measured = subprocess.run(
                    command,
                    check=True,
                    capture_output=True,
                    text=True,
                    env=env,
                    timeout=30,
                )
                value: dict[str, object] = json.loads(measured.stdout)
                steps: list[dict[str, str | int]] = json.loads(measured.stderr)
                if (
                    value["records"] != 5000000
                    or not steps
                    or not any(x["step"] == "open_mapping" for x in steps)
                ):
                    raise RuntimeError("Observer did not measure a real mapping")
                rows.append(
                    {
                        "arm": arm,
                        "key": case["key"],
                        "phases": value["phases"],
                        "steps": steps,
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
