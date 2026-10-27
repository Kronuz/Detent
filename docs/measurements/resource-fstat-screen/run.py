from __future__ import annotations

import hashlib
import json
import os
import subprocess
from datetime import UTC, datetime
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
REPO = HERE.parents[2]


def main() -> None:
    output = HERE / "result.json"
    if output.exists():
        raise ValueError("Refuse to replace a screen")
    manifest: dict[str, list[dict[str, str | int]]] = json.loads(
        (REPO / "docs/measurements/prepared-managed-5m-20261026.json").read_text()
    )
    binaries = {
        "private_original": ROOT
        / "baseline-build/tests/detent_system/detent_system_prepared_client_probe",
        "private_reduced": HERE / "private/probe",
        "shared_original": ROOT
        / "reader-build/tests/detent_system/detent_system_prepared_client_probe",
        "shared_reduced": HERE / "shared/probe",
    }
    environment = {
        k: v
        for k, v in os.environ.items()
        if not k.startswith(("DETENT_", "DYLD_", "Malloc")) and k != "LD_PRELOAD"
    }
    environment["LC_ALL"] = "C"
    directory = REPO / ".scratch/fast-client-build/managed-5m-r3/generations"
    selected = hashlib.sha256((directory / "selected").read_bytes()).hexdigest()
    samples: list[dict[str, str | int | float]] = []
    report: dict[str, object] = {
        "status": "incomplete",
        "started_utc": datetime.now(UTC).isoformat(),
        "method": "One 20-key screen, four rotated arms, separate matching warmup for each fresh measured process. Original ordinary SDK probe and timing boundary, no instrumentation. Only one duplicate pre-mapping fstat removed in each reduced arm. No acceptance claim from this small screen.",
        "selected_sha256": selected,
        "binaries": {
            k: hashlib.sha256(v.read_bytes()).hexdigest() for k, v in binaries.items()
        },
        "samples": samples,
    }
    try:
        arms = list(binaries)
        for index, case in enumerate(manifest["cases"][:20]):
            offset = index % len(arms)
            order = arms[offset:] + arms[:offset]
            for arm in order:
                command = [
                    str(binaries[arm]),
                    "--read",
                    str(directory),
                    str(case["key"]),
                    str(case["expected_int"]),
                ]
                subprocess.run(
                    command,
                    check=True,
                    capture_output=True,
                    env=environment,
                    timeout=30,
                )
                measured = subprocess.run(
                    command,
                    check=True,
                    capture_output=True,
                    text=True,
                    env=environment,
                    timeout=30,
                )
                sample: dict[str, str | int | float] = json.loads(measured.stdout)
                if sample["records"] != 5000000:
                    raise RuntimeError("Wrong corpus")
                sample.update(arm=arm, key=case["key"])
                samples.append(sample)
        if (
            hashlib.sha256((directory / "selected").read_bytes()).hexdigest()
            != selected
        ):
            raise RuntimeError("Selected generation changed")
        report["status"] = "passed"
    except BaseException as error:
        report.update(status="failed", error=repr(error))
        raise
    finally:
        report["finished_utc"] = datetime.now(UTC).isoformat()
        output.write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
