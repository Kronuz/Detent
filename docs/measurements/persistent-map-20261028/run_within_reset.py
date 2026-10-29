from __future__ import annotations

import hashlib
import json
import os
import subprocess
from datetime import UTC, datetime
from pathlib import Path
from typing import Any

root = Path(__file__).resolve().parent
output = root / "within-process-reset.json"
if output.exists():
    raise ValueError("Refuse to overwrite a batch")
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
    "method": "Single unresolved 100K/200-byte/batch-one/16-root case. Both private controls and extracted map coexist in one process. All three maps are fully rebuilt outside timing before EVERY round; the same initial 1024-key update trace runs in every round, rotating all six arm orders twice. 1024 updates per measured phase, matched key-lookup warmups before each phase; full values and node counts verified after every round. Three processes and rounds rotate initial map allocation placement; each process has a matching unrecorded warmup. This diagnoses within-process update CPU, not full-system acceptance.",
    "hashes": {
        name: hashlib.sha256((root / name).read_bytes()).hexdigest()
        for name in (
            "within_process_reset.cc",
            "run_within_reset.py",
            "within-process-reset",
        )
    },
    "samples": rows,
}
try:
    for placement in range(3):
        command = [str(root / "within-process-reset"), str(placement)]
        subprocess.run(
            command, env=environment, check=True, capture_output=True, timeout=60
        )
        result = subprocess.run(
            command,
            env=environment,
            check=True,
            capture_output=True,
            text=True,
            timeout=60,
        )
        batch: list[dict[str, Any]] = [
            json.loads(line) for line in result.stdout.splitlines()
        ]
        if len(batch) != 36 or any(row["placement"] != placement for row in batch):
            raise RuntimeError("Incomplete or mismatched process")
        for round_index in range(12):
            group = [row for row in batch if row["round"] == round_index]
            if (
                len(group) != 3
                or {row["arm"] for row in group} != {0, 1, 2}
                or {row["position"] for row in group} != {0, 1, 2}
                or any(row["update_cpu_ns"] <= 0 for row in group)
            ):
                raise RuntimeError("Incomplete same-round controls")
        rows.extend(batch)
    report["status"] = "passed"
except BaseException as error:
    report.update(status="failed", error=repr(error))
    raise
finally:
    report["finished_utc"] = datetime.now(UTC).isoformat()
    output.write_text(json.dumps(report, indent=2) + "\n")
