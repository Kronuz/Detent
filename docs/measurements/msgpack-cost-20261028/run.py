from __future__ import annotations

import hashlib
import json
import os
import subprocess
from datetime import UTC, datetime
from pathlib import Path

root = Path(__file__).resolve().parent
output = root / "paired.json"
if output.exists():
    raise ValueError("Refuse to replace a cost batch")
environment = {
    key: value
    for key, value in os.environ.items()
    if not key.startswith(("DETENT_", "DYLD_", "Malloc")) and key != "LD_PRELOAD"
}
environment["LC_ALL"] = "C"
rows: list[dict[str, object]] = []
report: dict[str, object] = {
    "status": "incomplete",
    "started_utc": datetime.now(UTC).isoformat(),
    "method": "Five alternating plain pairs per snapshot/delta × 34/200-byte key corpus × 37/4096-byte input fragments, 50 repeats/process. One additional observed pair per case, two repeats/process. Each process has its own matching warmup. Plain CPU is acceptance data; observed allocation events and per-step wall maxima include instrumentation overhead. Dedicated child accounts must fully refund. Encode verifies each output chunk against the canonical input; no frame compression or transport is timed.",
    "sources": {
        name: hashlib.sha256((root / name).read_bytes()).hexdigest()
        for name in ("production.cc", "observer.hh", "build.py", "run.py")
    },
    "binaries": {
        arm: hashlib.sha256((root / f"production-{arm}").read_bytes()).hexdigest()
        for arm in ("before", "after")
    },
    "samples": rows,
}
try:
    index = 0
    for width in (34, 200):
        for kind, file in (("snapshot", "0.wire"), ("delta", "2.wire")):
            corpus = root.parent / f"wire-before-{width}" / file
            corpus_hash = hashlib.sha256(corpus.read_bytes()).hexdigest()
            for fragment in (37, 4096):
                for mode, pairs, repeats in (("plain", 5, 50), ("observed", 1, 2)):
                    for pair in range(pairs):
                        arms = (
                            ("before", "after")
                            if (index + pair) % 2 == 0
                            else ("after", "before")
                        )
                        for arm in arms:
                            command = [
                                str(root / f"production-{arm}"),
                                kind,
                                str(corpus),
                                str(repeats),
                                str(fragment),
                                mode,
                            ]
                            warm = command.copy()
                            warm[3] = "2"
                            subprocess.run(
                                warm,
                                env=environment,
                                check=True,
                                capture_output=True,
                                timeout=120,
                            )
                            result = subprocess.run(
                                command,
                                env=environment,
                                check=False,
                                capture_output=True,
                                text=True,
                                timeout=120,
                            )
                            if result.returncode:
                                report["failed_process"] = {
                                    "command": command,
                                    "returncode": result.returncode,
                                    "stdout": result.stdout,
                                    "stderr": result.stderr,
                                }
                                result.check_returncode()
                            value: dict[str, object] = json.loads(result.stdout)
                            if (
                                value["logical_bytes"] != corpus.stat().st_size
                                or value["repeats"] != repeats
                                or value["observed"] != (mode == "observed")
                            ):
                                raise RuntimeError("Cost fixture metadata differs")
                            value.update(
                                arm=arm,
                                pair=pair,
                                kind=kind,
                                key_bytes=width,
                                corpus_sha256=corpus_hash,
                            )
                            rows.append(value)
                index += 1
    report["status"] = "passed"
except BaseException as error:
    report.update(status="failed", error=repr(error))
    raise
finally:
    report["finished_utc"] = datetime.now(UTC).isoformat()
    output.write_text(json.dumps(report, indent=2) + "\n")
