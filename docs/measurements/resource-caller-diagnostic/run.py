from __future__ import annotations

import hashlib
import json
import os
import subprocess
from datetime import UTC, datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT.parents[1]


def main() -> None:
    output = ROOT / "diagnostic/result.json"
    if output.exists():
        raise ValueError("Diagnostic batch already exists")
    manifest: dict[str, list[dict[str, str | int]]] = json.loads(
        (REPO / "docs/measurements/prepared-managed-5m-20261026.json").read_text()
    )
    cases = [(str(row["key"]), int(row["expected_int"])) for row in manifest["cases"]]
    directory = REPO / ".scratch/fast-client-build/managed-5m-r3/generations"
    env = {
        key: value
        for key, value in os.environ.items()
        if not key.startswith(("DETENT_", "DYLD_", "Malloc")) and key != "LD_PRELOAD"
    }
    env["LC_ALL"] = "C"
    # Smoke-check observer support before the fixed diagnostic batch.
    for arm in ("before", "after"):
        for observed in (False, True):
            command = [
                str(ROOT / f"diagnostic/{arm}"),
                "--read",
                str(directory),
                cases[0][0],
                str(cases[0][1]),
            ]
            if observed:
                command.append("--allocations")
            subprocess.run(
                command, check=True, capture_output=True, env=env, timeout=30
            )
    samples: list[dict[str, object]] = []
    report: dict[str, object] = {
        "status": "incomplete",
        "started_utc": datetime.now(UTC).isoformat(),
        "method": "One fixed diagnostic batch: 40 keys, two rounds, before/after and unhooked/allocation-logging modes, counterbalanced order. Separate matching warmup before each timed process. CPU/fault timing is analyzed only in unhooked mode; allocation logging adds overhead and is not acceptance timing. Caller CPU uses CLOCK_THREAD_CPUTIME_ID; faults and switches are process-wide. Successful malloc-logger allocation/reallocation events are caller-thread only. Both arms retain factory, Client construction and first lookup inside measured phases.",
        "observer_source_sha256": hashlib.sha256(
            (ROOT / "diagnostic/probe.cc").read_bytes()
        ).hexdigest(),
        "binaries": {
            arm: hashlib.sha256((ROOT / f"diagnostic/{arm}").read_bytes()).hexdigest()
            for arm in ("before", "after")
        },
        "samples": samples,
    }
    try:
        for round_index in range(2):
            for index, (key, expected) in enumerate(cases):
                conditions = [
                    ("before", False),
                    ("after", False),
                    ("before", True),
                    ("after", True),
                ]
                if (round_index + index) % 2:
                    conditions.reverse()
                for arm, observed in conditions:
                    command = [
                        str(ROOT / f"diagnostic/{arm}"),
                        "--read",
                        str(directory),
                        key,
                        str(expected),
                    ]
                    if observed:
                        command.append("--allocations")
                    subprocess.run(
                        command, check=True, capture_output=True, env=env, timeout=30
                    )
                    raw = subprocess.check_output(
                        command, text=True, env=env, timeout=30
                    )
                    sample: dict[str, object] = json.loads(raw)
                    if (
                        sample["records"] != 5000000
                        or sample["allocation_logging"] != observed
                    ):
                        raise RuntimeError("Invalid diagnostic output")
                    sample.update(arm=arm, round=round_index, key=key)
                    samples.append(sample)
        report["status"] = "passed"
    except BaseException as error:
        report.update(status="failed", error=repr(error))
        raise
    finally:
        output.write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
