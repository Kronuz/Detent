from __future__ import annotations

import json
import os
import subprocess
from pathlib import Path
from typing import Any

from run import check_steps

here = Path(__file__).resolve().parent
repo = here.parents[2]
manifest: dict[str, Any] = json.loads(
    (repo / "docs/measurements/prepared-managed-5m-20261026.json").read_text()
)
case = manifest["cases"][0]
for arm in ("before", "after"):
    for observe in (False, True):
        result = subprocess.run(
            [
                str(here / arm / "probe"),
                "--read",
                str(repo / ".scratch/fast-client-build/managed-5m-r3/generations"),
                str(case["key"]),
                str(case["expected_int"]),
                "--allocations",
            ],
            env=dict(os.environ, METADATA_OBSERVER=str(int(observe))),
            capture_output=True,
            text=True,
            check=True,
            timeout=10,
        )
        value: dict[str, Any] = json.loads(result.stdout)
        stages: list[dict[str, Any]] = json.loads(result.stderr)
        if value["records"] != 5000000:
            raise RuntimeError("Wrong cardinality")
        check_steps(stages, observe)
        print(
            json.dumps(
                {
                    "arm": arm,
                    "observer": observe,
                    "stages": len(stages),
                    "returned_bytes": sum(row["returned_bytes"] for row in stages),
                }
            )
        )
