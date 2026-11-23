"""Tabulate coldboot pair marks from recovery-s2/coldboot (runs started after the r4 fixture fix)."""

from __future__ import annotations

import json
import re
import statistics
import sys
from pathlib import Path

ROOT = Path("/Users/kronuz/code/Kronuz/Detent/.claude/worktrees/agent-af2c08dd24001ec58/.scratch/recovery-s2/coldboot")
NAME = re.compile(r"(screen|aa|confirm)-(\d+)-r(\d+)-(\d)-(before|after)-(\d{8}T\d{6}Z)$")
SINCE = sys.argv[1] if len(sys.argv) > 1 else "20261107T023700Z"


def main() -> None:
    rows: list[tuple[int, str, int, int, str, dict[str, object]]] = []
    for path in ROOT.iterdir():
        match = NAME.match(path.name)
        if not match or match[6] < SINCE:
            continue
        marks_path = path / "marks.json"
        marks: dict[str, object] = json.loads(marks_path.read_text()) if marks_path.exists() else {"result": "missing"}
        rows.append((int(match[2]), match[1], int(match[3]), int(match[4]), match[5], marks))
    order = {"screen": 0, "aa": 1, "confirm": 2}
    rows.sort(key=lambda r: (r[0], order[r[1]], r[2], r[3]))
    print("| records | stage | round | arm | result | authority ready s | agent selected s | restart median s | total s |")
    print("|---|---|---|---|---|---|---|---|---|")
    for records, stage, rnd, seq, arm, m in rows:
        restarts = m.get("restart_seconds") or []
        median = f"{statistics.median(restarts):.3f}" if isinstance(restarts, list) and restarts else "-"

        def num(key: str) -> str:
            value = m.get(key)
            return f"{value:.2f}" if isinstance(value, (int, float)) else "-"

        print(f"| {records} | {stage} | {rnd} | {seq}-{arm} | {m.get('result')} | {num('authority_ready')} | {num('agent_selected')} | {median} | {num('total_seconds')} |")


main()
