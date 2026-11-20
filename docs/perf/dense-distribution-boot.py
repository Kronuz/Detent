"""Cold-agent bootstrap from one warm relay: pages against records, on one fixture.

usage: dense-distribution-boot.py FIXTURE OUTPUT BINS TREE ARM...
  FIXTURE holds state-0..2 and seeded.json; it is cloned, never written.
  BINS    holds detent-authority, detent-relay, detent-agent-pages and detent-agent-records
          (dense-distribution-bins.sh); TREE is the checkout whose tests/detent_system it runs.
  ARM     pages or records, the measured agents in order.

One authority restores the fixture and one relay mirrors it. When the relay reports its mirror
ready, a priming pages agent (the relay builds its pages for it) and a priming records agent boot;
then each ARM boots one fresh agent, one at a time, after the relay goes quiet. An agent is ready
when its selected table reaches every namespace's position at the authority. Per agent: wall time
to ready, socket bytes in (nettop), bytes hashed and page transfers (its log), CPU, disk bytes
written and lifetime peak physical footprint (proc_pid_rusage), and the relay's CPU and bytes out.
Prints one "AGENT {json}" line per agent; exits nonzero when an arm did not boot as its arm.
"""

from __future__ import annotations

import asyncio
import ctypes
import json
import re
import subprocess
import sys
import time
from dataclasses import replace
from datetime import datetime
from pathlib import Path
from typing import Any

FIXTURE, OUTPUT, BINS, TREE = sys.argv[1:5]
ARMS = sys.argv[5:]
sys.path.insert(0, str(Path(TREE) / "tests/detent_system"))

from authority import Authority
from capacity import CapacityProfile
from managed import ManagedCohort
from mixed import MixedWriters
from runtime import Actors, Evidence

SELECTION_TIMEOUT = 1800
QUIET_LIMIT = 5.0
HASHED = re.compile(r"hashed (\d+) bytes since start: (\d+) to build, (\d+) to check, (\d+) in frames")
TRANSFERS = re.compile(r"page transfers (\d+) received \((\d+) bytes\), (\d+) refused")
SYNCED = re.compile(r"publication synced (\d+) files")


class RusageInfoV4(ctypes.Structure):
    _fields_ = [("ri_uuid", ctypes.c_uint8 * 16)] + [
        (name, ctypes.c_uint64)
        for name in [
            "user_time",
            "system_time",
            "pkg_idle_wkups",
            "interrupt_wkups",
            "pageins",
            "wired_size",
            "resident_size",
            "phys_footprint",
            "proc_start_abstime",
            "proc_exit_abstime",
            "child_user_time",
            "child_system_time",
            "child_pkg_idle_wkups",
            "child_interrupt_wkups",
            "child_pageins",
            "child_elapsed_abstime",
            "diskio_bytesread",
            "diskio_byteswritten",
            "qos_default",
            "qos_maintenance",
            "qos_background",
            "qos_utility",
            "qos_legacy",
            "qos_user_initiated",
            "qos_user_interactive",
            "billed_system_time",
            "serviced_system_time",
            "logical_writes",
            "lifetime_max_phys_footprint",
            "instructions",
            "cycles",
            "billed_energy",
            "serviced_energy",
            "interval_max_phys_footprint",
            "runnable_time",
        ]
    ]


class Timebase(ctypes.Structure):
    _fields_ = [("numer", ctypes.c_uint32), ("denom", ctypes.c_uint32)]


LIBPROC = ctypes.CDLL("/usr/lib/libproc.dylib")
LIBSYSTEM = ctypes.CDLL("/usr/lib/libSystem.B.dylib")
TIMEBASE = Timebase()
LIBSYSTEM.mach_timebase_info(ctypes.byref(TIMEBASE))


def rusage(pid: int) -> dict[str, float]:
    """CPU seconds, disk bytes and lifetime peak footprint of a live process."""
    info = RusageInfoV4()
    if LIBPROC.proc_pid_rusage(pid, 4, ctypes.byref(info)) != 0:
        raise RuntimeError(f"proc_pid_rusage failed for pid {pid}")
    seconds = TIMEBASE.numer / TIMEBASE.denom / 1e9
    return {
        "cpu_s": (info.user_time + info.system_time) * seconds,
        "disk_read": info.diskio_bytesread,
        "disk_written": info.diskio_byteswritten,
        "peak_footprint": info.lifetime_max_phys_footprint,
        "footprint": info.phys_footprint,
    }


def run_command(command: list[str], timeout: float) -> str:
    return subprocess.run(command, check=False, capture_output=True, text=True, timeout=timeout).stdout


def nettop(pid: int) -> tuple[int, int]:
    """Bytes in and out of one process's sockets, as nettop counts them."""
    out = run_command(["nettop", "-P", "-L", "1", "-x", "-p", str(pid), "-J", "bytes_in,bytes_out"], 15)
    for line in out.splitlines():
        fields = line.split(",")
        if len(fields) >= 3 and fields[0].endswith(f".{pid}"):
            return int(fields[1]), int(fields[2])
    raise RuntimeError(f"nettop has no row for pid {pid}: {out!r}")


def log_time(text: str) -> float | None:
    """Epoch seconds of a detent log line ("<glyph>2026-11-19 22:35:13.094 ...", local time)."""
    stamp = text.lstrip()[1:24]
    try:
        return datetime.strptime(stamp, "%Y-%m-%d %H:%M:%S.%f").astimezone().timestamp()
    except ValueError:
        return None


def actor_lines(events: Path, name: str) -> list[tuple[float, str]]:
    text = "".join(
        event["text"]
        for event in map(json.loads, events.read_text().splitlines())
        if event.get("kind") == "actor_output" and event.get("name") == name
    )
    lines: list[tuple[float, str]] = []
    for line in text.splitlines():
        when = log_time(line)
        if when is not None:
            lines.append((when, line.lstrip()[24:].strip()))
    return lines


def last(pattern: re.Pattern[str], lines: list[tuple[float, str]]) -> list[int] | None:
    found = [pattern.search(line) for _, line in lines]
    matches = [m for m in found if m]
    return [int(value) for value in matches[-1].groups()] if matches else None


async def quiet(pid: int) -> tuple[bool, float]:
    """Wait until four 0.25 s samples in a row add at most 0.01 s of CPU, bounded."""
    began = time.monotonic()
    calm = 0
    previous = rusage(pid)["cpu_s"]
    while calm < 4 and time.monotonic() - began < QUIET_LIMIT:
        await asyncio.sleep(0.25)
        now = rusage(pid)["cpu_s"]
        calm = calm + 1 if now - previous <= 0.01 else 0
        previous = now
    return calm >= 4, time.monotonic() - began


async def run() -> list[dict[str, Any]]:
    if not ARMS or any(arm not in ("pages", "records") for arm in ARMS):
        raise ValueError("each ARM is pages or records")
    fixture, output, bins, tree = Path(FIXTURE), Path(OUTPUT), Path(BINS), Path(TREE)
    seeded = json.loads((fixture / "seeded.json").read_text())
    records = int(seeded["records"])
    agents = {arm: bins / f"detent-agent-{arm}" for arm in ("pages", "records")}
    binaries = [bins / "detent-authority", bins / "detent-relay", *agents.values()]
    certificates = bins.parent / "tests/detent_authority/tls-fixture"
    capacity = CapacityProfile.load(tree / "tests/detent_system/presets/capacity-5000000.json")
    capacity = replace(
        capacity,
        delivery=capacity.delivery | {"timeout_ms": 600000, "string_bytes": 536870912},
        image=capacity.image | {"string_bytes": 536870912},
    )
    configuration: dict[str, Any] = {
        "records": records,
        "seeded": seeded,
        "fixture": str(fixture.resolve()),
        "bins": str(bins.resolve()),
        "built_commit": (bins / "BUILT_COMMIT").read_text().strip(),
        "tree": str(tree.resolve()),
        "arms": ARMS,
        "binary_file": run_command(["file", *map(str, binaries)], 5),
        "delivery_limits": capacity.delivery,
        "image_limits": capacity.image,
        "authority_voters": 3,
        "relays": 1,
    }
    if any("x86_64" not in line for line in configuration["binary_file"].splitlines()):
        raise RuntimeError(f"not x86_64 binaries: {configuration['binary_file']}")
    print(json.dumps({"effective_configuration": configuration}), flush=True)
    output.mkdir(mode=0o700)
    for index in range(3):
        copy = await asyncio.create_subprocess_exec(
            "cp", "-cR", str(fixture / f"state-{index}"), str(output / f"state-{index}")
        )
        if await asyncio.wait_for(copy.wait(), 300):
            raise RuntimeError(f"cloning state-{index} failed")
    events = output / "events.jsonl"
    evidence = Evidence(events, 256 * 1024 * 1024)
    results: list[dict[str, Any]] = []
    t0 = time.monotonic()
    try:
        async with Actors(evidence, 60) as actors:
            authority = Authority(
                bins / "detent-authority", certificates, output, actors, 4, http_workers=4, capacity=capacity
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
            authority_ready = time.monotonic() - t0
            writers = MixedWriters(authority, records, 4, 17, maximum_records=records)
            positions: dict[str, int] = {}
            for producer in range(4):
                state = await asyncio.wait_for(
                    writers.successful(
                        producer,
                        "/v2/read",
                        {"namespace": authority.namespace(producer), "keys": [writers.key(producer)]},
                    ),
                    60,
                )
                if type(state.get("version")) is not int or state["version"] <= 0:
                    raise RuntimeError(f"no position for {authority.namespace(producer)}: {state}")
                positions[authority.namespace(producer)] = state["version"]
            namespaces = sorted(positions)
            relays = ManagedCohort(
                authority,
                actors,
                output,
                bins / "detent-relay",
                bins / "unused-oracle",
                certificates,
                0,
                4,
                relay=bins / "detent-relay",
                relay_count=1,
                probe_sessions=16,
                probe_active=16,
                source_timeout_ms=600000,
                selection_timeout=SELECTION_TIMEOUT,
                namespaces=namespaces,
                capacity=capacity,
            )
            relay_start = time.monotonic()
            await asyncio.wait_for(relays.start(), 60)
            relay = relays.relays[0].process.pid
            port = relays.relay_ports[0]
            while True:
                if relays.relays[0].process.returncode is not None:
                    raise RuntimeError("relay exited before its mirror was ready")
                try:
                    _, health = await authority.request(0, "/v1/health", endpoint_port=port)
                    if health.get("ready") and health["mirror"]["ready"]:
                        break
                except (OSError, RuntimeError, KeyError):
                    pass
                if time.monotonic() - relay_start > SELECTION_TIMEOUT:
                    raise TimeoutError("relay mirror never ready")
                await asyncio.sleep(0.1)
            mirror_ready = time.monotonic() - relay_start
            print(
                json.dumps(
                    {
                        "authority_ready_s": authority_ready,
                        "relay_mirror_ready_s": mirror_ready,
                        "positions": positions,
                        "relay_health": health,
                    }
                ),
                flush=True,
            )
            sequence = [("prime", "pages"), ("prime", "records")] + [("measured", arm) for arm in ARMS]
            for number, (role, arm) in enumerate(sequence, 1):
                settled, settle_s = await quiet(relay)
                label = f"{number:02d}-{arm}-"
                directory = output / label.rstrip("-")
                directory.mkdir(mode=0o700)
                cohort = ManagedCohort(
                    authority,
                    actors,
                    directory,
                    agents[arm],
                    bins / "unused-oracle",
                    certificates,
                    1,
                    4,
                    source_timeout_ms=600000,
                    selection_timeout=SELECTION_TIMEOUT,
                    namespaces=namespaces,
                    capacity=capacity,
                    label=label,
                )
                cohort.relay_ports = list(relays.relay_ports)
                _, before = await authority.request(0, "/v1/health", endpoint_port=port)
                relay_before = rusage(relay)
                relay_wire_before = nettop(relay)
                start_wall = time.time()
                start = time.monotonic()
                await asyncio.wait_for(cohort.start(), 60)
                agent = cohort.members[0].process.pid
                await cohort.wait_selected(positions)
                ready_s = time.monotonic() - start
                usage = rusage(agent)
                relay_after = rusage(relay)
                agent_wire = nettop(agent)
                relay_wire = nettop(relay)
                _, after = await authority.request(0, "/v1/health", endpoint_port=port)
                await cohort.close()
                lines = actor_lines(events, f"{label}agent-0")
                hashed = last(HASHED, lines)
                transfers = last(TRANSFERS, lines) or [0, 0, 0]
                synced = [t for t, line in lines if SYNCED.search(line)]
                pages_before, pages_after = before["mirror"]["pages"], after["mirror"]["pages"]
                result = {
                    "n": number,
                    "role": role,
                    "arm": arm,
                    "records": records,
                    "ready_s": ready_s,
                    "published_s": synced[0] - start_wall if synced else None,
                    "relay_quiet": settled,
                    "relay_quiet_wait_s": settle_s,
                    "agent_bytes_in": agent_wire[0],
                    "agent_bytes_out": agent_wire[1],
                    "hashed_total": hashed[0] if hashed else None,
                    "hashed_build": hashed[1] if hashed else None,
                    "hashed_check": hashed[2] if hashed else None,
                    "hashed_frames": hashed[3] if hashed else None,
                    "transfers_received": transfers[0],
                    "transfer_bytes": transfers[1],
                    "transfers_refused": transfers[2],
                    "agent_cpu_s": usage["cpu_s"],
                    "agent_disk_written": usage["disk_written"],
                    "agent_peak_footprint": usage["peak_footprint"],
                    "relay_cpu_s": relay_after["cpu_s"] - relay_before["cpu_s"],
                    "relay_bytes_out": relay_wire[1] - relay_wire_before[1],
                    "relay_peak_footprint": relay_after["peak_footprint"],
                    "relay_pages_builds": pages_after["builds"] - pages_before["builds"],
                    "relay_pages_full_copies": pages_after["full_copies"] - pages_before["full_copies"],
                    "relay_pages_build_failures": pages_after["build_failures"],
                }
                print("AGENT " + json.dumps(result, sort_keys=True), flush=True)
                results.append(result)
                if arm == "pages" and (
                    result["transfers_received"] < 1 or result["transfers_refused"] or result["hashed_build"] != 0
                ):
                    raise RuntimeError(f"the pages agent did not boot from pages: {result}")
                if arm == "records" and (result["transfers_received"] or not result["hashed_build"]):
                    raise RuntimeError(f"the records agent did not boot from records: {result}")
                if hashed is None or not synced:
                    raise RuntimeError(f"the agent logged no counters: {result}")
            await relays.close()
    finally:
        evidence.close()
        (output / "results.json").write_text(json.dumps(results, indent=1))
    return results


if __name__ == "__main__":
    asyncio.run(run())
