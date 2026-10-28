from __future__ import annotations

import json
import subprocess
from pathlib import Path

root = Path(__file__).resolve().parent
output = root / "paired.jsonl"
with output.open("x") as stream:
    for knobs, requests in ((100, 500), (100000, 20)):
        for coding in ("identity", "gzip", "zstd"):
            expected: tuple[str, int, int] | None = None
            for pair in range(3):
                arms = ("before", "after") if pair % 2 == 0 else ("after", "before")
                for arm in arms:
                    result = subprocess.run(
                        [
                            str(root / f"blocking-{arm}"),
                            str(knobs),
                            str(requests),
                            coding,
                        ],
                        capture_output=True,
                        text=True,
                        check=True,
                        timeout=120,
                    )
                    row = json.loads(result.stdout)
                    identity = (
                        row["payload_sha256"],
                        row["logical_bytes"],
                        row["response_wire_bytes"],
                    )
                    if expected is None:
                        expected = identity
                    if identity != expected:
                        raise RuntimeError("Before/after payload or wire bytes differ")
                    row.update(arm=arm, pair=pair)
                    stream.write(json.dumps(row) + "\n")
                    stream.flush()
