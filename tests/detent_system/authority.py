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

from runtime import Actors

CONTEXT = {
    "cluster": "63" + "00" * 15,
    "group": "67" + "00" * 15,
    "epoch": "65" + "00" * 15,
}


def credential(index: int) -> dict[str, Any]:
    return {
        "token": f"{index + 65536:032x}",
        "producer": f"load{index}",
        "namespaces": [f"load{index}"],
    }


class Authority:
    def __init__(
        self,
        binary: Path,
        certificates: Path,
        directory: Path,
        actors: Actors,
        producers: int,
    ) -> None:
        self.binary = binary
        self.directory = directory
        self.actors = actors
        self.context = ssl.create_default_context(cafile=str(certificates / "ca.pem"))
        self.ports: list[int] = []
        self.leader_index = 0
        self.lane = asyncio.Semaphore(producers)
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
                        credential(producer) for producer in range(producers)
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

    def release_ports(self) -> None:
        for reservation in self.reservations:
            reservation.close()
        self.reservations.clear()

    async def start(self) -> None:
        self.release_ports()
        for index in range(3):
            await self.actors.start(
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
        await self.leader()

    def _request(
        self, index: int, path: str, fields: dict[str, Any] | None, producer: int
    ) -> tuple[int, dict[str, Any]]:
        connection = http.client.HTTPSConnection(
            "localhost", self.ports[index + 3], context=self.context, timeout=5
        )
        try:
            headers = {"Connection": "close"}
            if fields is None:
                connection.request("GET", path, headers=headers)
            else:
                headers["Authorization"] = f"Bearer {credential(producer)['token']}"
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
    ) -> tuple[int, dict[str, Any]]:
        async with self.lane:
            original = asyncio.create_task(
                asyncio.to_thread(self._request, index, path, fields, producer)
            )
            cancellation: asyncio.CancelledError | None = None
            while not original.done():
                try:
                    await asyncio.shield(original)
                except asyncio.CancelledError as error:
                    cancellation = cancellation or error
            result = original.result()
            if cancellation is not None:
                raise cancellation
            return result

    async def leader(self) -> int:
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            for index in range(3):
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
