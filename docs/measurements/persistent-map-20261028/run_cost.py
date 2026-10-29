from __future__ import annotations

import hashlib
import itertools
import json
import os
import subprocess
from datetime import UTC, datetime
from pathlib import Path
from typing import Any

root = Path(__file__).resolve().parent
output = root / "tree-cost.json"
if output.exists():
    raise ValueError("Refuse to replace a completed or interrupted batch")
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
    "method": "Three rotating triplets of two identical private-codec controls and one generic-map extraction per count/key-width/batch/retained-version case. Identical generated production-style keys; no observed malloc hook. Each fresh process has a matching warmup. Caller CPU measures phases; heap statistics include the process allocator, not just tree nodes. Collection turn maxima include per-turn clock calls. This is a tree-only screen, not full consumer qualification.",
    "hashes": {
        name: hashlib.sha256((root / name).read_bytes()).hexdigest()
        for name in ("tree_cost.cc", "run_cost.py", "tree-before", "tree-after")
    },
    "samples": samples,
}
try:
    cases = itertools.product((1000, 100000), (34, 200), (1, 128), (1, 16))
    for case_index, case in enumerate(cases):
        for pair in range(3):
            arms = ("control-a", "control-b", "after")
            offset = (case_index + pair) % len(arms)
            expected: dict[str, Any] | None = None
            for arm in arms[offset:] + arms[:offset]:
                binary = root / ("tree-after" if arm == "after" else "tree-before")
                command = [str(binary), *(str(value) for value in case)]
                subprocess.run(
                    command,
                    check=True,
                    capture_output=True,
                    env=environment,
                    timeout=60,
                )
                result = subprocess.run(
                    command,
                    check=True,
                    capture_output=True,
                    text=True,
                    env=environment,
                    timeout=60,
                )
                row: dict[str, Any] = json.loads(result.stdout)
                identity = {
                    key: row[key]
                    for key in (
                        "count",
                        "width",
                        "batch",
                        "retained",
                        "retained_nodes",
                        "allocated_nodes",
                        "collected_nodes",
                        "collection_turns",
                        "lookup_sum",
                        "export_sum",
                    )
                }
                if expected is not None and identity != expected:
                    raise RuntimeError(
                        f"Logical or tree identity differs: {identity} != {expected}"
                    )
                expected = identity
                row.update(arm=arm, pair=pair)
                samples.append(row)
    report["status"] = "passed"
except BaseException as error:
    report.update(status="failed", error=repr(error))
    raise
finally:
    report["finished_utc"] = datetime.now(UTC).isoformat()
    output.write_text(json.dumps(report, indent=2) + "\n")
