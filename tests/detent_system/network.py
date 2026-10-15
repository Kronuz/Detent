"""Bounded TLS-transparent peer links for process-level partition checks."""

from __future__ import annotations

import asyncio
import json
from builtins import BaseExceptionGroup, ExceptionGroup
from pathlib import Path


class PeerLink:
    def __init__(self, target: int) -> None:
        self.target = target
        self.blocked = False
        self.server: asyncio.Server | None = None
        self.tasks: set[asyncio.Task[None]] = set()
        self.writers: set[asyncio.StreamWriter] = set()
        self.failure: BaseException | None = None

    async def start(self) -> int:
        original = asyncio.create_task(
            asyncio.start_server(self.accepted, "127.0.0.1", 0, limit=8192, backlog=4)
        )
        cancellation: asyncio.CancelledError | None = None
        while not original.done():
            try:
                await asyncio.shield(original)
            except asyncio.CancelledError as error:
                cancellation = cancellation or error
        self.server = original.result()
        if cancellation is not None:
            raise cancellation
        return int(self.server.sockets[0].getsockname()[1])

    def accepted(
        self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter
    ) -> None:
        if len(self.tasks) >= 4:
            self.failure = RuntimeError("peer-link connection admission exceeded")
            assert self.server is not None
            self.server.close()
        task = asyncio.create_task(self.forward(reader, writer))
        self.tasks.add(task)
        task.add_done_callback(self.finished)

    def finished(self, task: asyncio.Task[None]) -> None:
        self.tasks.discard(task)
        if task.cancelled():
            self.failure = self.failure or RuntimeError(
                "peer-link original was cancelled"
            )
        elif failure := task.exception():
            self.failure = self.failure or failure

    async def forward(
        self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter
    ) -> None:
        owned = [writer]
        self.writers.add(writer)
        try:
            if self.blocked or self.failure is not None:
                return
            upstream, output = await asyncio.wait_for(
                asyncio.open_connection("127.0.0.1", self.target, limit=8192), 2
            )
            owned.append(output)
            self.writers.add(output)
            for stream in owned:
                stream.transport.set_write_buffer_limits(high=16384, low=4096)
            if self.blocked:
                return

            async def copy(
                source: asyncio.StreamReader, destination: asyncio.StreamWriter
            ) -> None:
                try:
                    while payload := await source.read(8192):
                        destination.write(payload)
                        await destination.drain()
                finally:
                    for stream in owned:
                        stream.close()

            async with asyncio.TaskGroup() as group:
                group.create_task(copy(reader, output))
                group.create_task(copy(upstream, writer))
        except (ConnectionError, OSError, TimeoutError):
            pass
        except ExceptionGroup as errors:
            _, remaining = errors.split((ConnectionError, OSError, TimeoutError))
            if remaining is not None:
                raise remaining
        finally:
            for stream in owned:
                stream.close()
            for stream in owned:
                try:
                    await stream.wait_closed()
                except (ConnectionError, OSError):
                    pass
                self.writers.discard(stream)

    async def block(self) -> None:
        self.blocked = True
        for writer in self.writers:
            writer.close()
        if self.tasks:
            await asyncio.gather(*tuple(self.tasks))

    async def close(self) -> None:
        if self.server is not None:
            self.server.close()
        await self.block()
        if self.server is not None:
            await self.server.wait_closed()
        if self.failure is not None:
            raise self.failure


class PeerMesh:
    def __init__(self) -> None:
        self.links: dict[tuple[int, int], PeerLink] = {}

    async def start(self, directory: Path, ports: list[int]) -> None:
        try:
            for source in range(3):
                path = directory / f"authority-{source}.json"
                configuration = json.loads(path.read_text())
                for target in range(3):
                    if target == source:
                        continue
                    link = PeerLink(ports[target])
                    self.links[source, target] = link
                    configuration["voters"][target]["port"] = await link.start()
                path.write_text(json.dumps(configuration))
        except BaseException:
            await self.close()
            raise

    async def isolate(self, index: int) -> None:
        async with asyncio.TaskGroup() as group:
            for (source, target), link in self.links.items():
                if index in (source, target):
                    group.create_task(link.block())

    def heal(self, index: int) -> None:
        for (source, target), link in self.links.items():
            if index in (source, target):
                link.blocked = False

    async def close(self) -> None:
        original = asyncio.create_task(self._close())
        cancellation: asyncio.CancelledError | None = None
        while not original.done():
            try:
                await asyncio.shield(original)
            except asyncio.CancelledError as error:
                cancellation = cancellation or error
        original.result()
        if cancellation is not None:
            raise cancellation

    async def _close(self) -> None:
        results = await asyncio.gather(
            *(link.close() for link in self.links.values()), return_exceptions=True
        )
        failures = [result for result in results if isinstance(result, BaseException)]
        self.links.clear()
        if failures:
            raise BaseExceptionGroup("peer mesh drainage failed", failures)
