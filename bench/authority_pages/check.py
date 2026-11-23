"""Checks one authority_pages_bench run and prints its summary; exits nonzero when an arm did not run as named.

check.py seals NAMESPACES SEALS < lines
check.py commit ARM... < lines
check.py combine LOG...

`combine` reads one commit job's log per LOG (one rotation of the arms each) and prints, per arm, the
median and min-max over jobs of its group commit p50, and of its paired delta against the none run of
the same job.
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
    if mode == "combine":
        combine(sys.argv[2:])
        return
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
    named = sys.argv[2:]
    runs = steps.get("run", [])
    by_arm: dict[str, list[dict[str, object]]] = {}
    for run in runs:
        by_arm.setdefault(str(run["arm"]), []).append(run)
    if [str(r["arm"]) for r in runs] != named:
        fail(f"arms ran {[r['arm'] for r in runs]}, not {named}")
    for arm, arm_runs in by_arm.items():
        for run in arm_runs:
            seals, builds = int(run["seals"]), int(run["builds"])
            if seals < 1:
                fail(f"{arm} round {run['round']}: no authority seal")
            if arm == "none" and builds:
                fail("arm none built pages")
            if arm != "none" and builds < seals:
                fail(f"{arm} round {run['round']}: {builds} builds for {seals} seals")
            if arm == "write" and int(run["pages_written"]) <= 0:
                fail("arm write wrote no pages")
    print(json.dumps({arm: summarize(arm_runs) for arm, arm_runs in by_arm.items()}))


def summarize(runs: list[dict[str, object]]) -> dict[str, object]:
    p50 = [float(r["group_ms_p50"]) for r in runs]
    return {
        "group_ms_p50_median": statistics.median(p50),
        "group_ms_p50_range": [min(p50), max(p50)],
        "group_ms_p99_median": statistics.median(
            float(r["group_ms_p99"]) for r in runs
        ),
        "worst_service_gap_ms_max": max(float(r["worst_service_gap_ms"]) for r in runs),
        "tx_per_s_median": statistics.median(float(r["tx_per_s"]) for r in runs),
        "build_cpu_ms_per_seal_median": statistics.median(
            float(r["build_cpu_ms"]) / max(int(r["seals"]), 1) for r in runs
        ),
    }


def combine(logs: list[str]) -> None:
    by_arm: dict[str, list[dict[str, object]]] = {}
    deltas: dict[str, list[float]] = {}
    for log in logs:
        with open(log) as lines:
            runs = [
                row
                for row in (
                    json.loads(line)
                    for line in lines
                    if line.startswith('{"step":"run"')
                )
            ]
        none = [float(r["group_ms_p50"]) for r in runs if r["arm"] == "none"]
        if len(none) != 1:
            fail(f"{log}: {len(none)} none runs, not 1")
        for run in runs:
            by_arm.setdefault(str(run["arm"]), []).append(run)
            deltas.setdefault(str(run["arm"]), []).append(
                float(run["group_ms_p50"]) - none[0]
            )
    summary = {}
    for arm, runs in by_arm.items():
        summary[arm] = summarize(runs) | {
            "jobs": len(runs),
            "delta_ms_p50_median": statistics.median(deltas[arm]),
            "delta_ms_p50_range": [min(deltas[arm]), max(deltas[arm])],
        }
    print(json.dumps(summary, indent=1))


if __name__ == "__main__":
    main()
