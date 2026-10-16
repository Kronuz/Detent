"""Fixed three-voter process fixture with bounded authenticated HTTPS calls."""

from __future__ import annotations

import asyncio
import hashlib
import http.client
import json
import socket
import ssl
import sys
import time
from pathlib import Path
from typing import Any

from http_activity import GeneratorAdmissionRefused, HTTPActivity
from runtime import Actor, Actors

READER_TOKEN = "ab" * 16
READER_PRINCIPAL = "system-reader"
CONTEXT = {
    "cluster": "63" + "00" * 15,
    "group": "67" + "00" * 15,
    "epoch": "65" + "00" * 15,
}


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
        self.binary = binary
        self.directory = directory
        self.actors = actors
        self.producers = producers
        self.shared_namespace = shared_namespace
        self.namespaces = tuple(
            sorted({self.namespace(producer) for producer in range(producers)})
        )
        self.context = ssl.create_default_context(cafile=str(certificates / "ca.pem"))
        self.ports: list[int] = []
        self.members: list[Actor] = []
        self.leader_index = 0
        self.excluded: set[int] = set()
        workers = producers if http_workers is None else http_workers
        if not 1 <= workers <= 264:
            raise ValueError("invalid HTTP worker admission")
        self.lane = asyncio.Semaphore(min(producers, workers))
        self.http_activity = HTTPActivity()
        self.reservations: list[socket.socket] = []
        try:
            for _ in range(6):
                reservation = socket.socket()
                self.reservations.append(reservation)
                reservation.bind(("127.0.0.1", 0))
                self.ports.append(reservation.getsockname()[1])
            names = ("one", "two", "three")
            voters = []
            for index, name in enumerate(names):
                der = ssl.PEM_cert_to_DER_cert(
                    (certificates / f"{name}.pem").read_text()
                )
                voters.append(
                    {
                        "id": index + 1,
                        "host": "127.0.0.1",
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
                    "publisher_bind": "127.0.0.1",
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
                if sys.platform.startswith("linux"):
                    configuration["storage_backend"] = "native"
                path = directory / f"authority-{index}.json"
                path.write_text(json.dumps(configuration))
                path.chmod(0o600)
        except BaseException:
            self.release_ports()
            raise

    def namespace(self, producer: int) -> str:
        if not 0 <= producer < self.producers:
            raise ValueError("producer outside configured authority cohort")
        return self.shared_namespace or f"load{producer}"

    def release_ports(self) -> None:
        for reservation in self.reservations:
            reservation.close()
        self.reservations.clear()

    async def start_member(self, index: int) -> Actor:
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
                "--workers",
                "1",
            ],
        )

    async def start(self) -> None:
        self.release_ports()
        for index in range(3):
            self.members.append(await self.start_member(index))
        await self.leader()

    async def kill_member(self, index: int) -> None:
        member = self.members[index]
        member.process.kill()
        await member.process.wait()
        await member.capture
        self.actors.evidence.record(
            "member_killed", index=index, code=member.process.returncode
        )

    async def restart_member(self, index: int) -> None:
        member = self.members[index]
        if member.process.returncode is None:
            raise ValueError("cannot restart a running member")
        await member.capture
        self.actors.children.remove(member)
        self.members[index] = await self.start_member(index)

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
        connection = http.client.HTTPSConnection(
            "localhost",
            endpoint_port or self.ports[index + 3],
            context=self.context,
            timeout=5,
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
