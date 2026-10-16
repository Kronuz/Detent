"""Durable managed-agent cohort and an independent SDK oracle process."""

from __future__ import annotations

import asyncio
import hashlib
import json
import socket
import time
from pathlib import Path

from authority import CONTEXT, READER_PRINCIPAL, READER_TOKEN, Authority
from runtime import Actor, Actors
from writers import Writers


def scope_identity(producers: int, namespaces: list[str] | None = None) -> str:
    principal = READER_PRINCIPAL.encode()
    names = sorted(
        {name.encode() for name in namespaces}
        if namespaces is not None
        else {f"load{index}".encode() for index in range(producers)}
    )
    digest = hashlib.sha256()
    digest.update(b"detent-authorized-scope-v1")
    digest.update(len(principal).to_bytes(8, "big") + principal)
    digest.update((1).to_bytes(8, "big") + len(names).to_bytes(8, "big"))
    for name in names:
        digest.update(len(name).to_bytes(8, "big") + name)
    return digest.hexdigest()


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
        delivery_mode: str = "subscription",
        probe_sessions: int = 0,
        probe_active: int = 0,
        source_timeout_ms: int = 2000,
        selection_timeout: float = 30,
        namespaces: list[str] | None = None,
    ) -> None:
        self.authority = authority
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
            else [f"load{index}" for index in range(producers)]
        )
        self.relay = relay
        self.delivery_mode = delivery_mode
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
        self.scope = scope_identity(producers, self.namespaces)
        self.relay_ports: list[int] = []
        self.members: list[Actor] = []

    async def start(self) -> None:
        leader = await self.authority.leader()
        first = (leader + 1) % 3
        endpoints = [
            {
                "address": "127.0.0.1",
                "port": self.authority.ports[3 + (first + offset) % 3],
            }
            for offset in range(3)
        ]
        self.actors.evidence.record(
            "managed_initial_source", follower=first, leader=leader
        )
        relay_ports = self.relay_ports
        if self.relay is not None:
            if self.delivery_mode != "subscription":
                raise ValueError("relay consumers require subscription delivery")
            for index in range(2):
                directory = self.directory / f"relay-{index}"
                directory.mkdir(mode=0o700)
                upstream = CONTEXT | {
                    "principal": READER_PRINCIPAL,
                    "policy_epoch": 1,
                    "namespaces": self.namespaces,
                    "token": READER_TOKEN,
                    "authority_endpoints": endpoints,
                    "hostname": "localhost",
                    "ca": str(self.certificates / "ca.pem"),
                    "delivery_mode": "subscription",
                    "image_format": "paged-v2",
                    "quorum_refresh_ms": 1000,
                    "poll_interval_ms": 10,
                    "reconnect_maximum_ms": 100,
                    "timeout_ms": self.source_timeout_ms,
                }
                upstream_path = directory / "upstream.json"
                upstream_path.write_text(json.dumps(upstream))
                upstream_path.chmod(0o600)
                with socket.socket() as reservation:
                    reservation.bind(("127.0.0.1", 0))
                    port = reservation.getsockname()[1]
                relay_ports.append(port)
                configuration = {
                    "address": "127.0.0.1",
                    "port": port,
                    "hostname": "localhost",
                    "certificate": str(self.certificates / "one.pem"),
                    "private_key": str(self.certificates / "one.key"),
                    "ca": str(self.certificates / "ca.pem"),
                    "bundles": [
                        {
                            "source": str(upstream_path),
                            "directory": str(directory / "generations"),
                            "mode": "fresh",
                        }
                    ],
                    "credentials": [
                        {
                            "token": "cd" * 16,
                            "principal": READER_PRINCIPAL,
                            "policy_epoch": 1,
                            "bundles": [0],
                        }
                    ],
                }
                if self.probe_sessions:
                    sessions = (self.probe_sessions + 1) // 2 + self.count + 32
                    active = (self.probe_active + 1) // 2
                    configuration.update(
                        connections=active + 128,
                        control_bytes=max(64 * 1024 * 1024, (active + 128) * 65536),
                        service_limits={
                            "opens": 64,
                            "closes": 64,
                            "polls": active + 64,
                            "bootstrap_transfers": min(128, active + 64),
                            "catchup_transfers": min(128, active + 64),
                            "sessions": sessions,
                            "per_principal": sessions,
                            "idle_ms": 3600000,
                            "maximum_ms": 86400000,
                        },
                    )
                config = directory / "relay.json"
                config.write_text(json.dumps(configuration))
                config.chmod(0o600)
                self.relays.append(
                    await self.actors.start(
                        f"relay-{index}",
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
            source = CONTEXT | {
                "principal": READER_PRINCIPAL,
                "policy_epoch": 1,
                "namespaces": self.namespaces,
                "token": READER_TOKEN,
                "authority_endpoints": endpoints,
                "hostname": "localhost",
                "ca": str(self.certificates / "ca.pem"),
                "delivery_mode": self.delivery_mode,
                "image_format": "paged-v2",
                "compaction_interval_ms": 0,
                "quorum_refresh_ms": 1000,
                "poll_interval_ms": 10,
                "reconnect_maximum_ms": 100,
                "timeout_ms": self.source_timeout_ms,
            }
            if relay_ports:
                source.pop("authority_endpoints")
                source.pop("quorum_refresh_ms")
                source.update(
                    address="127.0.0.1",
                    port=relay_ports[index % len(relay_ports)],
                    token="cd" * 16,
                )
            managed = directory / "managed.json"
            managed.write_text(json.dumps(source))
            managed.chmod(0o600)
            self.source_paths.append(managed)
            configuration = {
                "managed_path": str(managed),
                "generation_dir": str(directory / "generations"),
                "runtime_dir": str(directory / "run"),
                **{f"generation_{name}": value for name, value in CONTEXT.items()},
                "generation_scope": self.scope,
            }
            config = directory / "agent.json"
            config.write_text(json.dumps(configuration))
            config.chmod(0o644)
            self.members.append(
                await self.actors.start(
                    f"agent-{index}",
                    [str(self.binary), "--config", str(config), "--initialize-managed"],
                )
            )

    async def wait_selected(self, revision: int) -> None:
        for index, member in enumerate(self.members):
            directory = self.directory / f"agent-{index}" / "generations"
            deadline = time.monotonic() + self.selection_timeout
            while True:
                if member.process.returncode is not None:
                    raise RuntimeError(
                        f"managed agent {index} exited before convergence"
                    )
                try:
                    with (directory / "selected").open("rb") as stream:
                        selected = stream.read(177)
                    if (
                        len(selected) == 176
                        and selected[:8] == b"DTSELECT"
                        and int.from_bytes(selected[152:160], "big") >= revision
                    ):
                        break
                except FileNotFoundError:
                    pass
                if time.monotonic() >= deadline:
                    raise RuntimeError(f"managed agent {index} did not converge")
                await asyncio.sleep(0.05)

    async def verify(self, writers: Writers) -> None:
        revision = max(
            int(receipt["revision"])
            for receipt in writers.receipts
            if receipt is not None
        )
        expected = self.directory / "expected-values.bin"
        expected.unlink(missing_ok=True)
        with expected.open("xb") as stream:
            writers.write_expected(stream, revision)
        await self.wait_selected(revision)
        for index in range(len(self.members)):
            directory = self.directory / f"agent-{index}" / "generations"
            verifier = await self.actors.start(
                f"oracle-{index}",
                [
                    str(self.oracle),
                    str(directory),
                    str(expected),
                    CONTEXT["cluster"],
                    CONTEXT["group"],
                    CONTEXT["epoch"],
                    self.scope,
                ],
            )
            await asyncio.wait_for(verifier.process.wait(), 60)
            await verifier.capture
            if verifier.process.returncode:
                raise RuntimeError(f"independent SDK oracle rejected agent {index}")
            self.actors.evidence.record(
                "managed_verified",
                index=index,
                records=writers.records,
                revision=revision,
            )
            self.actors.children.remove(verifier)

    async def close(self) -> None:
        await self.actors.stop(self.members)
        await self.actors.stop(self.relays)
        if any(member.process.returncode != 0 for member in self.members + self.relays):
            raise RuntimeError("managed cohort did not drain cleanly")
