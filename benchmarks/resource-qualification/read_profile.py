from __future__ import annotations

import hashlib
import json
import os
import platform
import re
import statistics
import subprocess
import sys
from datetime import UTC, datetime
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    probe, directory, source, output = (Path(arg).resolve() for arg in sys.argv[1:])
    if output.exists() or platform.system() != "Darwin":
        raise ValueError("Use a fresh output path on macOS")
    subprocess.run(
        [
            "git",
            "-C",
            str(source),
            "diff",
            "HEAD",
            "--exit-code",
            "--",
            "src",
            "include",
            "tests",
        ],
        check=True,
    )
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
    report: dict[str, object] = {
        "status": "incomplete",
        "started_utc": datetime.now(UTC).isoformat(),
        "source": "Detent:"
        + subprocess.check_output(
            ["git", "-C", str(source), "rev-parse", "HEAD"], text=True
        ).strip(),
        "probe_sha256": digest(probe),
        "controller_sha256": digest(Path(__file__)),
        "selected_sha256": digest(directory / "selected"),
        "platform": platform.platform(),
        "method": "40 saved integer keys, five rounds. A separate warmup process precedes each fresh SDK process. SDK first-read timing excludes executable launch. Steady means use 100000 reads after 1000 warmups. macOS time -l reports whole-process maximum RSS in bytes, including steady reads. Sequential profile; not a paired causal comparison.",
        "samples": samples,
    }
    try:
        for round_index in range(5):
            for key, expected in cases:
                command = [str(probe), "--read", str(directory), key, str(expected)]
                subprocess.run(
                    command,
                    check=True,
                    capture_output=True,
                    env=environment,
                    timeout=30,
                )
                completed = subprocess.run(
                    ["/usr/bin/time", "-l", *command, "100000"],
                    check=True,
                    capture_output=True,
                    text=True,
                    env=environment,
                    timeout=30,
                )
                sample: dict[str, str | int | float] = json.loads(completed.stdout)
                if sample["records"] != 5000000:
                    raise RuntimeError("Corpus cardinality differs")
                rss = re.search(
                    r"^\s*(\d+)\s+maximum resident set size\s*$",
                    completed.stderr,
                    re.MULTILINE,
                )
                if not rss:
                    raise RuntimeError("Missing macOS maximum RSS")
                sample.update(
                    round=round_index,
                    key=key,
                    expected_int=expected,
                    maximum_rss_bytes=int(rss[1]),
                )
                samples.append(sample)
        if digest(directory / "selected") != report["selected_sha256"]:
            raise RuntimeError("Selection changed during measurement")
        first = sorted(int(row["first_live_ns"]) for row in samples)
        report["summary"] = {
            "count": len(samples),
            "first_read_median_ns": statistics.median(first),
            "first_read_p99_ns": first[(99 * len(first) + 99) // 100 - 1],
            "first_read_max_ns": max(first),
            "below_1ms": sum(value < 1000000 for value in first),
            "steady_mean_median_ns": statistics.median(
                float(row["steady_mean_ns"]) for row in samples
            ),
            "maximum_rss_median_bytes": statistics.median(
                int(row["maximum_rss_bytes"]) for row in samples
            ),
            "maximum_rss_max_bytes": max(
                int(row["maximum_rss_bytes"]) for row in samples
            ),
            "major_faults": sum(int(row["major_faults"]) for row in samples),
        }
        report["status"] = "passed"
    except BaseException as error:
        report.update(status="failed", error=repr(error))
        raise
    finally:
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
