"""Durable managed-agent cohort and an independent SDK oracle process."""

from __future__ import annotations

import asyncio
import hashlib
import json
import socket
import struct
import time
from collections.abc import Callable
from pathlib import Path

from authority import CONTEXT, READER_PRINCIPAL, READER_TOKEN, Authority
from capacity import CapacityProfile
from runtime import Actor, Actors
from writers import Writers

RELAY_TOKEN = "cd" * 16


def table_positions(table: bytes) -> dict[str, int] | None:
    """Each namespace's W1 position in the agent store's `selected` table (DTTABLE1, D8)."""
    if (
        len(table) < 64 + 32
        or table[:8] != b"DTTABLE1"
        or hashlib.sha256(table[:-32]).digest() != table[-32:]
    ):
        return None
    positions: dict[str, int] = {}
    at = 64
    for _ in range(int.from_bytes(table[12:16], "big")):
        length = table[at]
        name = table[at + 1 : at + 1 + length].decode()
        position = at + 1 + length + 32
        positions[name] = int.from_bytes(table[position : position + 8], "big")
        at += 137 + length
    return positions


class ManagedCohort:
    def __init__(
        self,
        authority: Authority,
        actors: Actors,
        directory: Path,
        binary: Path,
        oracle: Path,
        certificates: Path,
        count: int,
        producers: int,
        relay: Path | None = None,
        probe_sessions: int = 0,
        probe_active: int = 0,
        source_timeout_ms: int = 2000,
        selection_timeout: float = 30,
        namespaces: list[str] | None = None,
        capacity: CapacityProfile | None = None,
        relay_count: int = 2,
        label: str = "",
        observe: Callable[[int, bytes], None] | None = None,
    ) -> None:
        self.authority = authority
        self.capacity = capacity
        if relay_count not in (1, 2):
            raise ValueError("invalid relay cohort size")
        self.relay_count = relay_count
        self.actors = actors
        self.directory = directory
        self.binary = binary
        self.oracle = oracle
        self.certificates = certificates
        self.count = count
        self.producers = producers
        self.namespaces = (
            sorted(set(namespaces))
            if namespaces is not None
            else sorted(f"load{index}" for index in range(producers))
        )
        self.relay = relay
        if not 0 <= probe_sessions <= 50000 or not 0 <= probe_active <= probe_sessions:
            raise ValueError("invalid relay probe admission")
        if probe_sessions and not probe_active:
            raise ValueError("probe sessions require active exchange admission")
        if not 1 <= source_timeout_ms <= 86400000 or not 1 <= selection_timeout <= 3600:
            raise ValueError("invalid managed fixture deadlines")
        self.probe_sessions = probe_sessions
        self.probe_active = probe_active
        self.source_timeout_ms = source_timeout_ms
        self.selection_timeout = selection_timeout
        self.source_paths: list[Path] = []
        self.relays: list[Actor] = []
        self.relay_ports: list[int] = []
        self.members: list[Actor] = []
        # Actor names carry `label`, so cohorts share one Actors; `observe` sees each agent's output.
        self.label = label
        self.observe = observe

    def relay_configuration(
        self, directory: Path, port: int, upstreams: list[dict[str, object]]
    ) -> dict[str, object]:
        mirror: dict[str, object] = {
            "directory": str(directory / "mirror"),
            "cluster": CONTEXT["cluster"],
            "namespaces": self.namespaces,
            "upstreams": upstreams,
            "hostname": "localhost",
            "ca": str(self.certificates / "ca.pem"),
            "token": READER_TOKEN,
            "interval_ms": 10,
            "reconnect_maximum_ms": 100,
            "timeout_ms": self.source_timeout_ms,
        }
        configuration: dict[str, object] = {
            "address": self.authority.address,
            "port": port,
            "hostname": "localhost",
            "certificate": str(self.certificates / "one.pem"),
            "private_key": str(self.certificates / "one.key"),
            "ca": str(self.certificates / "ca.pem"),
            "mirror": mirror,
            "credentials": [
                {
                    "token": RELAY_TOKEN,
                    "principal": READER_PRINCIPAL,
                    "namespaces": self.namespaces,
                }
            ],
        }
        if self.probe_sessions:
            active = (self.probe_active + self.relay_count - 1) // self.relay_count
            mirror["sessions"] = min(
                65536,
                (self.probe_sessions + self.relay_count - 1) // self.relay_count
                + self.count
                + 32,
            )
            configuration.update(
                connections=active + 128,
                control_bytes=max(64 * 1024 * 1024, (active + 128) * 65536),
                service_limits={
                    "opens": min(256, active + 64),
                    "closes": 64,
                    "polls": active + 64,
                },
            )
        return configuration

    async def start(self) -> None:
        leader = await self.authority.leader()
        first = (leader + 1) % 3
        endpoints: list[dict[str, object]] = [
            {
                "address": self.authority.address,
                "port": self.authority.ports[3 + (first + offset) % 3],
            }
            for offset in range(3)
        ]
        self.actors.evidence.record(
            "managed_initial_source", follower=first, leader=leader
        )
        relay_ports = self.relay_ports
        if self.relay is not None:
            for index in range(self.relay_count):
                directory = self.directory / f"relay-{index}"
                directory.mkdir(mode=0o700)
                with socket.socket(
                    socket.AF_INET6 if ":" in self.authority.address else socket.AF_INET
                ) as reservation:
                    reservation.bind((self.authority.address, 0))
                    port = reservation.getsockname()[1]
                relay_ports.append(port)
                config = directory / "relay.json"
                config.write_text(
                    json.dumps(self.relay_configuration(directory, port, endpoints))
                )
                config.chmod(0o600)
                self.relays.append(
                    await self.actors.start(
                        f"{self.label}relay-{index}",
                        [
                            str(self.relay),
                            "--config",
                            str(config),
                            "--runtime-dir",
                            str(directory / "run"),
                        ],
                    )
                )
        for index in range(self.count):
            directory = self.directory / f"agent-{index}"
            directory.mkdir(mode=0o700)
            source: dict[str, object] = {
                "cluster": CONTEXT["cluster"],
                "namespaces": self.namespaces,
                "token": READER_TOKEN,
                "authority_endpoints": endpoints,
                "hostname": "localhost",
                "ca": str(self.certificates / "ca.pem"),
                "compaction_interval_ms": 0,
                "quorum_refresh_ms": 1000,
                "retry_interval_ms": 10,
                "reconnect_maximum_ms": 100,
                "timeout_ms": self.source_timeout_ms,
            }
            if self.capacity is not None:
                source.update(self.capacity.managed())
            if relay_ports:
                source.pop("authority_endpoints")
                source.pop("quorum_refresh_ms")
                source.update(
                    address=self.authority.address,
                    port=relay_ports[index % len(relay_ports)],
                    token=RELAY_TOKEN,
                )
            managed = directory / "managed.json"
            managed.write_text(json.dumps(source))
            managed.chmod(0o600)
            self.source_paths.append(managed)
            configuration = {
                "managed_path": str(managed),
                "generation_dir": str(directory / "generations"),
                "runtime_dir": str(directory / "run"),
            }
            config = directory / "agent.json"
            config.write_text(json.dumps(configuration))
            config.chmod(0o644)
            self.members.append(await self.start_agent(index, initialize=True))

    async def start_agent(self, index: int, *, initialize: bool) -> Actor:
        """Agent `index` on its configuration; a restart opens the store it holds."""
        config = self.directory / f"agent-{index}" / "agent.json"
        observe = self.observe

        def output(payload: bytes) -> None:
            if observe is not None:
                observe(index, payload)

        return await self.actors.start(
            f"{self.label}agent-{index}",
            [str(self.binary), "--config", str(config)]
            + (["--log-level", "info"] if observe is not None else [])
            + (["--initialize-managed"] if initialize else []),
            observe=output,
        )

    async def restart_agent(self, index: int) -> None:
        await self.actors.stop([self.members[index]])
        self.members[index] = await self.start_agent(index, initialize=False)

    def positions(self, writers: Writers) -> dict[str, int]:
        """The writers' position in each followed namespace (D3: its own counter)."""
        positions = dict.fromkeys(self.namespaces, 0)
        for producer, revision in enumerate(writers.positions):
            name = self.authority.namespace(producer)
            if name in positions:
                positions[name] = max(positions[name], revision)
        return positions

    async def wait_selected(self, positions: dict[str, int]) -> None:
        """Wait until every agent's entry for each name reaches its position."""
        for index, member in enumerate(self.members):
            directory = self.directory / f"agent-{index}" / "generations"
            deadline = time.monotonic() + self.selection_timeout
            behind: dict[str, int | None] = {}
            while True:
                if member.process.returncode is not None:
                    raise RuntimeError(
                        f"managed agent {index} exited before convergence"
                    )
                try:
                    table = table_positions((directory / "selected").read_bytes())
                except FileNotFoundError:
                    table = None
                if table is not None:
                    behind = {
                        name: table.get(name)
                        for name, position in positions.items()
                        if position and table.get(name, 0) < position
                    }
                    if not behind:
                        break
                if time.monotonic() >= deadline:
                    raise RuntimeError(
                        f"managed agent {index} did not converge: {behind} "
                        f"of {positions}"
                    )
                await asyncio.sleep(0.05)

    async def verify(self, writers: Writers) -> None:
        positions = self.positions(writers)
        expected = self.directory / "expected-values.bin"
        expected.unlink(missing_ok=True)
        with expected.open("xb") as stream:
            stream.write(struct.pack("<QQ", writers.records, writers.producers))
            stream.writelines(
                struct.pack("<Q", positions[self.authority.namespace(producer)])
                for producer in range(writers.producers)
            )
            stream.writelines(struct.pack("<q", value) for value in writers.values)
        await self.wait_selected(positions)
        for index in range(len(self.members)):
            oracle_started = time.monotonic()
            directory = self.directory / f"agent-{index}" / "generations"
            verifier = await self.actors.start(
                f"{self.label}oracle-{index}",
                [
                    str(self.oracle),
                    str(directory),
                    str(expected),
                    CONTEXT["cluster"],
                    *(self.capacity.oracle_arguments() if self.capacity else []),
                ],
            )
            await asyncio.wait_for(verifier.process.wait(), 60)
            await verifier.capture
            if verifier.process.returncode:
                raise RuntimeError(f"independent SDK oracle rejected agent {index}")
            self.actors.evidence.record(
                "managed_verified",
                oracle_process_seconds=time.monotonic() - oracle_started,
                index=index,
                records=writers.records,
                positions=positions,
            )
            self.actors.children.remove(verifier)

    async def close(self) -> None:
        await self.actors.stop(self.members)
        await self.actors.stop(self.relays)
        if any(member.process.returncode != 0 for member in self.members + self.relays):
            raise RuntimeError("managed cohort did not drain cleanly")
