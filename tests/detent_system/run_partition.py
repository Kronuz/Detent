"""Keep old-leader HTTPS reachable while isolating every peer link."""

from __future__ import annotations

import argparse
import asyncio
import time
from pathlib import Path

from authority import Authority
from evidence import build_context, cache_dependencies, file_sha256, host, repository
from managed import ManagedCohort
from network import PeerMesh
from run_writers import drained
from runtime import Actors, Evidence, Scenario
from writers import Writers


async def run(arguments: argparse.Namespace) -> None:
    scenario = Scenario.load(arguments.scenario)
    preflight = scenario.preflight(arguments.output)
    arguments.output.mkdir(parents=True, mode=0o700, exist_ok=False)
    evidence = Evidence(arguments.output / "events.jsonl", scenario.artifact_bytes)
    try:
        evidence.record(
            "metadata",
            host=host(),
            preflight=preflight,
            source=repository(Path(__file__).resolve().parents[2]),
            build=build_context(arguments.build, arguments.output),
            dependencies=cache_dependencies(arguments.build),
            binary_sha256=file_sha256(arguments.binary),
            seed=17,
            qualification="reachable-isolated-leader",
        )
        async with Actors(evidence, scenario.shutdown_seconds) as actors:
            authority = Authority(
                arguments.binary.resolve(),
                arguments.certificates.resolve(),
                arguments.output.resolve(),
                actors,
                scenario.producers,
            )
            mesh = PeerMesh()
            try:
                await mesh.start(arguments.output, authority.ports)
                await authority.start()
                writers = Writers(authority, scenario.records, scenario.producers, 17)
                await writers.phase("prepare")
                cohort = ManagedCohort(
                    authority,
                    actors,
                    arguments.output.resolve(),
                    arguments.agents.resolve(),
                    arguments.local_oracle.resolve(),
                    arguments.certificates.resolve(),
                    scenario.durable_agents,
                    scenario.producers,
                )
                await cohort.start()
                revision = max(
                    int(receipt["revision"])
                    for receipt in writers.receipts
                    if receipt is not None
                )
                await cohort.wait_selected(revision)
                old = await authority.leader()
                deadline = time.monotonic() + 10
                while True:
                    code, health = await authority.request(old, "/v1/health")
                    if (
                        code == 200
                        and health.get("subscription_idle_responses", 0)
                        >= scenario.durable_agents
                    ):
                        break
                    if time.monotonic() >= deadline:
                        raise RuntimeError(
                            "managed subscriptions did not reach idle before partition"
                        )
                    await asyncio.sleep(0.05)
                idle_before = int(health["subscription_idle_responses"])
                evidence.record(
                    "partition_requested", old_leader=old, idle_before=idle_before
                )
                authority.excluded.add(old)
                await mesh.isolate(old)
                new = await authority.leader()
                if old == new:
                    raise RuntimeError(
                        "isolated member remained the fixture writer route"
                    )
                code, old_health = await authority.request(old, "/v1/health")
                if (
                    code != 200
                    or old_health.get("role") != 2
                    or old_health.get("fenced")
                ):
                    raise RuntimeError(
                        "fixture did not preserve the reachable stale leader"
                    )
                evidence.record(
                    "majority_elected",
                    old_leader=old,
                    new_leader=new,
                    old_https_role=old_health["role"],
                )
                await writers.phase("mutate", time.monotonic() + 5)
                await writers.phase("verify")
                await cohort.verify(writers)
                code, old_health = await authority.request(old, "/v1/health")
                if code != 200 or old_health.get("role") != 2:
                    raise RuntimeError(
                        "old leader did not remain reachable through verification"
                    )
                evidence.record(
                    "partition_verified",
                    old_idle_responses=old_health["subscription_idle_responses"],
                    counts=dict(writers.counts),
                )
                await cohort.close()
                mesh.heal(old)
                authority.excluded.remove(old)
                await drained(authority, evidence)
                children = list(actors.children)
            finally:
                await mesh.close()
                authority.release_ports()
        if any(actor.process.returncode != 0 for actor in children):
            raise RuntimeError("partition actors did not drain cleanly")
        evidence.record("completed", dropped_output_records=evidence.dropped)
    finally:
        evidence.close()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in (
        "scenario",
        "binary",
        "build",
        "agents",
        "local-oracle",
        "certificates",
        "output",
    ):
        parser.add_argument(f"--{name}", type=Path, required=True)
    asyncio.run(run(parser.parse_args()))


if __name__ == "__main__":
    main()
