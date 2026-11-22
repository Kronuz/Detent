"""Checks one authority_pages_bench run and prints its summary; exits nonzero when an arm did not run as named.

check.py seals NAMESPACES SEALS < lines
check.py commit ROUNDS < lines
"""

from __future__ import annotations

import json
import statistics
import sys


def fail(why: str) -> None:
    print(f"check failed: {why}", file=sys.stderr)
    sys.exit(1)


def main() -> None:
    mode = sys.argv[1]
    rows = [json.loads(line) for line in sys.stdin if line.startswith("{")]
    steps: dict[str, list[dict[str, object]]] = {}
    for row in rows:
        steps.setdefault(str(row.get("step", row.get("mode"))), []).append(row)
    if mode == "seals":
        spaces, seals = int(sys.argv[2]), int(sys.argv[3])
        cold = steps["cold"][0]
        if cold["builds"] != spaces:
            fail(f"cold built {cold['builds']} namespaces, not {spaces}")
        if len(steps.get("seal", [])) != seals or any(
            int(s["builds"]) < 1 for s in steps["seal"]
        ):
            fail("a seal built nothing")
        summary = steps["summary"][0]
        if int(summary["written_per_seal"]) <= 0:
            fail("seals wrote no pages")
        print(json.dumps({"cold": cold, "summary": summary}))
        return
    rounds = int(sys.argv[2])
    runs = steps.get("run", [])
    by_arm: dict[str, list[dict[str, object]]] = {}
    for run in runs:
        by_arm.setdefault(str(run["arm"]), []).append(run)
    for arm in ("none", "thread", "inline", "write"):
        if len(by_arm.get(arm, [])) != rounds:
            fail(f"arm {arm} ran {len(by_arm.get(arm, []))} times, not {rounds}")
        for run in by_arm[arm]:
            seals, builds = int(run["seals"]), int(run["builds"])
            if seals < 1:
                fail(f"{arm} round {run['round']}: no authority seal")
            if arm == "none" and builds:
                fail("arm none built pages")
            if arm != "none" and builds < seals:
                fail(f"{arm} round {run['round']}: {builds} builds for {seals} seals")
            if arm == "write" and int(run["pages_written"]) <= 0:
                fail("arm write wrote no pages")
    summary = {}
    for arm, arm_runs in by_arm.items():
        p50 = [float(r["group_ms_p50"]) for r in arm_runs]
        summary[arm] = {
            "group_ms_p50_median": statistics.median(p50),
            "group_ms_p50_range": [min(p50), max(p50)],
            "group_ms_p99_median": statistics.median(
                float(r["group_ms_p99"]) for r in arm_runs
            ),
            "worst_service_gap_ms_max": max(
                float(r["worst_service_gap_ms"]) for r in arm_runs
            ),
            "tx_per_s_median": statistics.median(
                float(r["tx_per_s"]) for r in arm_runs
            ),
        }
    print(json.dumps(summary))


if __name__ == "__main__":
    main()
