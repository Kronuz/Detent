"""Qualify native TLS probes beside independent durable SDK readers."""

from __future__ import annotations

import argparse
import asyncio
import hashlib
import json
import random
import time
from collections.abc import Callable
from pathlib import Path

from authority import Authority
from capacity import CapacityProfile
from evidence import build_context, cache_dependencies, file_sha256, host, repository
from fleet import (
    FleetOutput,
    admission,
    available_memory,
    load_result,
    process_cpu_seconds,
    process_resources,
    resident_bytes,
)
from http_activity import configure_workers
from managed import ManagedCohort
from mixed import MixedWriters
from pacing import paced_mutations, validate_rate
from run_writers import drained, sample
from runtime import Actors, Evidence, Scenario
from writers import Writers


async def resource_samples(
    actors: Actors,
    evidence: Evidence,
    ceiling: int,
    stop: asyncio.Event,
    cohort: Callable[[], ManagedCohort | None],
) -> None:
    while not stop.is_set():
        resources = await asyncio.to_thread(
            process_resources, actors.children.copy(), include_driver=True
        )
        driver = resources.pop("driver")
        resident = sum(int(row["resident_bytes"]) for row in resources.values())
        evidence.record(
            "fleet_resident",
            resident_bytes=resident,
            processes=resources,
            driver=driver,
            monotonic_seconds=time.monotonic(),
            sample_period_ms=200,
        )
        if resident + int(driver["resident_bytes"]) > ceiling:
            raise RuntimeError("fleet workload exceeded admitted resident ceiling")
        selected = cohort()
        if selected is not None:
            await relay_snapshot(selected, evidence, "sample")
        try:
            await asyncio.wait_for(stop.wait(), 0.2)
        except TimeoutError:
            pass


async def relay_snapshot(cohort: ManagedCohort, evidence: Evidence, phase: str) -> int:
    total = 0
    for index, port in enumerate(cohort.relay_ports):
        status, health = await cohort.authority.request(
            0, "/v1/health", endpoint_port=port
        )
        retained = health.get("sessions")
        if status != 200 or type(retained) is not int or retained < 0:
            raise RuntimeError("relay health lacks retained session accounting")
        total += retained
        evidence.record("relay_health", phase=phase, relay=index, snapshot=health)
    return total


async def relay_settled(
    cohort: ManagedCohort, evidence: Evidence, minimum: int, maximum: int
) -> None:
    deadline = time.monotonic() + 10
    while True:
        retained = await relay_snapshot(cohort, evidence, "conservation")
        if minimum <= retained <= maximum:
            return
        if time.monotonic() >= deadline:
            raise RuntimeError(f"relay session conservation failed: {retained}")
        await asyncio.sleep(0.2)


async def run(arguments: argparse.Namespace) -> None:
    capacity = (
        CapacityProfile.load(arguments.capacity_config)
        if arguments.capacity_config is not None
        else None
    )
    maximum_records = (
        min(capacity.application["keys"], capacity.image["records"])
        if capacity
        else 500000
    )
    scenario = Scenario.load(arguments.scenario, maximum_records)
    validate_rate(arguments.offer_rate)
    http_workers = configure_workers(scenario.producers, arguments.http_workers)
    duration = (
        arguments.duration if arguments.duration is not None else scenario.duration
    )
    if not 1 <= duration <= 84000 or not 1 <= arguments.active <= scenario.subscribers:
        raise ValueError("invalid fleet duration or active population")
    if not 1 <= arguments.workers <= min(16, arguments.active):
        raise ValueError("invalid fleet worker count")
    if arguments.hot_namespace and not arguments.mixed:
        raise ValueError("hot namespace contention requires mixed oracle mode")
    if scenario.records < scenario.producers:
        raise ValueError("fleet needs at least one definition per producer")
    if not 0 <= arguments.bootstrap_delay <= 60000:
        raise ValueError("bootstrap delay must be between zero and 60000 ms")
    preflight = scenario.preflight(arguments.output)
    namespace_count = 1 if arguments.hot_namespace else scenario.producers
    preliminary = admission(scenario.subscribers, arguments.active, namespace_count)
    arguments.output.mkdir(parents=True, mode=0o700, exist_ok=False)
    evidence = Evidence(arguments.output / "events.jsonl", scenario.artifact_bytes)
    try:
        evidence.record(
            "metadata",
            qualification="native-transport-probes-and-durable-cohort",
            source=repository(Path(__file__).resolve().parents[2]),
            dependencies=cache_dependencies(arguments.build),
            build=build_context(arguments.build, arguments.output),
            binaries={
                name: file_sha256(getattr(arguments, name))
                for name in ("binary", "agents", "relays", "local_oracle", "fleet")
            },
            scenario_sha256=hashlib.sha256(arguments.scenario.read_bytes()).hexdigest(),
            capacity_sha256=file_sha256(arguments.capacity_config)
            if arguments.capacity_config
            else None,
            host=host(),
            seed=arguments.seed,
            http_workers=http_workers,
            offered_workloads_per_second=arguments.offer_rate,
            duration=duration,
            transport_probes=scenario.subscribers,
            durable_stores=scenario.durable_agents,
            requested_active_exchanges=arguments.active,
            preflight=preflight,
            preliminary_memory=preliminary,
            warm_start=arguments.warm,
            compression=arguments.compression,
        )
        resource_stop = asyncio.Event()
        resource_cohort: ManagedCohort | None = None
        resource_ceiling = min(
            preliminary["workload_ceiling_bytes"], available_memory() * 4 // 5
        )
        async with Actors(evidence, 60) as actors, asyncio.TaskGroup() as monitoring:
            resources_task = monitoring.create_task(
                resource_samples(
                    actors,
                    evidence,
                    resource_ceiling,
                    resource_stop,
                    lambda: resource_cohort,
                )
            )
            authority = Authority(
                arguments.binary,
                arguments.certificates,
                arguments.output,
                actors,
                scenario.producers,
                http_workers=http_workers,
                capacity=capacity,
                shared_namespace="hot" if arguments.hot_namespace else None,
            )
            await authority.start()
            writers = (
                MixedWriters(
                    authority,
                    scenario.records,
                    scenario.producers,
                    arguments.seed,
                    shared_namespace="hot" if arguments.hot_namespace else None,
                    maximum_records=maximum_records,
                )
                if arguments.mixed
                else Writers(
                    authority, scenario.records, scenario.producers, arguments.seed
                )
            )
            seeded_at = time.monotonic()
            await writers.phase("prepare")
            evidence.record(
                "capacity_seeded",
                records=scenario.records,
                seconds=time.monotonic() - seeded_at,
                process_cpu_seconds=process_cpu_seconds(actors.children),
            )
            await writers.phase("verify")
            seed_revision = max(
                int(receipt["revision"])
                for receipt in writers.receipts
                if receipt is not None
            )
            cohort = ManagedCohort(
                authority,
                actors,
                arguments.output,
                arguments.agents,
                arguments.local_oracle,
                arguments.certificates,
                scenario.durable_agents,
                scenario.producers,
                relay=arguments.relays,
                probe_sessions=scenario.subscribers,
                probe_active=arguments.active,
                source_timeout_ms=240000,
                selection_timeout=300,
                namespaces=list(authority.namespaces),
                capacity=capacity,
                relay_count=arguments.relay_count,
            )
            bootstrap_at = time.monotonic()
            await cohort.start()
            await cohort.wait_selected(seed_revision)
            evidence.record(
                "capacity_bootstrap_selected",
                records=scenario.records,
                seconds=time.monotonic() - bootstrap_at,
            )
            await cohort.verify(writers)
            evidence.record(
                "capacity_bootstrap_verified",
                records=scenario.records,
                seconds=time.monotonic() - bootstrap_at,
            )
            resource_cohort = cohort
            if capacity is not None:
                for size in (1, 128):
                    if size > 1 and not isinstance(writers, MixedWriters):
                        continue
                    before_intents = writers.counts["sized_operation_intents"]
                    began_update = time.monotonic()
                    if isinstance(writers, MixedWriters):
                        await writers.workload(
                            0, size, "upsert", random.Random(arguments.seed)
                        )
                    else:
                        await writers.mutate_once(0, random.Random(arguments.seed), 0)
                    acknowledged = time.monotonic()
                    revision = max(
                        int(receipt["revision"])
                        for receipt in writers.receipts
                        if receipt is not None
                    )
                    await cohort.wait_selected(revision)
                    evidence.record(
                        "capacity_bounded_update",
                        records=scenario.records,
                        requested_mutations=size,
                        mutations=(
                            writers.counts["sized_operation_intents"] - before_intents
                        )
                        if isinstance(writers, MixedWriters)
                        else 1,
                        durable_ack_seconds=acknowledged - began_update,
                        selected_seconds=time.monotonic() - began_update,
                        selection_poll_seconds=0.05,
                    )
                    await cohort.verify(writers)
            seed_revision = max(
                int(receipt["revision"])
                for receipt in writers.receipts
                if receipt is not None
            )
            memory = admission(
                scenario.subscribers,
                arguments.active,
                len(authority.namespaces),
                resident_bytes(actors.children),
            )
            evidence.record("fleet_memory_admitted", **memory)
            command = [
                str(arguments.fleet),
                "--subscribers",
                str(scenario.subscribers),
                "--active",
                str(arguments.active),
                "--workers",
                str(arguments.workers),
                "--seconds",
                str(duration + 1800),
                "--heap-bytes",
                str(memory["probe_heap_bytes"]),
                "--seed",
                str(arguments.seed),
                "--result",
                str(arguments.output / "fleet-result.json"),
            ]
            if arguments.warm:
                command.extend(["--warm-revision", str(seed_revision)])
            for index, port in enumerate(cohort.relay_ports):
                source = json.loads(cohort.source_paths[0].read_text())
                source.update(
                    port=port,
                    timeout_ms=120000,
                    compression=arguments.compression == "zstd",
                )
                path = arguments.output / f"probe-source-{index}.json"
                path.write_text(json.dumps(source))
                path.chmod(0o600)
                command.extend(["--source", str(path)])
            if arguments.fault == "partial-launch":
                command.extend(["--fail-launch-after", str(scenario.subscribers // 2)])
            delay = (
                60000
                if arguments.fault == "parked-cancel"
                else arguments.bootstrap_delay
            )
            if delay:
                command.extend(
                    [
                        "--delay-bootstrap-slot",
                        str(scenario.subscribers - 1),
                        "--delay-bootstrap-ms",
                        str(delay),
                    ]
                )
            output = FleetOutput(allow_failure=bool(arguments.fault))
            probe = await actors.start(
                "fleet",
                command,
                input_pipe=True,
                observe=output.feed,
            )
            stop = asyncio.Event()
            async with asyncio.TaskGroup() as group:
                group.create_task(sample(authority, evidence, stop))
                try:
                    if arguments.fault:
                        if arguments.fault == "parked-cancel":
                            deadline = time.monotonic() + 15
                            while (
                                output.latest.get("bootstrap_parked", 0)
                                < scenario.subscribers - 1
                            ):
                                if (
                                    probe.process.returncode is not None
                                    or time.monotonic() >= deadline
                                    or output.failure is not None
                                ):
                                    raise RuntimeError(
                                        "fleet did not park completed bootstrap probes"
                                    )
                                await asyncio.sleep(0.01)
                            probe.process.terminate()
                        elif arguments.fault == "cancel":
                            # Give accepted Open/Next exchanges time to enter the transport.
                            await asyncio.sleep(1)
                            probe.process.terminate()
                        await asyncio.wait_for(probe.process.wait(), 60)
                        await probe.capture
                        fault = json.loads(
                            (arguments.output / "fleet-result.json").read_bytes()
                        )
                        accepted = (
                            scenario.subscribers // 2
                            if arguments.fault == "partial-launch"
                            else scenario.subscribers
                        )
                        if (
                            probe.process.returncode == 0
                            or fault.get("success") is not False
                            or fault.get("accepted") != accepted
                            or fault.get("completed") != accepted
                            or fault.get("retained_slot_bytes", 0) <= 0
                            or fault.get("premature_ready_polls") != 0
                            or output.diagnostic
                            != (
                                "fleet: injected fleet partial launch failure"
                                if arguments.fault == "partial-launch"
                                else "fleet: fleet stopped before complete checkpoint"
                            )
                        ):
                            raise RuntimeError(
                                "fleet injected failure did not drain accepted originals"
                            )
                        await cohort.verify(writers)
                        evidence.record(
                            "fleet_fault_drained", fault=arguments.fault, result=fault
                        )
                    else:
                        await output.wait_ready(probe, 900)
                        await relay_settled(
                            cohort,
                            evidence,
                            scenario.subscribers,
                            scenario.subscribers + cohort.count * 2 + 2,
                        )
                        assert probe.process.stdin is not None
                        probe.process.stdin.write(b'{"measurement":true}\n')
                        await probe.process.stdin.drain()
                        await output.wait_event(probe, output.measurement_started, 10)
                        baseline = writers.counts.copy()
                        cpu_began = time.monotonic()
                        cpu_initial = process_cpu_seconds(actors.children.copy())
                        began = time.monotonic()
                        generator_cpu = time.process_time()
                        http_initial = authority.http_activity.snapshot(reset=True)
                        if arguments.offer_rate is None:
                            await writers.phase("mutate", began + duration)
                        else:
                            await paced_mutations(
                                writers, arguments.offer_rate, began + duration
                            )
                        http_final = authority.http_activity.snapshot()
                        workload_elapsed = time.monotonic() - began
                        generator_cpu = time.process_time() - generator_cpu
                        cpu_final = process_cpu_seconds(actors.children.copy())
                        cpu_elapsed = time.monotonic() - cpu_began
                        if cpu_final.keys() != cpu_initial.keys():
                            raise RuntimeError(
                                "fleet process set changed during measurement"
                            )
                        process_cpu = {
                            name: cpu_final[name] - cpu_initial[name]
                            for name in cpu_initial
                        }
                        if any(value < 0 for value in process_cpu.values()):
                            raise RuntimeError("fleet process CPU counter decreased")
                        sustained_counts = dict(writers.counts - baseline)
                        probe.process.stdin.write(b'{"measurement":false}\n')
                        await probe.process.stdin.drain()
                        await output.wait_event(probe, output.measurement_stopped, 10)
                        await writers.phase("verify")
                        barrier = await writers.successful(
                            0,
                            "/v2/read",
                            {
                                "namespace": authority.namespace(0),
                                "keys": [f"{authority.namespace(0)}:key0"],
                            },
                        )
                        revision = int(barrier["version"])
                        assert probe.process.stdin is not None
                        probe.process.stdin.write(
                            json.dumps({"checkpoint": revision}).encode() + b"\n"
                        )
                        await probe.process.stdin.drain()
                        await asyncio.wait_for(probe.process.wait(), 900)
                        await probe.capture
                        if probe.process.returncode != 0:
                            raise RuntimeError(
                                "native fleet did not drain successfully"
                            )
                        result = load_result(
                            arguments.output / "fleet-result.json",
                            scenario.subscribers,
                            revision,
                        )
                        if delay and result["peak_bootstrap_parked"] < 1:
                            raise RuntimeError(
                                "delayed bootstrap did not exercise completed-probe parking"
                            )
                        if result["skipped_closes"] == 0:
                            await relay_settled(
                                cohort, evidence, 0, cohort.count * 2 + 2
                            )
                        else:
                            await relay_snapshot(
                                cohort, evidence, "retained_after_skipped_close"
                            )
                        await cohort.verify(writers)
                        evidence.record(
                            "fleet_verified",
                            result=result,
                            complete_records=scenario.records,
                            producer_receipts=scenario.producers,
                            sustained_seconds=workload_elapsed,
                            scheduled_interval_seconds=duration,
                            pending_drain_seconds=max(0, workload_elapsed - duration),
                            generator_cpu_seconds=generator_cpu,
                            process_cpu_seconds=process_cpu,
                            process_cpu_interval_seconds=cpu_elapsed,
                            process_core_demand={
                                name: value / cpu_elapsed
                                for name, value in process_cpu.items()
                            },
                            checkpoint_and_verification_seconds=time.monotonic()
                            - began
                            - workload_elapsed,
                            sustained_counts=sustained_counts,
                            http_initial=http_initial,
                            http_final=http_final,
                            active_durable_stores=scenario.durable_agents,
                        )
                finally:
                    stop.set()
            resource_stop.set()
            await resources_task
            await actors.stop([probe])
            await cohort.close()
            await drained(authority, evidence)
            children = actors.children.copy()
        if any(actor.process.returncode != 0 for actor in children):
            raise RuntimeError("fleet authorities did not drain successfully")
        evidence.record("completed", dropped_output_records=evidence.dropped)
    finally:
        evidence.close()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in (
        "scenario",
        "binary",
        "agents",
        "relays",
        "local-oracle",
        "fleet",
        "certificates",
        "build",
        "output",
    ):
        parser.add_argument(
            f"--{name}", type=lambda value: Path(value).resolve(), required=True
        )
    parser.add_argument("--capacity-config", type=lambda value: Path(value).resolve())
    parser.add_argument("--relay-count", type=int, choices=(1, 2), default=2)
    parser.add_argument("--active", type=int, default=128)
    parser.add_argument("--workers", type=int, default=1)
    parser.add_argument("--seed", type=int, default=17)
    parser.add_argument("--http-workers", type=int)
    parser.add_argument("--offer-rate", type=float)
    parser.add_argument("--duration", type=int)
    parser.add_argument(
        "--fault", choices=("partial-launch", "cancel", "parked-cancel")
    )
    parser.add_argument("--bootstrap-delay", type=int, default=0)
    parser.add_argument("--warm", action="store_true")
    parser.add_argument("--mixed", action="store_true")
    parser.add_argument("--hot-namespace", action="store_true")
    parser.add_argument("--compression", choices=("raw", "zstd"), default="raw")
    asyncio.run(run(parser.parse_args()))


if __name__ == "__main__":
    main()
