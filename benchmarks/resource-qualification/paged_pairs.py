from __future__ import annotations

import hashlib
import json
import os
import platform
import re
import subprocess
import sys
from datetime import UTC, datetime
from pathlib import Path


def main() -> None:
    before, after, output = (Path(arg).resolve() for arg in sys.argv[1:])
    output.mkdir(parents=True, exist_ok=False)
    samples: list[dict[str, object]] = []
    report: dict[str, object] = {
        "status": "incomplete",
        "started_utc": datetime.now(UTC).isoformat(),
        "platform": platform.platform(),
        "method": "Three alternating before/after pairs per paged recovery/collection workload; 100000 keys; quiet exclusive; executable-owned state/value/conservation checks; macOS time -l whole-process maximum RSS bytes.",
        "samples": samples,
    }
    environment = {
        key: value
        for key, value in os.environ.items()
        if not key.startswith(("DETENT_", "DYLD_")) and key != "LD_PRELOAD"
    }
    environment["LC_ALL"] = "C"
    try:
        for workload in ("recovery", "collection"):
            for pair in range(3):
                for arm in (
                    ("before", "after") if pair % 2 == 0 else ("after", "before")
                ):
                    binary = (
                        before if arm == "before" else after
                    ) / f"detent_paged_{workload}_bench"
                    work = output / f"{workload}-{pair}-{arm}"
                    work.mkdir()
                    completed = subprocess.run(
                        ["/usr/bin/time", "-l", str(binary), "100000"],
                        cwd=work,
                        env=environment,
                        check=True,
                        capture_output=True,
                        text=True,
                        timeout=120,
                    )
                    rows: list[dict[str, object]] = [
                        json.loads(line)
                        for line in completed.stdout.splitlines()
                        if line.startswith("{")
                    ]
                    if not rows or any(row.get("records") != 100000 for row in rows):
                        raise RuntimeError("Missing or mismatched cardinality results")
                    rss = re.search(
                        r"^\s*(\d+)\s+maximum resident set size\s*$",
                        completed.stderr,
                        re.MULTILINE,
                    )
                    if not rss:
                        raise RuntimeError("Missing maximum RSS")
                    samples.append(
                        {
                            "workload": workload,
                            "pair": pair,
                            "arm": arm,
                            "binary_sha256": hashlib.sha256(
                                binary.read_bytes()
                            ).hexdigest(),
                            "results": rows,
                            "maximum_rss_bytes": int(rss[1]),
                        }
                    )
                    (work / "stdout.txt").write_text(completed.stdout)
                    (work / "stderr.txt").write_text(completed.stderr)
        report["status"] = "passed"
    except BaseException as error:
        report.update(status="failed", error=repr(error))
        raise
    finally:
        (output / "result.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
