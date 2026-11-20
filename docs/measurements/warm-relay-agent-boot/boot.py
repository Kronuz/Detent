"""Time one cold agent bootstrap from a relay, warm or cold.

usage: boot.py MODE FIXTURE OUTPUT BUILD TREE
  MODE   warm: start the relay, wait for "durable bundles ready" at the fixture's revision and for
         the relay to go idle, then start the clock and one cold agent.
         cold: start the relay and the agent under one clock (coldboot.py's measurement).
  FIXTURE holds state-0..2 and seeded.json (records, revision); it is cloned, never written.
  BUILD  the release build directory; TREE the checkout whose tests/detent_system it runs.

Prints one "SUMMARY {json}" line and writes OUTPUT/marks.json; exits nonzero on any failure.
"""

from __future__ import annotations

import asyncio
import json
import subprocess
import sys
import time
from dataclasses import replace
from datetime import datetime
from pathlib import Path
from typing import Any

MODE, FIXTURE, OUTPUT, BUILD, TREE = sys.argv[1:6]
sys.path.insert(0, str(Path(TREE) / "tests/detent_system"))

from authority import Authority
from capacity import CapacityProfile
from managed import ManagedCohort
from mixed import MixedWriters
from run_fleet import relay_snapshot
from runtime import Actors, Evidence

READY = b"durable bundles ready"
SELECTION_TIMEOUT = 1800
SETTLE_LIMIT = 120.0


def run_command(command: list[str], timeout: float) -> str:
    return subprocess.run(
        command, check=False, capture_output=True, text=True, timeout=timeout
    ).stdout


def ps_seconds(value: str) -> float:
    total = 0.0
    for part in value.replace("-", ":").split(":"):
        total = total * 60 + float(part)
    return total


async def ps(pids: list[int]) -> dict[int, tuple[float, int]]:
    """CPU seconds and RSS KiB per live pid."""
    process = await asyncio.create_subprocess_exec(
        "ps",
        "-o",
        "pid=,time=,rss=",
        "-p",
        ",".join(str(pid) for pid in pids),
        stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.DEVNULL,
    )
    out, _ = await process.communicate()
    result: dict[int, tuple[float, int]] = {}
    for line in out.decode().splitlines():
        fields = line.split()
        if len(fields) == 3:
            result[int(fields[0])] = (ps_seconds(fields[1]), int(fields[2]))
    return result


def nettop(pid: int) -> tuple[int, int]:
    """Bytes in and out of one process's sockets, as nettop counts them."""
    out = run_command(
        ["nettop", "-P", "-L", "1", "-x", "-p", str(pid), "-J", "bytes_in,bytes_out"],
        15,
    )
    for line in out.splitlines():
        fields = line.split(",")
        if len(fields) >= 3 and fields[0].endswith(f".{pid}"):
            return int(fields[1]), int(fields[2])
    raise RuntimeError(f"nettop has no row for pid {pid}: {out!r}")


def clone(fixture: Path, output: Path) -> None:
    for index in range(3):
        subprocess.run(
            [
                "cp",
                "-cR",
                str(fixture / f"state-{index}"),
                str(output / f"state-{index}"),
            ],
            check=True,
            timeout=300,
        )


def log_time(text: str) -> float | None:
    """Epoch seconds of a detent log line ("<glyph>2026-11-19 22:35:13.094 ...", local time)."""
    stamp = text.lstrip()[1:24]
    try:
        return datetime.strptime(stamp, "%Y-%m-%d %H:%M:%S.%f").astimezone().timestamp()
    except ValueError:
        return None


def agent_lines(events: Path) -> list[tuple[float, str]]:
    text = "".join(
        event["text"]
        for event in map(json.loads, events.read_text().splitlines())
        if event.get("kind") == "actor_output" and event.get("name") == "agent-0"
    )
    lines: list[tuple[float, str]] = []
    for line in text.splitlines():
        when = log_time(line)
        if when is not None:
            lines.append((when, line.lstrip()[24:].strip()))
    return lines


class Tracer:
    """Samples CPU and RSS of the relay and agent every half second, stamped on the monotonic clock."""

    def __init__(self) -> None:
        self.pids: dict[str, int] = {}
        self.samples: list[tuple[float, str, float, int]] = []

    async def run(self) -> None:
        while True:
            if self.pids:
                now = time.monotonic()
                found = await ps(list(self.pids.values()))
                for name, pid in self.pids.items():
                    if pid in found:
                        self.samples.append((now, name, *found[pid]))
            await asyncio.sleep(0.5)

    def cpu_at(self, name: str, at: float) -> float | None:
        before = [s for s in self.samples if s[1] == name and s[0] <= at]
        return before[-1][2] if before else None

    def peak_rss(self, name: str, since: float) -> int:
        return max(
            (s[3] for s in self.samples if s[1] == name and s[0] >= since), default=0
        )


async def run() -> dict[str, Any]:
    if MODE not in ("warm", "cold"):
        raise ValueError("MODE is warm or cold")
    fixture, output, build, tree = Path(FIXTURE), Path(OUTPUT), Path(BUILD), Path(TREE)
    seeded = json.loads((fixture / "seeded.json").read_text())
    records, seeded_revision = int(seeded["records"]), int(seeded["revision"])
    binaries = {
        name: build / "src" / name
        for name in ("detent-authority", "detent-agent", "detent-relay")
    }
    oracle = build / "tests/detent_system/detent_system_mixed_oracle"
    certificates = build / "tests/detent_authority/tls-fixture"
    capacity = CapacityProfile.load(
        tree / "tests/detent_system/presets/capacity-5000000.json"
    )
    capacity = replace(
        capacity,
        delivery=capacity.delivery | {"timeout_ms": 600000, "string_bytes": 536870912},
        image=capacity.image | {"string_bytes": 536870912},
    )
    configuration: dict[str, Any] = {
        "mode": MODE,
        "records": records,
        "seeded": seeded,
        "fixture": str(fixture.resolve()),
        "build": str(build.resolve()),
        "tree": str(tree.resolve()),
        "binary_file": run_command(["file", *map(str, binaries.values())], 5),
        "delivery_limits": capacity.delivery,
        "image_limits": capacity.image,
        "authority_voters": 3,
        "relays": 1,
        "agents": 1,
        "selection_timeout": SELECTION_TIMEOUT,
    }
    if any("x86_64" not in line for line in configuration["binary_file"].splitlines()):
        raise RuntimeError(f"not x86_64 binaries: {configuration['binary_file']}")
    print(json.dumps({"effective_configuration": configuration}), flush=True)
    output.mkdir(mode=0o700)
    clone(fixture, output)
    evidence = Evidence(output / "events.jsonl", 128 * 1024 * 1024)
    marks: dict[str, Any] = {"configuration": configuration, "result": "failed"}
    tracer = Tracer()
    tracing = asyncio.create_task(tracer.run())
    t0 = time.monotonic()
    relay_ready: list[float] = []
    buffered = bytearray()

    def watch_relay(payload: bytes) -> None:
        buffered.extend(payload)
        if not relay_ready and READY in buffered:
            relay_ready.append(time.monotonic())
        del buffered[: max(0, len(buffered) - len(READY))]

    try:
        async with Actors(evidence, 60) as actors:
            launch = actors.start

            async def start_observed(
                name: str, command: list[str], **options: Any
            ) -> Any:
                if name == "relay-0":
                    options["observe"] = watch_relay
                return await launch(name, command, **options)

            actors.start = start_observed  # type: ignore[method-assign]
            authority = Authority(
                binaries["detent-authority"],
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
            marks["authority_ready_s"] = time.monotonic() - t0
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
            if type(revision) is not int or revision < seeded_revision:
                raise RuntimeError(
                    f"authority revision {revision} below seeded {seeded_revision}"
                )
            marks["revision"] = revision
            cohort = ManagedCohort(
                authority,
                actors,
                output,
                binaries["detent-agent"],
                oracle,
                certificates,
                1,
                4,
                relay=binaries["detent-relay"],
                relay_count=1,
                probe_sessions=16,
                probe_active=16,
                source_timeout_ms=600000,
                selection_timeout=SELECTION_TIMEOUT,
                namespaces=list(authority.namespaces),
                capacity=capacity,
            )
            # Relays first (count 0), then the agent (relay None keeps the relay ports).
            cohort.count = 0
            relay_start = time.monotonic()
            await asyncio.wait_for(cohort.start(), 60)
            relay = cohort.relays[0].process.pid
            tracer.pids["relay"] = relay
            if MODE == "warm":
                while not relay_ready:
                    if cohort.relays[0].process.returncode is not None:
                        raise RuntimeError("relay exited before durable bundles ready")
                    if time.monotonic() - relay_start > SELECTION_TIMEOUT:
                        raise TimeoutError("relay missed durable bundles ready")
                    await asyncio.sleep(0.05)
                marks["relay_ready_s"] = relay_ready[0] - relay_start
                while True:
                    _, health = await authority.request(
                        0, "/v1/health", endpoint_port=cohort.relay_ports[0]
                    )
                    bundle = health["bundles"][0]
                    if bundle["revision"] >= revision and bundle["snapshot_ready"]:
                        break
                    if time.monotonic() - relay_start > SELECTION_TIMEOUT:
                        raise TimeoutError("relay never reached the fixture's revision")
                    await asyncio.sleep(0.1)
                marks["relay_revision_s"] = time.monotonic() - relay_start
                # Idle: four half-second samples in a row add at most 0.01 s of relay CPU.
                quiet = 0
                last = (await ps([relay]))[relay][0]
                while (
                    quiet < 4
                    and time.monotonic() - relay_start
                    < SETTLE_LIMIT + marks["relay_revision_s"]
                ):
                    await asyncio.sleep(0.5)
                    now = (await ps([relay]))[relay][0]
                    quiet = quiet + 1 if now - last <= 0.01 else 0
                    last = now
                marks["relay_settled"] = quiet >= 4
                marks["relay_settled_s"] = time.monotonic() - relay_start
                marks["relay_cpu_at_settle_s"] = last
                marks["relay_wire_before"] = nettop(relay)
            cohort.count = 1
            cohort.relay = None
            start_wall = time.time()
            start = time.monotonic()
            await asyncio.wait_for(cohort.start(), 60)
            agent = cohort.members[0].process.pid
            tracer.pids["agent"] = agent
            await asyncio.wait_for(cohort.wait_selected(revision), SELECTION_TIMEOUT)
            selected = time.monotonic()
            marks["agent_selected_s"] = selected - start
            marks["relay_start_to_selected_s"] = selected - relay_start
            if MODE == "cold":
                marks["relay_ready_s"] = (
                    relay_ready[0] - relay_start if relay_ready else None
                )
            usage = await ps([relay, agent])
            marks["agent_cpu_s"], marks["agent_rss_kib"] = usage[agent]
            marks["relay_cpu_s"] = usage[relay][0]
            marks["agent_wire"] = nettop(agent)
            marks["relay_wire"] = nettop(relay)
            await asyncio.wait_for(relay_snapshot(cohort, evidence, "end"), 30)
            await cohort.close()
            marks["relay_cpu_at_agent_start_s"] = tracer.cpu_at("relay", start)
            marks["agent_rss_peak_kib"] = max(
                tracer.peak_rss("agent", start), marks["agent_rss_kib"]
            )
            marks["agent_start_wall"] = start_wall
            marks["trace"] = [
                (round(t - start, 3), name, cpu, rss)
                for t, name, cpu, rss in tracer.samples
            ]
        marks["result"] = "passed"
    finally:
        tracing.cancel()
        await asyncio.gather(tracing, return_exceptions=True)
        marks["total_s"] = time.monotonic() - t0
        evidence.close()
        (output / "marks.json").write_text(json.dumps(marks, indent=1))
    return marks


def summarize(output: Path, marks: dict[str, Any]) -> dict[str, Any]:
    """Phase marks from the agent's log and the relay's end status."""
    start_wall = marks["agent_start_wall"]
    lines = agent_lines(output / "events.jsonl")
    running = next((t for t, l in lines if "managed delivery running" in l), None)
    synced = [(t, l) for t, l in lines if "publication synced" in l]
    hashed = [l for _, l in lines if "hashed" in l and "since start" in l]
    relay: dict[str, Any] = {}
    for raw in (output / "events.jsonl").read_text().splitlines():
        event = json.loads(raw)
        if event.get("kind") == "relay_health" and event.get("phase") == "end":
            relay = event["snapshot"]["bundles"][0]
    if not synced:
        raise RuntimeError("the agent logged no publication synced line")
    published = synced[0][0] - start_wall
    agent_cpu_at_published = max(
        (
            cpu
            for t, name, cpu, _ in marks["trace"]
            if name == "agent" and t <= published
        ),
        default=None,
    )
    relay_before = marks.get("relay_wire_before") or (0, 0)
    summary = {
        "mode": marks["configuration"]["mode"],
        "records": marks["configuration"]["records"],
        "revision": marks["revision"],
        "authority_ready_s": marks["authority_ready_s"],
        "relay_ready_s": marks.get("relay_ready_s"),
        "relay_settled_s": marks.get("relay_settled_s"),
        "relay_settled": marks.get("relay_settled"),
        "agent_selected_s": marks["agent_selected_s"],
        "relay_start_to_selected_s": marks["relay_start_to_selected_s"],
        "agent_running_s": None if running is None else running - start_wall,
        "agent_published_s": published,
        "agent_cpu_s": marks["agent_cpu_s"],
        "agent_cpu_at_published_s": agent_cpu_at_published,
        "agent_rss_peak_kib": marks["agent_rss_peak_kib"],
        "agent_bytes_in": marks["agent_wire"][0],
        "agent_bytes_out": marks["agent_wire"][1],
        "relay_serve_bytes_out": marks["relay_wire"][1] - relay_before[1],
        "relay_serve_bytes_in": marks["relay_wire"][0] - relay_before[0],
        "relay_cpu_s": marks["relay_cpu_s"],
        "relay_cpu_during_agent_s": marks["relay_cpu_s"]
        - (marks["relay_cpu_at_agent_start_s"] or 0.0),
        "agent_publication": synced[0][1],
        "agent_hashed": hashed[0] if hashed else None,
        "relay_publication_files": relay.get("publication_files"),
        "relay_publication_device_flushes": relay.get("publication_device_flushes"),
        "relay_revision": relay.get("revision"),
        "total_s": marks["total_s"],
    }
    return summary


if __name__ == "__main__":
    result = asyncio.run(run())
    print(
        "SUMMARY " + json.dumps(summarize(Path(OUTPUT), result), sort_keys=True),
        flush=True,
    )
