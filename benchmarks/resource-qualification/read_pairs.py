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

REPO = Path(__file__).resolve().parents[2]


def main() -> None:
    before, after, directory, output = (Path(arg).resolve() for arg in sys.argv[1:])
    if output.exists():
        raise ValueError("Use a fresh output path")
    manifest: dict[str, list[dict[str, str | int]]] = json.loads(
        (REPO / "docs/measurements/prepared-managed-5m-20261026.json").read_text()
    )
    cases = [(str(row["key"]), int(row["expected_int"])) for row in manifest["cases"]]
    environment = {
        key: value
        for key, value in os.environ.items()
        if not key.startswith(("DETENT_", "DYLD_")) and key != "LD_PRELOAD"
    }
    environment["LC_ALL"] = "C"
    samples: list[dict[str, str | int | float]] = []
    selected = hashlib.sha256((directory / "selected").read_bytes()).hexdigest()
    report: dict[str, object] = {
        "status": "incomplete",
        "started_utc": datetime.now(UTC).isoformat(),
        "platform": platform.platform(),
        "method": "40 real integer keys, five rounds; alternating before/after per key and round; each arm gets a separate warmup process immediately before its measured fresh process. Identical 100000 steady reads after first-read timing. macOS time -l whole-process peak RSS bytes. SDK timing excludes executable launch.",
        "binaries": {
            "before": hashlib.sha256(before.read_bytes()).hexdigest(),
            "after": hashlib.sha256(after.read_bytes()).hexdigest(),
        },
        "selected_sha256": selected,
        "samples": samples,
    }
    try:
        for round_index in range(5):
            for index, (key, expected) in enumerate(cases):
                order = (
                    ("before", "after")
                    if (round_index + index) % 2 == 0
                    else ("after", "before")
                )
                for arm in order:
                    command = [
                        str(before if arm == "before" else after),
                        "--read",
                        str(directory),
                        key,
                        str(expected),
                    ]
                    subprocess.run(
                        command,
                        check=True,
                        capture_output=True,
                        env=environment,
                        timeout=30,
                    )
                    result = subprocess.run(
                        ["/usr/bin/time", "-l", *command, "100000"],
                        check=True,
                        capture_output=True,
                        text=True,
                        env=environment,
                        timeout=30,
                    )
                    sample: dict[str, str | int | float] = json.loads(result.stdout)
                    rss = re.search(
                        r"^\s*(\d+)\s+maximum resident set size\s*$",
                        result.stderr,
                        re.MULTILINE,
                    )
                    if sample["records"] != 5000000 or not rss:
                        raise RuntimeError("Invalid cardinality or missing RSS")
                    sample.update(
                        arm=arm,
                        round=round_index,
                        key=key,
                        expected_int=expected,
                        maximum_rss_bytes=int(rss[1]),
                    )
                    samples.append(sample)
        if (
            hashlib.sha256((directory / "selected").read_bytes()).hexdigest()
            != selected
        ):
            raise RuntimeError("Selected generation changed")
        report["status"] = "passed"
    except BaseException as error:
        report.update(status="failed", error=repr(error))
        raise
    finally:
        output.write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
