from __future__ import annotations

import json
import statistics
from pathlib import Path
from typing import Any

HERE = Path(__file__).resolve().parent


def main() -> None:
    report: dict[str, Any] = json.loads((HERE / "screen.json").read_text())
    rows = report["samples"]
    if report["status"] != "completed" or len(rows) != 80:
        raise ValueError("Incomplete four-arm screen")
    by_arm: dict[str, dict[str, dict[str, Any]]] = {}
    for row in rows:
        arm = by_arm.setdefault(row["arm"], {})
        if row["key"] in arm:
            raise ValueError("Duplicate key within arm")
        arm[row["key"]] = row
    expected = {
        "private-current",
        "private-capsule",
        "shared-current",
        "shared-capsule",
    }
    if set(by_arm) != expected or any(len(arm) != 20 for arm in by_arm.values()):
        raise ValueError("Wrong arms or cardinality")
    totals: dict[str, Any] = {}
    passing = True
    for resource in ("private", "shared"):
        current, capsule = by_arm[resource + "-current"], by_arm[resource + "-capsule"]
        if current.keys() != capsule.keys():
            raise ValueError("Unpaired keys")
        statistics_by_metric: dict[str, Any] = {}
        for metric in (
            "wall_ns",
            "caller_cpu_ns",
            "process_cpu_ns",
            "caller_allocation_events",
            "minor_faults",
            "major_faults",
        ):
            a = [
                sum(phase[metric] for phase in row["phases"])
                for row in current.values()
            ]
            b = [
                sum(phase[metric] for phase in capsule[key]["phases"])
                for key in current
            ]
            differences = [after - before for before, after in zip(a, b, strict=True)]
            delta = statistics.median(differences)
            statistics_by_metric[metric] = {
                "current_median": statistics.median(a),
                "capsule_median": statistics.median(b),
                "paired_delta_median": delta,
                "capsule_lower_count": sum(value < 0 for value in differences),
                "samples": len(a),
            }
            if metric in ("wall_ns", "caller_cpu_ns") and delta >= 0:
                passing = False
        totals[resource] = statistics_by_metric
    result = {
        "verdict": "PASS PROTOTYPE SCREEN" if passing else "NO-GO: DISCARD PROTOTYPE",
        "criterion": "Paired median factory+client+lookup wall and caller CPU benefit separately in both resource closures. Not uninstrumented200/200 acceptance or production adoption.",
        "statistics": totals,
    }
    (HERE / "screen-summary.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
