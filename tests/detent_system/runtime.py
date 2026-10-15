"""Bounded process ownership and evidence capture for system qualification."""

from __future__ import annotations

import asyncio
import json
import resource
import shutil
from builtins import ExceptionGroup
from dataclasses import dataclass
from pathlib import Path
from typing import Any, BinaryIO, Self


@dataclass(frozen=True)
class Scenario:
    name: str
    records: int
    producers: int
    subscribers: int
    durable_agents: int
    duration: int
    artifact_bytes: int = 64 * 1024 * 1024
    shutdown_seconds: int = 30

    @classmethod
    def load(cls, path: Path) -> Scenario:
        if path.stat().st_size > 65536:
            raise ValueError("scenario exceeds 64 KiB")

        def unique_fields(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
            result: dict[str, Any] = {}
            for key, value in pairs:
                if key in result:
                    raise ValueError(f"duplicate scenario field: {key}")
                result[key] = value
            return result

        data: Any = json.loads(path.read_text(), object_pairs_hook=unique_fields)
        required = {
            "name",
            "records",
            "producers",
            "subscribers",
            "durable_agents",
            "duration",
        }
        optional = {"artifact_bytes", "shutdown_seconds"}
        if (
            not isinstance(data, dict)
            or not required <= data.keys()
            or data.keys() - required - optional
        ):
            raise ValueError("unknown or missing scenario fields")
        if (
            not isinstance(data["name"], str)
            or not data["name"]
            or len(data["name"]) > 64
        ):
            raise ValueError("invalid scenario name")
        bounds = {
            "records": (0, 500000),
            "producers": (1, 256),
            "subscribers": (0, 50000),
            "durable_agents": (1, 8),
            "duration": (1, 86400),
            "artifact_bytes": (4096, 1024**3),
            "shutdown_seconds": (1, 60),
        }
        for key, (minimum, maximum) in bounds.items():
            if key in data and (
                type(data[key]) is not int or not minimum <= data[key] <= maximum
            ):
                raise ValueError(f"invalid scenario {key}")
        return cls(**data)

    def preflight(self, output: Path) -> dict[str, int]:
        descriptors = 512 + 2 * self.subscribers + 2 * self.producers
        soft, hard = resource.getrlimit(resource.RLIMIT_NOFILE)
        if soft != resource.RLIM_INFINITY and soft < descriptors:
            raise ValueError(
                f"FD limit {soft} is below required {descriptors}; hard limit {hard}"
            )
        parent = output.resolve().parent
        while not parent.exists():
            parent = parent.parent
        available = shutil.disk_usage(parent).free
        # Includes two roots per durable participant and three authority copies.
        disk = (self.records * 512 + 4 * 1024 * 1024) * (
            2 * (self.durable_agents + 2) + 3
        )
        disk += self.artifact_bytes
        if available < disk:
            raise ValueError(
                f"free disk {available} is below conservative required {disk}"
            )
        return {
            "required_fds": descriptors,
            "soft_fds": soft,
            "hard_fds": hard,
            "required_disk_bytes": disk,
            "available_disk_bytes": available,
        }


class Evidence:
    def __init__(self, path: Path, maximum_bytes: int) -> None:
        if maximum_bytes < 4096:
            raise ValueError("evidence budget must be at least 4 KiB")
        self.stream: BinaryIO = path.open("xb")
        self.maximum_bytes = maximum_bytes
        self.used = 0
        self.dropped = 0

    def record(self, kind: str, **fields: Any) -> None:
        payload = json.dumps({"kind": kind, **fields}, sort_keys=True).encode() + b"\n"
        if len(payload) > 16384:
            raise ValueError("evidence record exceeds 16 KiB")
        ceiling = self.maximum_bytes
        if kind == "actor_output":
            ceiling -= min(65536, self.maximum_bytes // 4)
        if len(payload) > ceiling - self.used:
            if kind != "actor_output":
                raise ValueError("critical evidence exceeds artifact budget")
            self.dropped += 1
            return
        self.stream.write(payload)
        self.stream.flush()
        self.used += len(payload)

    def close(self) -> None:
        self.stream.close()


@dataclass
class Actor:
    name: str
    process: asyncio.subprocess.Process
    capture: asyncio.Task[None]


class Actors:
    def __init__(
        self, evidence: Evidence, shutdown_seconds: int, maximum: int = 16
    ) -> None:
        self.evidence = evidence
        self.shutdown_seconds = shutdown_seconds
        self.maximum = maximum
        self.children: list[Actor] = []
        self.launch_lock = asyncio.Lock()

    async def __aenter__(self) -> Self:
        return self

    async def __aexit__(self, *args: object) -> None:
        cleanup = asyncio.create_task(self.close())
        cancellation: asyncio.CancelledError | None = None
        while not cleanup.done():
            try:
                await asyncio.shield(cleanup)
            except asyncio.CancelledError as error:
                cancellation = cancellation or error
        cleanup.result()
        if cancellation is not None:
            raise cancellation

    async def start(self, name: str, command: list[str]) -> Actor:
        async with self.launch_lock:
            return await self._start(name, command)

    async def _start(self, name: str, command: list[str]) -> Actor:
        if len(self.children) >= self.maximum or any(
            actor.name == name for actor in self.children
        ):
            raise ValueError("actor count or duplicate name")
        launch = asyncio.create_task(
            asyncio.create_subprocess_exec(
                *command,
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.STDOUT,
                limit=8192,
            )
        )
        failure: asyncio.CancelledError | None = None
        while not launch.done():
            try:
                await asyncio.shield(launch)
            except asyncio.CancelledError as error:
                failure = failure or error
        process = launch.result()
        capture = asyncio.create_task(self._capture(name, process))
        actor = Actor(name, process, capture)
        self.children.append(actor)
        self.evidence.record("actor_started", name=name, pid=process.pid)
        if failure is not None:
            raise failure
        return actor

    async def _capture(self, name: str, process: asyncio.subprocess.Process) -> None:
        assert process.stdout is not None
        failure: Exception | None = None
        while payload := await process.stdout.read(1024):
            if failure is None:
                try:
                    self.evidence.record(
                        "actor_output", name=name, text=payload.decode(errors="replace")
                    )
                except (OSError, ValueError) as error:
                    failure = error
        if failure is not None:
            raise failure

    async def stop(self, selected: list[Actor]) -> None:
        if len({actor.name for actor in selected}) != len(selected) or any(
            actor not in self.children for actor in selected
        ):
            raise ValueError("cannot stop an unowned or duplicate actor")
        await self._close(selected.copy())

    async def close(self) -> None:
        await self._close(self.children.copy())

    async def _close(self, selected: list[Actor]) -> None:
        for actor in reversed(selected):
            if actor.process.returncode is None:
                try:
                    actor.process.terminate()
                except ProcessLookupError:
                    pass
        failures: list[Exception] = []
        forced = False
        for actor in reversed(selected):
            try:
                await asyncio.wait_for(actor.process.wait(), self.shutdown_seconds)
            except TimeoutError:
                forced = True
                try:
                    actor.process.kill()
                except ProcessLookupError:
                    pass
                await actor.process.wait()
            try:
                await actor.capture
            except (OSError, ValueError) as error:
                failures.append(error)
            try:
                self.evidence.record(
                    "actor_stopped", name=actor.name, code=actor.process.returncode
                )
            except (OSError, ValueError) as error:
                failures.append(error)
        for actor in selected:
            self.children.remove(actor)
        if forced:
            failures.append(
                RuntimeError(
                    "actor exceeded authentic shutdown deadline and was killed"
                )
            )
        if failures:
            raise ExceptionGroup("actor drainage failed", failures)
