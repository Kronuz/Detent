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
    directories = {"before": before, "after": after}
    environment = {
        key: value
        for key, value in os.environ.items()
        if not key.startswith(("DETENT_", "DYLD_")) and key != "LD_PRELOAD"
    }
    environment["LC_ALL"] = "C"
    records: list[dict[str, object]] = []
    report: dict[str, object] = {
        "status": "incomplete",
        "started_utc": datetime.now(UTC).isoformat(),
        "platform": platform.platform(),
        "method": "Five alternating before/after pairs for each identical workload, under quiet exclusive. macOS time -l maximum RSS is recorded in bytes. Account heap uses malloc zone live-allocation statistics outside the timed loops; retirement reports final charges after domain/runtime release.",
        "samples": records,
    }
    try:
        for workload in ("ownership", "retirement"):
            for pair in range(5):
                order = ("before", "after") if pair % 2 == 0 else ("after", "before")
                for arm in order:
                    binary = directories[arm] / f"detent_resource_{workload}_bench"
                    completed = subprocess.run(
                        ["/usr/bin/time", "-l", str(binary)],
                        check=True,
                        capture_output=True,
                        text=True,
                        env=environment,
                        timeout=60,
                    )
                    text = completed.stdout
                    if workload == "ownership":
                        if (
                            "residual_blocks=0 residual_bytes=0" not in text
                            or not re.search(r" used=0 ", text)
                        ):
                            raise RuntimeError(
                                "Account allocations or admitted charges remain"
                            )
                    elif "payload_remaining=0 controls_remaining=0" not in text:
                        raise RuntimeError("Retirement payload/control charges remain")
                    rss = re.search(
                        r"^\s*(\d+)\s+maximum resident set size\s*$",
                        completed.stderr,
                        re.MULTILINE,
                    )
                    if not rss:
                        raise RuntimeError("Missing maximum RSS")
                    records.append(
                        {
                            "workload": workload,
                            "pair": pair,
                            "arm": arm,
                            "binary_sha256": hashlib.sha256(
                                binary.read_bytes()
                            ).hexdigest(),
                            "stdout": text,
                            "maximum_rss_bytes": int(rss[1]),
                        }
                    )
                    (output / f"{workload}-{pair}-{arm}.txt").write_text(
                        text + completed.stderr
                    )
        report["status"] = "passed"
    except BaseException as error:
        report.update(status="failed", error=repr(error))
        raise
    finally:
        (output / "result.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
