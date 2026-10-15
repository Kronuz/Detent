"""Verify durable local reads through an unavailable authority or lost quorum."""

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
            kind_of_outage=arguments.kind,
            host=host(),
            preflight=preflight,
            source=repository(Path(__file__).resolve().parents[2]),
            build=build_context(arguments.build, arguments.output),
            dependencies=cache_dependencies(arguments.build),
            binary_sha256=file_sha256(arguments.binary),
            seed=17,
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
                if arguments.kind == "no-quorum":
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
                await cohort.verify(writers)
                selections = [
                    (
                        arguments.output / f"agent-{index}/generations/selected"
                    ).read_bytes()
                    for index in range(scenario.durable_agents)
                ]
                if arguments.kind == "all-down":
                    for index in range(3):
                        await authority.kill_member(index)
                else:
                    for index in range(3):
                        await mesh.isolate(index)
                evidence.record("outage_started")
                await asyncio.sleep(5)
                await cohort.verify(writers)
                for index, selection in enumerate(selections):
                    if (
                        arguments.output / f"agent-{index}/generations/selected"
                    ).read_bytes() != selection:
                        raise RuntimeError(
                            "outage changed the actual durable selection"
                        )
                evidence.record("outage_local_reads_verified")
                if arguments.kind == "all-down":
                    for index in range(3):
                        await authority.restart_member(index)
                else:
                    for index in range(3):
                        mesh.heal(index)
                await authority.leader()
                await writers.phase("mutate", time.monotonic() + 3)
                await writers.phase("verify")
                await cohort.verify(writers)
                await cohort.close()
                await drained(authority, evidence)
                children = list(actors.children)
            finally:
                await mesh.close()
                authority.release_ports()
        if any(actor.process.returncode != 0 for actor in children):
            raise RuntimeError("outage actors did not drain cleanly")
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
    parser.add_argument("--kind", choices=("all-down", "no-quorum"), required=True)
    asyncio.run(run(parser.parse_args()))


if __name__ == "__main__":
    main()
