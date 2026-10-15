"""Prove terminal source trust failures never try another configured endpoint."""

from __future__ import annotations

import argparse
import asyncio
import json
import ssl
from pathlib import Path

from authority import CONTEXT, READER_PRINCIPAL, READER_TOKEN
from managed import scope_identity
from runtime import Actors, Evidence


class ControlServer:
    def __init__(self, certificate: Path, key: Path, case: str) -> None:
        self.context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        self.context.load_cert_chain(certificate, key)
        self.case = case
        self.requests = 0
        self.failure: BaseException | None = None
        self.server: asyncio.Server | None = None
        self.tasks: set[asyncio.Task[None]] = set()
        self.writers: set[asyncio.StreamWriter] = set()

    async def start(self) -> int:
        self.server = await asyncio.start_server(
            self.accept, "127.0.0.1", 0, ssl=self.context, limit=8192
        )
        return int(self.server.sockets[0].getsockname()[1])

    def accept(
        self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter
    ) -> None:
        task = asyncio.create_task(self.respond(reader, writer))
        self.tasks.add(task)
        task.add_done_callback(self.finished)

    def finished(self, task: asyncio.Task[None]) -> None:
        self.tasks.discard(task)
        if task.cancelled():
            self.failure = self.failure or RuntimeError(
                "trust server original was cancelled"
            )
        elif failure := task.exception():
            self.failure = self.failure or failure

    async def respond(
        self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter
    ) -> None:
        self.writers.add(writer)
        try:
            header = await asyncio.wait_for(reader.readuntil(b"\r\n\r\n"), 2)
            length = next(
                int(line.split(b":", 1)[1])
                for line in header.split(b"\r\n")
                if line.lower().startswith(b"content-length:")
            )
            if not 0 <= length <= 4096:
                raise ValueError("trust fixture request bound")
            await reader.readexactly(length)
            self.requests += 1
            status = 401
            body = b'{"error":"unauthorized"}'
            extra = b""
            content_type = b"application/json"
            if self.case.startswith("close-"):
                if b"/session/open " in header:
                    status = 200
                    body = b'{"version":1,"session":"11111111111111111111111111111111","window":0,"mode":"snapshot","wait_ms":1000}'
                elif b"/session/close " in header:
                    if self.case == "close-malformed":
                        body = b"{broken"
                    elif self.case == "close-forbidden":
                        status, body = 403, b'{"error":"scope_forbidden"}'
                    elif self.case == "close-identity":
                        status, body = 409, b'{"error":"identity_changed"}'
                else:
                    status, body = 503, b'{"error":"source_not_leader"}'
            elif b"/session/close " in header:
                status, body = 204, b""
            elif self.case == "forbidden":
                status, body = 403, b'{"error":"scope_forbidden"}'
            elif self.case == "identity":
                status, body = 409, b'{"error":"identity_changed"}'
            elif self.case == "profile":
                status = 200
                if b"/session/open " in header:
                    body = b'{"version":1,"session":"11111111111111111111111111111111","window":0,"mode":"snapshot","wait_ms":1000}'
                else:
                    content_type, body = b"application/vnd.detent.distribution", b"\x00"
                    extra = b"X-Detent-Session-Version: 1\r\nX-Detent-Session: 11111111111111111111111111111111\r\nX-Detent-Window: 1\r\nX-Detent-Profile: 2\r\nX-Detent-Revision: 1\r\nX-Detent-Kind: 1\r\nX-Detent-Coding: raw\r\n"
            writer.write(
                f"HTTP/1.1 {status} Response\r\n".encode()
                + b"Content-Type: "
                + content_type
                + b"\r\nContent-Length: "
                + str(len(body)).encode()
                + b"\r\n"
                + extra
                + b"\r\n"
                + body
            )
            await writer.drain()
        except (ConnectionError, OSError, asyncio.IncompleteReadError):
            pass
        finally:
            writer.close()
            try:
                await writer.wait_closed()
            except (ConnectionError, OSError):
                pass
            self.writers.discard(writer)

    async def close(self) -> None:
        if self.server is not None:
            self.server.close()
        for writer in self.writers:
            writer.close()
        if self.tasks:
            await asyncio.gather(*tuple(self.tasks))
        if self.server is not None:
            await self.server.wait_closed()
        if self.failure is not None:
            raise self.failure


async def run(arguments: argparse.Namespace) -> None:
    arguments.output.mkdir(parents=True, mode=0o700, exist_ok=False)
    evidence = Evidence(arguments.output / "events.jsonl", 1024 * 1024)
    try:
        for case in (
            "unauthorized",
            "forbidden",
            "identity",
            "profile",
            "hostname",
            "ca",
            "close-unauthorized",
            "close-forbidden",
            "close-identity",
            "close-malformed",
        ):
            certificate = "rogue" if case == "ca" else "one"
            primary = ControlServer(
                arguments.certificates / f"{certificate}.pem",
                arguments.certificates / f"{certificate}.key",
                case,
            )
            secondary = ControlServer(
                arguments.certificates / "one.pem",
                arguments.certificates / "one.key",
                "unauthorized",
            )
            try:
                ports = [await primary.start(), await secondary.start()]
                directory = arguments.output / case
                directory.mkdir(mode=0o700)
                source = CONTEXT | {
                    "principal": READER_PRINCIPAL,
                    "namespaces": ["load0"],
                    "token": READER_TOKEN,
                    "authority_endpoints": [
                        {"address": "127.0.0.1", "port": port} for port in ports
                    ],
                    "hostname": "wrong.example" if case == "hostname" else "localhost",
                    "ca": str(arguments.certificates / "ca.pem"),
                    "delivery_mode": "subscription",
                    "timeout_ms": 1000,
                    "poll_interval_ms": 10,
                    "reconnect_maximum_ms": 100,
                }
                path = directory / "managed.json"
                path.write_text(json.dumps(source))
                path.chmod(0o600)
                config = directory / "agent.json"
                config.write_text(
                    json.dumps(
                        {
                            "managed_path": str(path),
                            "generation_dir": str(directory / "generations"),
                            "runtime_dir": str(directory / "run"),
                            **{
                                f"generation_{name}": value
                                for name, value in CONTEXT.items()
                            },
                            "generation_scope": scope_identity(1),
                        }
                    )
                )
                async with Actors(evidence, 10) as actors:
                    agent = await actors.start(
                        case,
                        [
                            str(arguments.agents),
                            "--config",
                            str(config),
                            "--initialize-managed",
                        ],
                    )
                    await asyncio.wait_for(agent.process.wait(), 10)
                    minimum = (
                        3
                        if case.startswith("close-")
                        else 2
                        if case == "profile"
                        else 0
                        if case in ("hostname", "ca")
                        else 1
                    )
                    if (
                        agent.process.returncode != 1
                        or secondary.requests
                        or primary.requests < minimum
                    ):
                        raise RuntimeError(f"trust failure did not fail closed: {case}")
                evidence.record(
                    "trust_rejected",
                    case=case,
                    primary_requests=primary.requests,
                    secondary_requests=secondary.requests,
                )
            finally:
                await primary.close()
                await secondary.close()
        evidence.record("completed")
    finally:
        evidence.close()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("agents", "certificates", "output"):
        parser.add_argument(
            f"--{name}", type=lambda value: Path(value).resolve(), required=True
        )
    asyncio.run(run(parser.parse_args()))


if __name__ == "__main__":
    main()
