"""Run the 500k publication fixtures under finite, reproducible CPU contention."""

from __future__ import annotations

import argparse
import asyncio
import json
import sys
import time
from pathlib import Path

from evidence import file_sha256, host
from runtime import Actors, Evidence

CASES = (
    "subscription-relay-paged-migrate-500000",
    "subscription-relay-paged-transitions-500000",
)


async def run(arguments: argparse.Namespace) -> None:
    if not 1 <= arguments.workers <= 32:
        raise ValueError("CPU worker count must be 1–32")
    arguments.output.mkdir(parents=True, mode=0o700, exist_ok=False)
    evidence = Evidence(arguments.output / "events.jsonl", 4 * 1024 * 1024)
    results: list[dict[str, object]] = []
    try:
        evidence.record(
            "metadata",
            workers=arguments.workers,
            binary_sha256=file_sha256(arguments.binary),
            host=host(),
        )
        async with Actors(evidence, 10, maximum=34) as actors:
            for index in range(arguments.workers):
                await actors.start(
                    f"cpu-{index}", [sys.executable, "-c", "while True: pass"]
                )

            async def execute(case: str) -> None:
                started = time.monotonic()
                actor = await actors.start(
                    case,
                    [
                        str(arguments.binary.resolve()),
                        str(arguments.certificates.resolve()),
                        f"--{case}",
                    ],
                )
                await asyncio.wait_for(actor.process.wait(), 300)
                await actor.capture
                result: dict[str, object] = {
                    "case": case,
                    "code": actor.process.returncode,
                    "elapsed_seconds": time.monotonic() - started,
                }
                results.append(result)
                evidence.record("case_result", **result)

            async with asyncio.TaskGroup() as group:
                for case in CASES:
                    group.create_task(execute(case))
        evidence.record(
            "completed", results=results, dropped_output_records=evidence.dropped
        )
        if any(result["code"] != 0 for result in results):
            raise RuntimeError("contended publication fixture failed")
    finally:
        evidence.close()
        (arguments.output / "results.json").write_text(
            json.dumps(results, indent=2) + "\n"
        )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--certificates", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--workers", type=int, default=32)
    asyncio.run(run(parser.parse_args()))


if __name__ == "__main__":
    main()
