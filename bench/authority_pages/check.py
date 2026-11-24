"""Checks one authority_pages_bench run and prints its summary; exits nonzero when an arm did not run as named.

check.py seals NAMESPACES SEALS < lines
check.py commit ARM... < lines
check.py combine LOG...
check.py paced RATE < lines
check.py pairs LOG...
check.py lag < lines

`combine` reads one commit job's log per LOG (one rotation of the arms each) and prints, per arm, the
median and min-max over jobs of its group commit p50, and of its paired delta against the none run of
the same job. `pairs` reads paced logs and prints, per arm, its block p50 minus the mean of the two
none blocks beside it: median, mean with a 95% interval, and min-max, beside the A/A arm's (aa, a none
block labelled apart). An arm differs from none only when its interval clears the A/A band.
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
    if mode == "pairs":
        pairs(sys.argv[2:])
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
    if mode == "paced":
        check_paced(rows, float(sys.argv[2]))
        return
    if mode == "lag":
        check_lag(steps)
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


OTHERS = ["thread", "pool", "write", "aa"]


def check_paced(rows: list[dict[str, object]], rate: float) -> None:
    blocks = [r for r in rows if r.get("step") == "block"]
    if not blocks or len(blocks) % 8:
        fail(f"{len(blocks)} blocks, not whole rounds of 8")
    for b in blocks:
        rnd, slot, arm = int(b["round"]), int(b["slot"]), str(b["arm"])
        want = "none" if slot % 2 == 0 else OTHERS[(slot // 2 + rnd) % 4]
        if arm != want:
            fail(f"round {rnd} slot {slot} ran {arm}, not {want}")
        seals, builds = int(b["seals"]), int(b["builds"])
        if seals < 1:
            fail(f"round {rnd} slot {slot}: no authority seal")
        if arm in ("none", "aa") and builds:
            fail(f"{arm} built pages")
        if arm in ("thread", "pool", "write") and builds < seals:
            fail(f"{arm} round {rnd}: {builds} builds for {seals} seals")
        if arm == "write" and int(b["pages_written"]) <= 0:
            fail("arm write wrote no pages")
    rates = [float(b["tx_per_s"]) for b in blocks]
    print(
        json.dumps(
            {
                "blocks": len(blocks),
                "rate": rate,
                "tx_per_s_median": statistics.median(rates),
                "tx_per_s_range": [min(rates), max(rates)],
                "pairs": paired(blocks),
            }
        )
    )


def deltas_of(blocks: list[dict[str, object]]) -> dict[str, list[float]]:
    deltas: dict[str, list[float]] = {}
    for j, b in enumerate(blocks):
        if b["arm"] == "none":
            continue
        beside = [
            float(blocks[k]["group_ms_p50"])
            for k in (j - 1, j + 1)
            if 0 <= k < len(blocks) and blocks[k]["arm"] == "none"
        ]
        deltas.setdefault(str(b["arm"]), []).append(
            float(b["group_ms_p50"]) - sum(beside) / len(beside)
        )
    return deltas


def stats(d: list[float]) -> dict[str, object]:
    mean = statistics.fmean(d)
    half = 1.96 * statistics.stdev(d) / len(d) ** 0.5 if len(d) > 1 else None
    return {
        "n": len(d),
        "median": round(statistics.median(d), 1),
        "mean": round(mean, 1),
        "ci95": None
        if half is None
        else [round(mean - half, 1), round(mean + half, 1)],
        "range": [round(min(d), 1), round(max(d), 1)],
    }


def paired(blocks: list[dict[str, object]]) -> dict[str, object]:
    return {arm: stats(d) for arm, d in deltas_of(blocks).items()}


def pairs(logs: list[str]) -> None:
    """Pairs within each log, then pools every log's pairs per arm."""
    pooled: dict[str, list[float]] = {}
    per_log = {}
    for log in logs:
        with open(log) as lines:
            blocks = [
                json.loads(line) for line in lines if line.startswith('{"step":"block"')
            ]
        deltas = deltas_of(blocks)
        per_log[log] = {arm: stats(d) for arm, d in deltas.items()}
        for arm, d in deltas.items():
            pooled.setdefault(arm, []).extend(d)
    print(
        json.dumps(
            {"logs": per_log, "pooled": {arm: stats(d) for arm, d in pooled.items()}},
            indent=1,
        )
    )


def check_lag(steps: dict[str, list[dict[str, object]]]) -> None:
    for summary in steps["summary"]:
        if int(summary["arrived"]) <= 0 or int(summary["passes"]) <= 0:
            fail("no change arrived or no pass ran")
        if summary["start"] == "cold" and float(summary["caught_up_ms"]) <= 0:
            fail("a cold start never caught up")
        print(json.dumps(summary))


if __name__ == "__main__":
    main()
