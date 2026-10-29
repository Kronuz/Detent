from __future__ import annotations

import hashlib
import json
import os
import subprocess
from datetime import UTC, datetime
from pathlib import Path
from typing import Any

root = Path(__file__).resolve().parent
output = root / "production-cost.json"
if output.exists():
    raise ValueError("Refuse to overwrite a cost batch")
binaries = {
    "before": root.parent
    / "resource-adoption-20261027/baseline-build/benchmarks/detent_authority_bench",
    "after": root / "build/benchmarks/detent_authority_bench",
}
environment = {
    key: value
    for key, value in os.environ.items()
    if not key.startswith(("DETENT_", "DYLD_", "Malloc")) and key != "LD_PRELOAD"
}
environment["LC_ALL"] = "C"
samples: list[dict[str, Any]] = []
report: dict[str, Any] = {
    "status": "incomplete",
    "started_utc": datetime.now(UTC).isoformat(),
    "method": "Three alternating fresh-process pairs per 1K/100K authority knobs and 2/16 switch overrides, uniform namespaces. Each arm receives a matching warmup per case. Unchanged production authority benchmark verifies seeded values and update admission, records load/update caller CPU, image encoding elapsed time and peak process RSS. Legacy snapshot-copy comparison is included by the unchanged benchmark. This does not measure durable ACK or fleet propagation.",
    "binary_hashes": {
        arm: hashlib.sha256(path.read_bytes()).hexdigest()
        for arm, path in binaries.items()
    },
    "driver_hash": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
    "samples": samples,
}
try:
    for count in (1000, 100000):
        for overrides in (2, 16):
            for arm, binary in binaries.items():
                subprocess.run(
                    [str(binary), str(count), "uniform", str(overrides)],
                    env=environment,
                    check=True,
                    capture_output=True,
                    timeout=120,
                )
            for pair in range(3):
                arms = ("before", "after") if pair % 2 == 0 else ("after", "before")
                identity: dict[str, Any] | None = None
                for arm in arms:
                    result = subprocess.run(
                        [str(binaries[arm]), str(count), "uniform", str(overrides)],
                        env=environment,
                        check=True,
                        capture_output=True,
                        text=True,
                        timeout=120,
                    )
                    row: dict[str, Any] = json.loads(result.stdout)
                    current = {
                        key: row[key]
                        for key in (
                            "knobs",
                            "namespaces",
                            "overrides",
                            "image_bytes",
                            "nodes_allocated_per_update",
                            "retained_nodes",
                        )
                    }
                    if identity is not None and current != identity:
                        raise RuntimeError(
                            f"Logical/tree counts differ: {current} != {identity}"
                        )
                    identity = current
                    row.update(arm=arm, pair=pair)
                    samples.append(row)
    report["status"] = "passed"
except BaseException as error:
    report.update(status="failed", error=repr(error))
    raise
finally:
    report["finished_utc"] = datetime.now(UTC).isoformat()
    output.write_text(json.dumps(report, indent=2) + "\n")
