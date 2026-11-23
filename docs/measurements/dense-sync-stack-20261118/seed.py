"""Seed an authority fixture: three voters holding a capacity scenario's records, then stop.

usage: seed.py TREE BUILD SCENARIO CAPACITY OUTPUT

The seeding half of tests/detent_system/run_fleet.py (flat writers, capacity profile, `prepare`
then `verify`) without the agent, relay and fleet phases; OUTPUT keeps state-0..2, which
coldboot.py clones. Uses the harness of TREE, the checkout BUILD was built from.
"""

from __future__ import annotations

import asyncio
import json
import subprocess
import sys
import time
from pathlib import Path


async def run(tree: Path, build: Path, scenario_path: Path, capacity_path: Path, output: Path) -> None:
    sys.path.insert(0, str(tree / "tests/detent_system"))
    from authority import Authority
    from capacity import CapacityProfile
    from http_activity import configure_workers
    from runtime import Actors, Evidence, Scenario
    from writers import Writers

    capacity = CapacityProfile.load(capacity_path)
    scenario = Scenario.load(
        scenario_path, min(capacity.application["keys"], capacity.image["records"])
    )
    workers = configure_workers(scenario.producers, None)
    output.mkdir(parents=True, mode=0o700, exist_ok=False)
    evidence = Evidence(output / "events.jsonl", scenario.artifact_bytes)
    try:
        async with Actors(evidence, 60) as actors:
            authority = Authority(
                build / "src/detentd",
                build / "tests/detent_authority/tls-fixture",
                output,
                actors,
                scenario.producers,
                http_workers=workers,
                capacity=capacity,
            )
            await authority.start()
            writers = Writers(authority, scenario.records, scenario.producers, 17)
            began = time.monotonic()
            await writers.phase("prepare")
            seconds = time.monotonic() - began
            await writers.phase("verify")
            revision = max(int(r["revision"]) for r in writers.receipts if r is not None)
            commit = subprocess.run(
                ["git", "-C", str(tree), "rev-parse", "HEAD"], capture_output=True, text=True
            ).stdout.strip()
            (output / "seeded.json").write_text(
                json.dumps(
                    {
                        "records": scenario.records,
                        "revision": revision,
                        "seed_seconds": seconds,
                        "tree": commit,
                    },
                    indent=1,
                )
            )
            print(json.dumps({"seeded": scenario.records, "revision": revision, "seconds": seconds}))
    finally:
        evidence.close()


if __name__ == "__main__":
    tree, build, scenario, capacity, output = (Path(a).resolve() for a in sys.argv[1:6])
    asyncio.run(run(tree, build, scenario, capacity, output))
