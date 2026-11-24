"""Fixed three-voter process fixture with bounded authenticated HTTPS calls."""

from __future__ import annotations

import asyncio
import hashlib
import http.client
import json
import socket
import ssl
import subprocess
import sys
import threading
import time
from collections.abc import Callable
from pathlib import Path
from typing import Any

from capacity import CapacityProfile
from http_activity import GeneratorAdmissionRefused, HTTPActivity
from runtime import Actor, Actors

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "support"))
from ports import lease_ports, release_ports  # noqa: E402

READER_TOKEN = "ab" * 16
READER_PRINCIPAL = "system-reader"
CONTEXT = {
    "cluster": "63" + "00" * 15,
    "group": "67" + "00" * 15,
    "epoch": "65" + "00" * 15,
}


class HostLag:
    """The longest a thread asleep for 1 ms went without running, from start to stop."""

    def __init__(self) -> None:
        self.worst = 0.0
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def _run(self) -> None:
        last = time.monotonic()
        while not self._stop.wait(0.001):
            now = time.monotonic()
            self.worst = max(self.worst, now - last - 0.001)
            last = now

    def stop(self) -> None:
        self._stop.set()
        self._thread.join()


def credential(index: int, shared_namespace: str | None = None) -> dict[str, Any]:
    return {
        "token": f"{index + 65536:032x}",
        "producer": f"load{index}",
        "namespaces": [shared_namespace or f"load{index}"],
    }


class Authority:
    def __init__(
        self,
        binary: Path,
        certificates: Path,
        directory: Path,
        actors: Actors,
        producers: int,
        shared_namespace: str | None = None,
        http_workers: int | None = None,
        capacity: CapacityProfile | None = None,
        address: str = "127.0.0.1",
    ) -> None:
        if not 1 <= producers <= 256:
            raise ValueError("authority fixture needs 1–256 producers")
        if shared_namespace is not None and (
            not 1 <= len(shared_namespace) <= 198
            or any(
                not (character.isascii() and character.isalnum())
                and character not in "._/-"
                for character in shared_namespace
            )
        ):
            raise ValueError("invalid shared authority namespace")
        self.address = address
        self.binary = binary
        self.directory = directory
        self.actors = actors
        self.producers = producers
        self.shared_namespace = shared_namespace
        self.namespaces = tuple(
            sorted({self.namespace(producer) for producer in range(producers)})
        )
        self.context = ssl.create_default_context(cafile=str(certificates / "ca.pem"))
        self.members: list[Actor] = []
        self.leader_index = 0
        self.excluded: set[int] = set()
        workers = producers if http_workers is None else http_workers
        if not 1 <= workers <= 264:
            raise ValueError("invalid HTTP worker admission")
        self.lane = asyncio.Semaphore(min(producers, workers))
        self.http_activity = HTTPActivity()
        self.ports = lease_ports(6, address)
        names = ("one", "two", "three")
        voters = []
        for index, name in enumerate(names):
            der = ssl.PEM_cert_to_DER_cert((certificates / f"{name}.pem").read_text())
            voters.append(
                {
                    "id": index + 1,
                    "host": address,
                    "port": self.ports[index],
                    "certificate_sha256": hashlib.sha256(der).hexdigest(),
                }
            )
        for index, name in enumerate(names):
            tls = {
                "certificate": str(certificates / f"{name}.pem"),
                "key": str(certificates / f"{name}.key"),
                "ca": str(certificates / "ca.pem"),
            }
            configuration: dict[str, Any] = CONTEXT | {
                "node": index + 1,
                "voters": voters,
                "directory": str(directory / f"state-{index}"),
                "peer_tls": tls,
                "publisher_tls": tls,
                "publisher_bind": address,
                "publisher_name": "localhost",
                "credentials": [
                    credential(producer, shared_namespace)
                    for producer in range(producers)
                ]
                + [
                    {
                        "token": READER_TOKEN,
                        "producer": READER_PRINCIPAL,
                        "principal": READER_PRINCIPAL,
                        "publish": False,
                        "namespaces": list(self.namespaces),
                    }
                ],
            }
            if capacity is not None:
                configuration["application_limits"] = capacity.application
                configuration["delivery_limits"] = capacity.delivery
                configuration["journal_limits"] = capacity.journal
            if sys.platform.startswith("linux"):
                configuration["storage_backend"] = "native"
            path = directory / f"authority-{index}.json"
            path.write_text(json.dumps(configuration))
            path.chmod(0o600)

    def namespace(self, producer: int) -> str:
        if not 0 <= producer < self.producers:
            raise ValueError("producer outside configured authority cohort")
        return self.shared_namespace or f"load{producer}"

    def release_ports(self) -> None:
        release_ports(self.ports)

    async def start_member(
        self, index: int, observe: Callable[[bytes], None] | None = None
    ) -> Actor:
        return await self.actors.start(
            f"authority-{index}",
            [
                str(self.binary),
                "--authority",
                str(self.directory / f"authority-{index}.json"),
                "--port",
                str(self.ports[index + 3]),
                "--runtime-dir",
                str(self.directory / f"runtime-{index}"),
            ],
            observe=observe,
        )

    async def start(self) -> None:
        for index in range(3):
            self.members.append(await self.start_member(index))
        await self.leader()

    async def kill_member(self, index: int) -> None:
        member = self.members[index]
        member.process.kill()
        try:
            async with asyncio.timeout(10):
                await member.process.wait()
                await member.capture
        except TimeoutError:
            raise RuntimeError(
                f"authority-{index} did not exit or close its output 10 s after SIGKILL"
            ) from None
        self.actors.evidence.record(
            "member_killed", index=index, code=member.process.returncode
        )

    async def restart_member(self, index: int, seconds: float = 20) -> None:
        """Restarts a stopped member on its port and returns once it answers, or raises
        after `seconds` plus the host's measured lag, saying what it waited for. A process
        outside the tests may hold the port for a while (bind: Address already in use), so
        the member starts again until it is free."""
        lag = HostLag()
        try:
            await self._restart(index, seconds, lag)
        finally:
            lag.stop()

    async def _restart(self, index: int, seconds: float, lag: HostLag) -> None:
        member = self.members[index]
        if member.process.returncode is None:
            raise ValueError("cannot restart a running member")
        await member.capture
        self.actors.children.remove(member)
        began = time.monotonic()
        attempts = 0

        def late() -> bool:
            return time.monotonic() - began > seconds + lag.worst

        while True:
            attempts += 1
            output = bytearray()
            spawned = time.time()
            member = await self.start_member(index, output.extend)
            self.members[index] = member
            refusal = "no request yet"
            while member.process.returncode is None:
                try:
                    await self.request(index, "/v1/health")
                    self.actors.evidence.record(
                        "member_answered",
                        index=index,
                        attempts=attempts,
                        spawned=spawned,
                        answered=time.time(),
                    )
                    return
                except (OSError, http.client.HTTPException) as error:
                    refusal = repr(error)
                if late():
                    raise RuntimeError(
                        f"authority-{index} alive (pid {member.process.pid}) but not answering "
                        f"{time.monotonic() - began:.1f} s after restart ({seconds} s plus "
                        f"{lag.worst:.1f} s of host lag allowed), attempt {attempts} spawned "
                        f"at {spawned:.3f}: last {refusal}; "
                        f"output {output[-400:].decode(errors='replace')!r}; "
                        f"{self._sockets(index, member.process.pid)}"
                    )
                await asyncio.sleep(0.05)
            await member.capture
            self.actors.children.remove(member)
            if b"Address already in use" not in output or late():
                raise RuntimeError(
                    f"authority-{index} exited {member.process.returncode} on restart "
                    f"attempt {attempts}: {output[-400:].decode(errors='replace')!r}"
                )
            self.actors.evidence.record("member_restart_port_busy", index=index)
            await asyncio.sleep(0.1)

    def _sockets(self, index: int, pid: int) -> str:
        """What the OS says about the member and its port, for a member that does not answer."""
        report = []
        for command in (
            ["ps", "-o", "pid,stat,etime,command", "-p", str(pid)],
            ["lsof", "-nP", f"-iTCP:{self.ports[index + 3]}"],
        ):
            try:
                result = subprocess.run(
                    command, capture_output=True, text=True, timeout=10, check=False
                )
                report.append(f"{command[0]}: {result.stdout.strip()!r}")
            except (OSError, subprocess.SubprocessError) as error:
                report.append(f"{command[0]}: {error!r}")
        return "; ".join(report)

    def _request(
        self,
        index: int,
        path: str,
        fields: dict[str, Any] | None,
        producer: int,
        endpoint_port: int | None = None,
        submitted_ns: int = 0,
    ) -> tuple[int, dict[str, Any]]:
        with self.http_activity.observe(submitted_ns or time.monotonic_ns()):
            return self._exchange(index, path, fields, producer, endpoint_port)

    def _exchange(
        self,
        index: int,
        path: str,
        fields: dict[str, Any] | None,
        producer: int,
        endpoint_port: int | None,
    ) -> tuple[int, dict[str, Any]]:
        port = endpoint_port or self.ports[index + 3]
        connection = http.client.HTTPSConnection(
            "localhost", port, context=self.context, timeout=5
        )
        # Dials the bound address and verifies the name: resolving localhost tries ::1 first,
        # where a connect to a free port can reach its own socket (test_localhost_dial.py).
        connection._create_connection = lambda _, *rest: socket.create_connection(
            (self.address, port), *rest
        )
        try:
            headers = {"Connection": "close"}
            if fields is None:
                connection.request("GET", path, headers=headers)
            else:
                headers["Authorization"] = (
                    f"Bearer {credential(producer, self.shared_namespace)['token']}"
                )
                connection.request("POST", path, json.dumps(CONTEXT | fields), headers)
            response = connection.getresponse()
            body = response.read(1024 * 1024 + 1)
            if len(body) > 1024 * 1024:
                raise ValueError("authority response exceeds 1 MiB")
            decoded: Any = json.loads(body)
            if not isinstance(decoded, dict):
                raise TypeError("authority response must be an object")
            return response.status, decoded
        finally:
            connection.close()

    async def request(
        self,
        index: int,
        path: str,
        fields: dict[str, Any] | None = None,
        producer: int = 0,
        *,
        endpoint_port: int | None = None,
        admission_deadline: float | None = None,
    ) -> tuple[int, dict[str, Any]]:
        submitted_ns = time.monotonic_ns()
        deadline = (
            time.monotonic() + 5 if admission_deadline is None else admission_deadline
        )
        if time.monotonic() >= deadline:
            raise GeneratorAdmissionRefused("HTTP admission deadline already expired")
        try:
            async with asyncio.timeout_at(deadline):
                await self.lane.acquire()
        except TimeoutError as error:
            raise GeneratorAdmissionRefused(
                "HTTP original was not submitted before deadline"
            ) from error
        try:
            if time.monotonic() >= deadline:
                raise GeneratorAdmissionRefused(
                    "HTTP admission deadline expired before submission"
                )
            original = asyncio.create_task(
                asyncio.to_thread(
                    self._request,
                    index,
                    path,
                    fields,
                    producer,
                    endpoint_port,
                    submitted_ns,
                )
            )
            cancellation: asyncio.CancelledError | None = None
            while not original.done():
                try:
                    await asyncio.wait({original})
                except asyncio.CancelledError as error:
                    cancellation = cancellation or error
            if cancellation is not None:
                if not original.cancelled():
                    original.exception()
                raise cancellation
            return original.result()

        finally:
            self.lane.release()

    async def leader(self) -> int:
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            for index in range(3):
                if index in self.excluded:
                    continue
                try:
                    status, health = await self.request(index, "/v1/health")
                    if (
                        status == 200
                        and health.get("role") == 2
                        and not health.get("fenced")
                    ):
                        self.leader_index = index
                        return index
                except (OSError, http.client.HTTPException):
                    pass
            await asyncio.sleep(0.05)
        raise RuntimeError("authority did not elect an available leader")
