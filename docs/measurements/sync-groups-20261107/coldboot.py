"""Restore cloned authority voters, then time one relay and one cold agent bootstrap, and optionally
the agent's restart on the store it built (three starts, to the "store ready" line).

usage: coldboot.py SOURCE OUTPUT RECORDS BUILD TREE [sample] [restart]
  BUILD is the build directory holding the binaries; TREE the source tree whose tests/detent_system it runs.
"""

from __future__ import annotations

import asyncio
import hashlib
import json
import signal
import subprocess
import sys
import time
from builtins import ExceptionGroup
from collections.abc import Callable
from dataclasses import replace
from pathlib import Path

REPO = Path(sys.argv[5])
sys.path.insert(0, str(REPO / "tests/detent_system"))

from authority import Authority
from capacity import CapacityProfile
from managed import ManagedCohort
from mixed import MixedWriters
from run_fleet import relay_snapshot
from runtime import Actors, Evidence


def run_command(
    command: list[str], timeout: float, *, check: bool = True
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command, check=check, capture_output=True, text=True, timeout=timeout
    )


def start_sampler(pid: int, output: Path) -> subprocess.Popen[bytes]:
    return subprocess.Popen(
        ["sample", str(pid), "1500", "5", "-mayDie", "-file", str(output)],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )


async def wait_selection(
    selection: asyncio.Task[None], timeout: float, observe: Callable[[], None]
) -> None:
    deadline = time.monotonic() + timeout
    try:
        while not selection.done():
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError(
                    f"cold bootstrap exceeded the {timeout:g}-second selection deadline"
                )
            await asyncio.wait({selection}, timeout=min(1.0, remaining))
            observe()
        await selection
    finally:
        if not selection.done():
            selection.cancel()
        await asyncio.gather(selection, return_exceptions=True)


def cpu_seconds(pid: int) -> float:
    out = run_command(
        ["ps", "-o", "time=", "-p", str(pid)], 5, check=False
    ).stdout.strip()
    if not out:
        return -1.0
    parts = out.replace("-", ":").split(":")
    total = 0.0
    for p in parts:
        total = total * 60 + float(p)
    return total


def lo0() -> list[str]:
    return [
        l
        for l in run_command(["netstat", "-ib", "-I", "lo0"], 10).stdout.splitlines()
        if "Link" in l
    ]


def stop_samplers(samplers: list[subprocess.Popen[bytes]]) -> None:
    failures: list[Exception] = []
    for process in samplers:
        if process.poll() is None:
            try:
                process.send_signal(signal.SIGINT)
            except ProcessLookupError:
                pass
    for process in samplers:
        try:
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)
        except (OSError, subprocess.TimeoutExpired) as error:
            failures.append(error)
    samplers.clear()
    if failures:
        raise ExceptionGroup("sampler cleanup failed", failures)


def preflight(
    source: Path, build: Path, records: int
) -> tuple[str, Path, dict[str, object]]:
    stages = [
        value.removeprefix("--stage=")
        for value in sys.argv[6:]
        if value.startswith("--stage=")
    ]
    stage = (
        stages[0] if len(stages) == 1 else "smoke" if records <= 10000 else "unapproved"
    )
    if stage not in {"smoke", "screen", "confirm", "aa"}:
        raise ValueError(
            "long coldboot runs require an explicit staged wrapper after an all-arm smoke"
        )
    if stage == "smoke" and records > 10000:
        raise ValueError("smoke is limited to 10000 records")
    binaries = [
        build / "src" / name for name in ("detentd", "detent-agent", "detent-relay")
    ]
    binaries.append(build / "tests/detent_system/detent_system_mixed_oracle")
    signatures = {
        str(path.resolve()): [path.stat().st_size, path.stat().st_mtime_ns]
        for path in binaries
    }
    if any(signature[0] <= 0 for signature in signatures.values()):
        raise ValueError("empty binary in coldboot configuration")
    for index in range(3):
        root = source / f"state-{index}"
        if not root.is_dir() or not any(root.iterdir()):
            raise ValueError(f"missing or empty authority clone: {root}")
    if not (build / "tests/detent_authority/tls-fixture/ca.pem").is_file():
        raise ValueError("missing TLS fixture")
    key = hashlib.sha256(str(build.resolve()).encode()).hexdigest()[:16]
    marker = Path(__file__).resolve().parent / f"smoke-{key}.json"
    if stage != "smoke":
        prior: object = json.loads(marker.read_text())
        if (
            not isinstance(prior, dict)
            or prior.get("binaries") != signatures
            or prior.get("result") != "passed"
        ):
            raise ValueError("coldboot binaries changed or smoke did not pass")
    configuration: dict[str, object] = {
        "stage": stage,
        "records": records,
        "build": str(build.resolve()),
        "tree": str(REPO.resolve()),
        "source": str(source.resolve()),
        "binaries": signatures,
        "binary_file": run_command(
            ["file", *[str(path) for path in binaries]], 5
        ).stdout,
    }
    print(json.dumps({"preflight": configuration}), flush=True)
    return stage, marker, configuration


async def run(
    source: Path, output: Path, records: int, build: Path, sample: bool, restart: bool
) -> None:
    stage, marker, configuration = preflight(source, build, records)
    certificates = build / "tests/detent_authority/tls-fixture"
    capacity = CapacityProfile.load(
        REPO / "tests/detent_system/presets/capacity-5000000.json"
    )
    capacity = replace(
        capacity,
        delivery=capacity.delivery | {"timeout_ms": 600000, "string_bytes": 536870912},
        image=capacity.image | {"string_bytes": 536870912},
    )
    if not 1 <= records <= capacity.application["keys"]:
        raise ValueError("record count exceeds the effective capacity")
    configuration.update(
        {
            "delivery_limits": capacity.delivery,
            "image_limits": capacity.image,
            "selection_timeout": 1800,
            "source_timeout_ms": 600000,
            "authority_voters": 3,
            "agents": 1,
            "relays": 1,
        }
    )
    print(json.dumps({"effective_configuration": configuration}), flush=True)
    output.mkdir(mode=0o700)
    evidence = Evidence(output / "events.jsonl", 128 * 1024 * 1024)
    marks: dict[str, object] = {"configuration": configuration, "result": "failed"}
    samplers: list[subprocess.Popen[bytes]] = []
    selection: asyncio.Task[None] | None = None
    t0 = time.monotonic()
    try:
        for index in range(3):
            run_command(
                [
                    "cp",
                    "-cR",
                    str(source / f"state-{index}"),
                    str(output / f"state-{index}"),
                ],
                300,
            )
        async with Actors(evidence, 60) as actors:
            authority = Authority(
                build / "src/detentd",
                certificates,
                output,
                actors,
                4,
                http_workers=4,
                capacity=capacity,
            )
            try:
                await authority.start()
            except RuntimeError as error:
                if str(error) != "authority did not elect an available leader":
                    raise
                deadline = time.monotonic() + 900
                while True:
                    try:
                        await authority.leader()
                        break
                    except RuntimeError:
                        if time.monotonic() >= deadline:
                            raise
                        await asyncio.sleep(0.5)
            marks["authority_ready"] = time.monotonic() - t0
            writers = MixedWriters(authority, records, 4, 17, maximum_records=records)
            state = await asyncio.wait_for(
                writers.successful(
                    0,
                    "/v2/read",
                    {"namespace": authority.namespace(0), "keys": [writers.key(0)]},
                ),
                60,
            )
            revision = state.get("version")
            if type(revision) is not int:
                raise RuntimeError("authority read did not return an integer revision")
            cohort = ManagedCohort(
                authority,
                actors,
                output,
                build / "src/detent-agent",
                build / "tests/detent_system/detent_system_mixed_oracle",
                certificates,
                1,
                4,
                relay=build / "src/detent-relay",
                relay_count=1,
                probe_sessions=16,
                probe_active=16,
                source_timeout_ms=600000,
                selection_timeout=1800,
                namespaces=list(authority.namespaces),
                capacity=capacity,
            )
            marks["lo0_before"] = lo0()
            start = time.monotonic()
            marks["cohort_start"] = start - t0
            await asyncio.wait_for(cohort.start(), 60)
            relay = cohort.relays[0].process.pid
            agent_pid = cohort.members[0].process.pid
            if sample:
                for name, pid in (("relay", relay), ("agent", agent_pid)):
                    samplers.append(start_sampler(pid, output / f"{name}.sample.txt"))
            first_agent_cpu: float | None = None

            def observe_selection() -> None:
                nonlocal first_agent_cpu
                if first_agent_cpu is None and cpu_seconds(agent_pid) > 3.0:
                    first_agent_cpu = time.monotonic() - start
                    marks["agent_busy"] = first_agent_cpu
                    marks["relay_cpu_at_agent_busy"] = cpu_seconds(relay)

            selection = asyncio.create_task(cohort.wait_selected(revision))
            await wait_selection(selection, cohort.selection_timeout, observe_selection)
            marks["agent_selected"] = time.monotonic() - start
            marks["relay_cpu"] = cpu_seconds(relay)
            marks["agent_cpu"] = cpu_seconds(agent_pid)
            for name, pid in (("relay", relay), ("agent", agent_pid)):
                marks[f"{name}_nettop"] = run_command(
                    [
                        "nettop",
                        "-P",
                        "-L",
                        "1",
                        "-x",
                        "-p",
                        str(pid),
                        "-J",
                        "bytes_in,bytes_out",
                    ],
                    15,
                    check=False,
                ).stdout
            stop_samplers(samplers)
            await asyncio.wait_for(relay_snapshot(cohort, evidence, "end"), 30)
            marks["lo0_after"] = lo0()
            await cohort.close()
            if restart:
                restart_seconds: list[float] = []
                marks["restart_seconds"] = restart_seconds
                agent_config = output / "agent-0" / "agent.json"
                for trial in range(3):
                    ready_at: list[float] = []
                    started = time.monotonic()

                    def observe(
                        payload: bytes, observed: list[float] = ready_at
                    ) -> None:
                        if not observed and (
                            b"store ready" in payload
                            or b"awaiting its first generation" in payload
                        ):
                            observed.append(time.monotonic())

                    agent = await actors.start(
                        f"agent-restart-{trial}",
                        [
                            str(build / "src/detent-agent"),
                            "--config",
                            str(agent_config),
                        ],
                        observe=observe,
                    )
                    while (
                        not ready_at
                        and agent.process.returncode is None
                        and time.monotonic() - started < 1800
                    ):
                        await asyncio.sleep(0.02)
                    if not ready_at:
                        raise TimeoutError(
                            f"agent restart {trial} exited or missed its 1800-second readiness deadline"
                        )
                    restart_seconds.append(ready_at[0] - started)
                    await actors.stop([agent])
                    actors.children = [
                        child for child in actors.children if child is not agent
                    ]
        marks["result"] = "passed"
        if stage == "smoke":
            marker.write_text(json.dumps(configuration | {"result": "passed"}))
    finally:
        if selection is not None:
            if not selection.done():
                selection.cancel()
            await asyncio.gather(selection, return_exceptions=True)
        stop_samplers(samplers)
        marks["total_seconds"] = time.monotonic() - t0
        evidence.close()
        (output / "marks.json").write_text(json.dumps(marks, indent=1))
        print(json.dumps(marks))


if __name__ == "__main__":
    asyncio.run(
        run(
            Path(sys.argv[1]),
            Path(sys.argv[2]),
            int(sys.argv[3]),
            Path(sys.argv[4]),
            "sample" in sys.argv[6:],
            "restart" in sys.argv[6:],
        )
    )
