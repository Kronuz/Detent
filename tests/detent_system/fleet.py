"""Bound native probe control, resource admission and result validation."""

from __future__ import annotations

import asyncio
import json
import math
import re
import sys
from pathlib import Path
from typing import Any

from evidence import command, physical_memory
from runtime import Actor


class FleetOutput:
    def __init__(self, *, allow_failure: bool = False) -> None:
        self.buffer = bytearray()
        self.allow_failure = allow_failure
        self.diagnostic: str | None = None
        self.ready = asyncio.Event()
        self.measurement_started = asyncio.Event()
        self.measurement_stopped = asyncio.Event()
        self.failure: Exception | None = None
        self.latest: dict[str, int] = {}
        self.checkpoint_revision = 0

    def feed(self, payload: bytes) -> None:
        try:
            self.buffer.extend(payload)
            while b"\n" in self.buffer:
                line, _, remaining = self.buffer.partition(b"\n")
                self.buffer = remaining
                if len(line) > 4096:
                    raise ValueError("fleet output line exceeds 4 KiB")
                if line.startswith(b"fleet: "):
                    if self.allow_failure and self.diagnostic is None:
                        self.diagnostic = line.decode(errors="replace")
                        continue
                    raise ValueError(line.decode(errors="replace"))
                data: Any = json.loads(line)
                if not isinstance(data, dict) or data.get("event") not in {
                    "ready",
                    "sample",
                    "checkpoint",
                    "measurement",
                }:
                    raise ValueError("invalid fleet output event")
                for key, value in data.items():
                    if key == "event":
                        continue
                    if key == "warm" and type(value) is bool:
                        continue
                    if type(value) is not int or not 0 <= value <= 2**64 - 1:
                        raise ValueError("invalid fleet output counter")
                if data["event"] == "ready":
                    if type(data.get("sessions")) is not int or data["sessions"] < 1:
                        raise ValueError("invalid fleet ready population")
                    self.ready.set()
                elif data["event"] == "checkpoint":
                    if type(data.get("revision")) is not int or data["revision"] < 1:
                        raise ValueError("invalid fleet checkpoint revision")
                    self.checkpoint_revision = data["revision"]
                elif data["event"] == "measurement":
                    running = data.get("running")
                    if running == 1 and not self.measurement_started.is_set():
                        self.measurement_started.set()
                    elif (
                        running == 0
                        and self.measurement_started.is_set()
                        and not self.measurement_stopped.is_set()
                    ):
                        self.measurement_stopped.set()
                    else:
                        raise ValueError("invalid fleet measurement transition")
                else:
                    self.latest = {
                        key: value for key, value in data.items() if type(value) is int
                    }
            if len(self.buffer) > 4096:
                raise ValueError("fleet output line exceeds 4 KiB")
        except (ValueError, TypeError) as error:
            self.failure = error
            self.ready.set()
            raise

    async def wait_ready(self, actor: Actor, timeout: float) -> None:
        await self.wait_event(actor, self.ready, timeout)

    async def wait_event(
        self, actor: Actor, event: asyncio.Event, timeout: float
    ) -> None:
        deadline = asyncio.get_running_loop().time() + timeout
        while not event.is_set():
            if self.failure is not None:
                raise self.failure
            if actor.process.returncode is not None:
                await actor.capture
                raise RuntimeError("fleet exited before readiness")
            if asyncio.get_running_loop().time() >= deadline:
                raise RuntimeError("fleet bootstrap deadline exceeded")
            await asyncio.sleep(0.05)
        if self.failure is not None:
            raise self.failure


def available_memory() -> int:
    if sys.platform.startswith("linux"):
        for line in Path("/proc/meminfo").read_text().splitlines():
            if line.startswith("MemAvailable:"):
                return int(line.split()[1]) * 1024
        raise ValueError("kernel does not report available memory")
    if sys.platform == "darwin":
        report = command(["vm_stat"])
        page = re.search(r"page size of (\d+) bytes", report)
        if page is None:
            raise ValueError("vm_stat lacks page size")
        counts = {
            name: int(value)
            for name, value in re.findall(
                r"^(Pages [^:]+):\s+(\d+)\.", report, re.MULTILINE
            )
        }
        return int(page[1]) * sum(
            counts[name]
            for name in ("Pages free", "Pages inactive", "Pages speculative")
        )
    if sys.platform.startswith("freebsd"):
        counts = command(
            [
                "sysctl",
                "-n",
                "vm.stats.vm.v_free_count",
                "vm.stats.vm.v_inactive_count",
                "hw.pagesize",
            ]
        ).split()
        return (int(counts[0]) + int(counts[1])) * int(counts[2])
    raise ValueError("fleet memory preflight supports Linux, macOS and FreeBSD")


def parse_cpu_time(value: str) -> float:
    """Read BSD fractional minutes and POSIX day/hour process CPU times."""
    prefix, separator, clock = value.partition("-")
    days = int(prefix) if separator else 0
    parts = (clock if separator else prefix).split(":")
    if len(parts) not in (2, 3):
        raise ValueError("invalid process CPU time")
    hours = int(parts[0]) if len(parts) == 3 else 0
    minutes = int(parts[-2])
    seconds = float(parts[-1])
    if (
        days < 0
        or hours < 0
        or minutes < 0
        or (len(parts) == 3 and minutes >= 60)
        or not math.isfinite(seconds)
        or not 0 <= seconds < 60
    ):
        raise ValueError("invalid process CPU time")
    return days * 86400 + hours * 3600 + minutes * 60 + seconds


def process_cpu_seconds(actors: list[Actor]) -> dict[str, float]:
    names = {
        actor.process.pid: actor.name
        for actor in actors
        if actor.process.returncode is None
    }
    if not names:
        return {}
    report = command(["ps", "-o", "pid=,time=", "-p", ",".join(map(str, names))])
    result: dict[str, float] = {}
    for line in report.splitlines():
        pid, clock = line.split()
        name = names[int(pid)]
        if name in result:
            raise ValueError("duplicate process CPU report")
        result[name] = parse_cpu_time(clock)
    if set(result) != set(names.values()):
        raise RuntimeError("incomplete process CPU report")
    return result


def resident_bytes(actors: list[Actor]) -> int:
    pids = [actor.process.pid for actor in actors if actor.process.returncode is None]
    if not pids:
        return 0
    report = command(["ps", "-o", "rss=", "-p", ",".join(map(str, pids))])
    return sum(int(line) * 1024 for line in report.splitlines())


def admission(
    subscribers: int, active: int, namespaces: int, baseline_rss: int = 0
) -> dict[str, int]:
    if not 1 <= active <= subscribers <= 50000 or not 0 <= namespaces <= 4096:
        raise ValueError("invalid fleet population")
    heap = 256 * 1024 * 1024 + (active + 64) * (512 * 1024 + namespaces * 512)
    heap += subscribers * (16384 + namespaces * 1024)
    heap = ((heap + 64 * 1024 * 1024 - 1) // (64 * 1024 * 1024)) * 64 * 1024 * 1024
    # TLS and socket capacity are outside the production admission ledger.
    transport = (active + 64) * 512 * 1024
    physical = physical_memory()
    available = available_memory()
    ceiling = min(24 * 1024**3, max(0, physical - 8 * 1024**3))
    additional = heap + transport
    required = baseline_rss * 2 + additional
    if required > ceiling or additional > available * 4 // 5:
        raise ValueError(
            f"fleet memory preflight refused {required} bytes; "
            f"workload ceiling {ceiling}, available estimate {available}"
        )
    return {
        "probe_heap_bytes": heap,
        "transport_reserve_bytes": transport,
        "baseline_rss_bytes": baseline_rss,
        "required_workload_bytes": required,
        "workload_ceiling_bytes": ceiling,
        "available_memory_estimate_bytes": available,
        "physical_memory_bytes": physical,
    }


def load_result(path: Path, subscribers: int, revision: int) -> dict[str, Any]:
    if not path.is_file() or path.stat().st_size > 65536:
        raise ValueError("fleet result is missing or exceeds 64 KiB")
    data: Any = json.loads(path.read_bytes())
    if not isinstance(data, dict) or data.get("success") is not True:
        raise ValueError("fleet result did not succeed")
    for key, expected in {
        "transport_probes": subscribers,
        "durable_stores": 0,
        "accepted": subscribers,
        "completed": subscribers,
        "opened": subscribers,
        "ready": subscribers,
        "checked": subscribers,
        "checkpoint_revision": revision,
        "premature_ready_polls": 0,
    }.items():
        if type(data.get(key)) is not int or data[key] != expected:
            raise ValueError(f"fleet result counter mismatch: {key}")
    return data
