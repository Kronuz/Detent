"""Print rows-file lines for jobs: each run's SUMMARY plus its job and the relay's RSS from marks.json.

usage: rows.py JOB... > rows-COMMIT.jsonl
The relay's RSS before the agent is the last relay sample at t <= 0.1 s; the peak is the maximum.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

JOBS = Path.home() / ".scratch/campaign/jobs"


def rows(job: str) -> list[dict[str, object]]:
    found: list[dict[str, object]] = []
    for out in sorted(JOBS.glob(f"{job}/a*/out")):
        (marks,) = out.glob("*/marks.json")
        trace = json.loads(marks.read_text())["trace"]
        relay = [(t, rss) for t, who, _cpu, rss in trace if who == "relay"]
        before = [rss for t, rss in relay if t <= 0.1]
        for line in (out / "summary.jsonl").read_text().splitlines():
            summary = json.loads(line.removeprefix("SUMMARY "))
            summary["job"] = job
            summary["relay_rss_before_kib"] = before[-1] if before else None
            summary["relay_rss_peak_kib"] = max(rss for _t, rss in relay)
            found.append(summary)
    return found


if __name__ == "__main__":
    for job in sys.argv[1:]:
        for row in rows(job):
            print(json.dumps(row, sort_keys=True))
