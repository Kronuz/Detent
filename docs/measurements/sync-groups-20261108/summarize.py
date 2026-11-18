"""Summarize one coldboot run directory: marks, the agent's "publication synced" line, and the relay
status JSON's sync_* and publication_* fields.

usage: summarize.py RUN_DIR [--require-counters]
With --require-counters it exits 1 unless the agent log line and every relay field are present.
"""

from __future__ import annotations

import json
import re
import statistics
import sys
from pathlib import Path

LINE = re.compile(r"detent-agent: (publication synced .*)")
FIELDS = (
    "publication_device_flushes",
    "publication_directory_syncs",
    "publication_files",
    "publication_groups",
    "startup_device_flushes",
    "sync_device_flushes",
    "sync_directory_syncs",
    "sync_files",
    "sync_groups",
)


def main() -> int:
    run = Path(sys.argv[1])
    marks = json.loads((run / "marks.json").read_text())
    lines: list[str] = []
    relay: dict[str, int] = {}
    for raw in (run / "events.jsonl").read_text().splitlines():
        if not raw.strip():
            continue
        event = json.loads(raw)
        if event.get("kind") == "actor_output":
            found = LINE.search(event.get("text", ""))
            if found and event.get("name") == "agent-0":
                lines.append(found.group(1).strip())
        elif event.get("kind") == "relay_health" and event.get("phase") == "end":
            for bundle in event["snapshot"].get("bundles", []):
                relay = {name: bundle[name] for name in FIELDS if name in bundle}
    restarts = marks.get("restart_seconds") or []
    summary = {
        "result": marks.get("result"),
        "records": marks["configuration"]["records"],
        "stage": marks["configuration"]["stage"],
        "authority_ready_s": marks.get("authority_ready"),
        "agent_selected_s": marks.get("agent_selected"),
        "restart_seconds": restarts,
        "restart_median_s": statistics.median(restarts) if restarts else None,
        "total_s": marks.get("total_seconds"),
        "agent_log_line": sorted(set(lines)),
        "relay_status": relay,
    }
    print("SUMMARY " + json.dumps(summary, sort_keys=True))
    if "--require-counters" in sys.argv[2:]:
        missing = [name for name in FIELDS if name not in relay]
        if marks.get("result") != "passed" or not lines or missing:
            print(
                f"counters missing: line={bool(lines)} fields={missing}",
                file=sys.stderr,
            )
            return 1
        if not relay["publication_files"] or not relay["publication_device_flushes"]:
            print("counters are zero", file=sys.stderr)
            return 1
    return 0


sys.exit(main())
