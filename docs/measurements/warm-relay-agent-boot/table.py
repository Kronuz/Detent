"""Tabulate the rows' SUMMARY lines: per size and arm, median and range of each measure.

usage: table.py JOB_ID...   (reads ~/.scratch/campaign/jobs/JOB/a*/out/summary.jsonl; a1 is the first attempt)
Prints a Markdown table per measure group and one line per run.
"""

from __future__ import annotations

import json
import re
import statistics
import sys
from pathlib import Path

JOBS = Path.home() / ".scratch/campaign/jobs"
FLUSHES = re.compile(
    r"publication synced (\d+) files in (\d+) groups, (\d+) directory syncs, (\d+) device flushes; "
    r"(\d+) device flushes since start"
)


def runs(job: str) -> list[dict[str, object]]:
    found: list[dict[str, object]] = []
    for attempt in sorted((JOBS / job).glob("a*/out/summary.jsonl")):
        for line in attempt.read_text().splitlines():
            summary = json.loads(line.removeprefix("SUMMARY "))
            summary["job"] = job
            counters = FLUSHES.search(str(summary["agent_publication"]))
            if counters is None:
                raise ValueError(f"{job}: no publication counters")
            (
                summary["files"],
                summary["groups"],
                summary["directory_syncs"],
                summary["device_flushes"],
                summary["device_flushes_total"],
            ) = map(int, counters.groups())
            found.append(summary)
    return found


def cell(values: list[float], digits: int = 2) -> str:
    if not values:
        return ""
    low, high = min(values), max(values)
    return f"{statistics.median(values):.{digits}f} ({low:.{digits}f} to {high:.{digits}f})"


def main() -> None:
    every = [run for job in sys.argv[1:] for run in runs(job)]
    sizes = sorted({int(str(run["records"])) for run in every})
    print(
        "| Records | Arm | Runs | Agent wall, s | Agent CPU, s | To publication synced, s"
        " | Relay start to selected, s | Relay ready, s |"
    )
    print("| --- | --- | ---: | --- | --- | --- | --- | --- |")
    for size in sizes:
        for mode in ("warm", "cold"):
            group = [r for r in every if r["records"] == size and r["mode"] == mode]
            if not group:
                continue

            def values(key: str) -> list[float]:
                return [float(r[key]) for r in group if r.get(key) is not None]  # noqa: B023

            print(
                f"| {size:,} | {mode} | {len(group)} | {cell(values('agent_selected_s'))}"
                f" | {cell(values('agent_cpu_s'))} | {cell(values('agent_published_s'))}"
                f" | {cell(values('relay_start_to_selected_s'))} | {cell(values('relay_ready_s'))} |"
            )
    print()
    print(
        "| Records | Arm | Files | Groups | Device flushes (publication, since start)"
        " | Agent bytes in | Agent bytes out | Relay bytes out (serving) | Relay CPU during agent, s"
        " | Agent RSS peak, MiB |"
    )
    print("| --- | --- | ---: | ---: | --- | ---: | ---: | ---: | --- | ---: |")
    for size in sizes:
        for mode in ("warm", "cold"):
            group = [r for r in every if r["records"] == size and r["mode"] == mode]
            if not group:
                continue
            same = {
                key: sorted({r[key] for r in group})
                for key in ("files", "groups", "device_flushes", "device_flushes_total")
            }
            rss = (
                statistics.median(float(str(r["agent_rss_peak_kib"])) for r in group)
                / 1024
            )
            print(
                f"| {size:,} | {mode} | {same['files']} | {same['groups']}"
                f" | {same['device_flushes']}, {same['device_flushes_total']}"
                f" | {cell([float(str(r['agent_bytes_in'])) for r in group], 0)}"
                f" | {cell([float(str(r['agent_bytes_out'])) for r in group], 0)}"
                f" | {cell([float(str(r['relay_serve_bytes_out'])) for r in group], 0)}"
                f" | {cell([float(str(r['relay_cpu_during_agent_s'])) for r in group])}"
                f" | {rss:.0f} |"
            )
    print()
    for run in every:
        print(
            f"{run['job']} {run['records']} {run['mode']} selected={run['agent_selected_s']:.3f}"
            f" published={run['agent_published_s']:.3f} cpu={run['agent_cpu_s']}"
            f" total={run['relay_start_to_selected_s']:.3f} relay_ready={run['relay_ready_s']}"
            f" settled={run['relay_settled']} revision={run['revision']}"
        )


if __name__ == "__main__":
    main()
