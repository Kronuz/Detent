"""Compare frozen distributor executables under a strict quiet reservation."""

from __future__ import annotations

import hashlib
import json
import os
import re
import statistics
import subprocess
import sys
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "benchmarks"))
from logging_compare import check_interference


def main() -> None:
    before, after, output = (Path(value).resolve() for value in sys.argv[1:4])
    output.mkdir(parents=True, exist_ok=False)
    runtime = output / "runtime"
    runtime.mkdir()
    samples: list[dict[str, Any]] = []
    for mode in ("PUT", "PATCH"):
        for pair in range(5):
            arms = [("before", before), ("after", after)]
            if pair % 2:
                arms.reverse()
            for arm, binary in arms:
                label = f"{mode}-{pair}-{arm}"
                check_interference(output / f"{label}-processes-before.txt")
                result = subprocess.run(
                    [
                        "/usr/bin/time",
                        "-l",
                        str(binary),
                        "100000",
                        mode,
                        "3",
                        str(runtime / label),
                    ],
                    capture_output=True,
                    text=True,
                    check=True,
                    timeout=180,
                    env=dict(os.environ, TMPDIR=str(runtime)),
                )
                (output / f"{label}.stdout").write_text(result.stdout)
                (output / f"{label}.stderr").write_text(result.stderr)
                check_interference(output / f"{label}-processes-after.txt")
                row: dict[str, Any] = json.loads(result.stdout)
                rss = re.search(
                    r"^\s*(\d+)\s+maximum resident set size",
                    result.stderr,
                    re.MULTILINE,
                )
                if rss is None:
                    raise ValueError("missing macOS peak RSS")
                row.update(arm=arm, pair=pair, peak_rss_bytes=int(rss.group(1)))
                samples.append(row)
                print(json.dumps(row), flush=True)
                (output / "samples.json").write_text(
                    json.dumps(samples, indent=2) + "\n"
                )
    report = {
        "before_sha256": hashlib.sha256(before.read_bytes()).hexdigest(),
        "after_sha256": hashlib.sha256(after.read_bytes()).hexdigest(),
        "median": {
            mode: {
                arm: {
                    metric: statistics.median(
                        float(row[metric])
                        for row in samples
                        if row["mode"] == mode and row["arm"] == arm
                    )
                    for metric in (
                        "seconds",
                        "allocations",
                        "allocated_bytes",
                        "peak_rss_bytes",
                    )
                }
                for arm in ("before", "after")
            }
            for mode in ("PUT", "PATCH")
        },
        "samples": samples,
        "limits": [
            "Five alternating pairs; 100k knobs, three overrides each, three measured publications after initial seeding.",
            "PUT replaces the full snapshot; PATCH replaces one knob; both use actual distributor serialization, compression and file persistence.",
            "Publication wall seconds exclude initial seeding and final destruction. Peak RSS covers the whole process, not retained container bytes.",
            "Allocation counts cover the benchmark's main-thread ordinary operator new calls during publication; background or over-aligned allocations are not metered.",
            "Desktop activity remains recorded; timing is exploratory. This does not measure network, fsync durability, fleet capacity or release readiness.",
        ],
    }
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
