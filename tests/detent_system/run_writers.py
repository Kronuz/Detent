"""Run sustained writers against three real authority processes."""

from __future__ import annotations

import argparse
import asyncio
import hashlib
import http.client
import time
from pathlib import Path

from authority import Authority
from evidence import build_context, cache_dependencies, file_sha256, host, repository
from runtime import Actors, Evidence, Scenario
from writers import Writers


async def sample(authority: Authority, evidence: Evidence, stop: asyncio.Event) -> None:
    while not stop.is_set():
        try:
            status, health = await authority.request(
                authority.leader_index, "/v1/health"
            )
        except (OSError, http.client.HTTPException):
            evidence.record("health_unavailable", monotonic_seconds=time.monotonic())
            await authority.leader()
            continue
        if status != 200:
            raise RuntimeError("authority health unavailable during sustained load")
        evidence.record("health", monotonic_seconds=time.monotonic(), snapshot=health)
        try:
            await asyncio.wait_for(stop.wait(), 1)
        except TimeoutError:
            pass


async def kill_leader(authority: Authority, evidence: Evidence, after: float) -> None:
    await asyncio.sleep(after)
    index = await authority.leader()
    evidence.record(
        "leader_kill_requested", index=index, monotonic_seconds=time.monotonic()
    )
    await authority.kill_member(index)
    new_leader = await authority.leader()
    if index == new_leader:
        raise RuntimeError("killed member still reports leadership")
    evidence.record(
        "leader_replaced", index=new_leader, monotonic_seconds=time.monotonic()
    )
    await authority.restart_member(index)


async def drained(authority: Authority, evidence: Evidence) -> None:
    keys = (
        "requests",
        "reserved_body_bytes",
        "reserved_reply_bytes",
        "connections_active",
        "retired_log_bytes",
        "retired_log_entries",
    )
    deadline = time.monotonic() + 10
    while True:
        status, health = await authority.request(authority.leader_index, "/v1/health")
        if status == 200 and all(health.get(key) == 0 for key in keys):
            evidence.record("drained", snapshot=health)
            return
        if time.monotonic() >= deadline:
            raise RuntimeError(f"authority resources did not drain: {health}")
        await asyncio.sleep(0.2)


async def run(arguments: argparse.Namespace) -> None:
    scenario = Scenario.load(arguments.scenario)
    if arguments.duration is not None:
        if not 1 <= arguments.duration <= 86400:
            raise ValueError("duration must be 1–86400 seconds")
        duration = arguments.duration
    else:
        duration = scenario.duration
    if (
        arguments.kill_leader_at is not None
        and not 0 < arguments.kill_leader_at < duration
    ):
        raise ValueError("leader kill must occur within the sustained interval")
    if scenario.records < scenario.producers:
        raise ValueError("writer scenario needs at least one record per producer")
    if not arguments.binary.is_file():
        raise ValueError("authority binary is not a regular file")
    preflight = scenario.preflight(arguments.output)
    arguments.output.mkdir(parents=True, mode=0o700, exist_ok=False)
    evidence = Evidence(arguments.output / "events.jsonl", scenario.artifact_bytes)
    try:
        evidence.record(
            "metadata",
            scenario_sha256=hashlib.sha256(arguments.scenario.read_bytes()).hexdigest(),
            source=repository(Path(__file__).resolve().parents[2]),
            dependencies=cache_dependencies(arguments.build),
            binary_sha256=file_sha256(arguments.binary),
            host=host(),
            build=build_context(arguments.build, arguments.output),
            preflight=preflight,
            seed=arguments.seed,
            duration=duration,
            qualification="authority-writers-only",
            kill_leader_at=arguments.kill_leader_at,
        )
        async with Actors(evidence, scenario.shutdown_seconds) as actors:
            authority = Authority(
                arguments.binary.resolve(),
                arguments.certificates.resolve(),
                arguments.output.resolve(),
                actors,
                scenario.producers,
            )
            await authority.start()
            writers = Writers(
                authority,
                scenario.records,
                scenario.producers,
                arguments.seed,
            )
            began = time.monotonic()
            await writers.phase("prepare")
            evidence.record(
                "seeded",
                records=scenario.records,
                elapsed_seconds=time.monotonic() - began,
            )
            baseline = writers.counts.copy()
            began = time.monotonic()
            stop = asyncio.Event()
            async with asyncio.TaskGroup() as group:
                group.create_task(sample(authority, evidence, stop))
                if arguments.kill_leader_at is not None:
                    group.create_task(
                        kill_leader(authority, evidence, arguments.kill_leader_at)
                    )
                try:
                    await writers.phase("mutate", began + duration)
                finally:
                    stop.set()
            elapsed = time.monotonic() - began
            sustained = writers.counts - baseline
            await writers.phase("verify")
            evidence.record(
                "verified",
                complete_records=scenario.records,
                producer_receipts=scenario.producers,
                sustained_seconds=elapsed,
                sustained_counts=dict(sustained),
                total_counts=dict(writers.counts),
                command_latency_total_seconds=writers.latency_total,
                command_latency_maximum_seconds=writers.latency_maximum,
            )
            await drained(authority, evidence)
            children = list(actors.children)
        if any(actor.process.returncode != 0 for actor in children):
            raise RuntimeError("authority did not drain successfully")
        evidence.record("completed", dropped_output_records=evidence.dropped)
    finally:
        evidence.close()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scenario", type=Path, required=True)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--build", type=Path, default=Path("build"))
    parser.add_argument("--certificates", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--seed", type=int, default=17)
    parser.add_argument("--duration", type=int)
    parser.add_argument("--kill-leader-at", type=float)
    asyncio.run(run(parser.parse_args()))


if __name__ == "__main__":
    main()
