"""A single-voter detent-authority and a detent-relay mirroring it, on fresh state under .scratch."""

from __future__ import annotations

import http.client
import json
import os
import shutil
import signal
import socket
import ssl
import subprocess
import tempfile
import time
from pathlib import Path
from typing import Any, Self

CONTEXT = {
    "cluster": "63" + "00" * 15,
    "group": "67" + "00" * 15,
    "epoch": "65" + "00" * 15,
}
WRITER = "a1" * 16
MIRROR = "b2" * 16
READER = "c3" * 16
NAMES = ["a", "b"]
LAYOUT = {
    "seal_transactions": 3,
    "period": 4,
    "carry_bytes": 64,
    "carry_count": 2,
    "alpha_num": 8,
    "alpha_den": 1,
}


def free_port() -> int:
    with socket.socket() as reservation:
        reservation.bind(("127.0.0.1", 0))
        return int(reservation.getsockname()[1])


def private(path: Path, value: dict[str, Any]) -> Path:
    path.write_text(json.dumps(value))
    path.chmod(0o600)
    return path


class Pair:
    """Starts both processes on enter, writes `transactions` to every name, waits until the relay
    mirrors them, and stops both on exit. The relay syncs every `interval_ms` (its default 250)."""

    def __init__(
        self, build: Path, transactions: int = 12, interval_ms: int = 250
    ) -> None:
        self.source = Path(__file__).resolve().parents[2]
        self.authority = build / "src/detent-authority"
        self.relay = build / "src/detent-relay"
        for binary in (self.authority, self.relay):
            if not binary.is_file():
                raise RuntimeError(f"missing {binary}")
        self.transactions = transactions
        self.interval_ms = interval_ms
        self.children: dict[str, subprocess.Popen[bytes]] = {}
        self.authority_port, self.relay_port = free_port(), free_port()
        scratch = self.source / ".scratch"
        scratch.mkdir(exist_ok=True)
        self.root = Path(tempfile.mkdtemp(prefix="idle-pair-", dir=scratch))
        self.certificates = self.root / "certificates"
        self.ca = str(self.certificates / "ca.pem")
        self.sequence = 0

    def __enter__(self) -> Self:
        try:
            self.start()
        except BaseException:
            self.dump()
            self.stop()
            raise
        return self

    def __exit__(self, *_: object) -> None:
        self.stop()

    def connection(self, port: int) -> http.client.HTTPSConnection:
        context = ssl.create_default_context(cafile=self.ca)
        return http.client.HTTPSConnection(
            "localhost", port, context=context, timeout=10
        )

    def call(
        self,
        port: int,
        path: str,
        body: dict[str, Any] | None = None,
        token: str = WRITER,
    ) -> tuple[int, bytes]:
        connection = self.connection(port)
        try:
            headers = {"Connection": "close"}
            if body is None:
                connection.request("GET", path, headers=headers)
            else:
                headers["Authorization"] = f"Bearer {token}"
                headers["Content-Type"] = "application/json"
                connection.request("POST", path, json.dumps(body), headers)
            response = connection.getresponse()
            return response.status, response.read()
        finally:
            connection.close()

    def health(self, port: int) -> dict[str, Any] | None:
        try:
            status, body = self.call(port, "/v1/health")
        except (OSError, http.client.HTTPException):
            return None
        result: dict[str, Any] | None = json.loads(body) if status == 200 else None
        return result

    def wait(self, name: str, ready: Any, timeout: float = 60) -> dict[str, Any]:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            child = self.children[name]
            if child.poll() is not None:
                raise RuntimeError(f"{name} exited {child.returncode}")
            port = self.authority_port if name == "authority" else self.relay_port
            health = self.health(port)
            if health and ready(health):
                return health
            time.sleep(0.05)
        raise RuntimeError(f"{name} never became ready")

    def write(self, path: str, fields: dict[str, Any]) -> dict[str, Any]:
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            status, body = self.call(self.authority_port, path, CONTEXT | fields)
            if status == 200:
                result: dict[str, Any] = json.loads(body)
                return result
            time.sleep(0.05)
        raise RuntimeError(f"{path} was not applied")

    def wait_history(self, timeout: float = 30) -> None:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            status, _ = self.call(
                self.authority_port,
                "/v2/history/layouts",
                {"namespaces": NAMES},
                token=MIRROR,
            )
            if status == 200:
                return
            time.sleep(0.02)
        raise RuntimeError("the authority's history never became available")

    def launch(self, name: str, command: list[str]) -> None:
        with (self.root / f"{name}.log").open("wb") as log:
            self.children[name] = subprocess.Popen(
                command, stdout=log, stderr=subprocess.STDOUT
            )

    def start(self) -> None:
        subprocess.run(
            [
                "cmake",
                f"-DOUTPUT={self.certificates}",
                f"-DOPENSSL={shutil.which('openssl')}",
                "-P",
                str(self.source / "tests/detent_authority/tls_certificates.cmake"),
            ],
            check=True,
            stdout=subprocess.DEVNULL,
            env={
                **os.environ,
                "OPENSSL_CONF": str(
                    self.source / "tests/detent_authority/tls_ambient.cnf"
                ),
            },
        )
        tls = {
            "certificate": str(self.certificates / "one.pem"),
            "key": str(self.certificates / "one.key"),
            "ca": self.ca,
        }
        authority = private(
            self.root / "authority.json",
            CONTEXT
            | {
                "directory": str(self.root / "authority-data"),
                "publisher_tls": tls,
                "publisher_bind": "127.0.0.1",
                "publisher_name": "localhost",
                "application_limits": {"state_bytes": 8 << 20, "layout": LAYOUT},
                "credentials": [
                    {"token": WRITER, "producer": "writer", "namespaces": NAMES},
                    {
                        "token": MIRROR,
                        "producer": "relay",
                        "principal": "relay",
                        "publish": False,
                        "namespaces": ["*"],
                    },
                ],
            },
        )
        self.launch(
            "authority",
            [
                str(self.authority),
                "--authority",
                str(authority),
                "--port",
                str(self.authority_port),
                "--runtime-dir",
                str(self.root / "authority-run"),
            ],
        )
        self.wait("authority", lambda health: health.get("ready"))
        self.write("/v2/open", {"expected_generation": 0, "nonce": "6e" + "00" * 15})
        for step in range(self.transactions):
            self.sequence += 1
            name = NAMES[step % len(NAMES)]
            receipt = self.write(
                "/v2/transactions",
                {
                    "generation": 1,
                    "sequence": self.sequence,
                    "namespace": name,
                    "operations": [
                        {"key": f"{name}:k{step % 5}", "value": {"default": step}}
                    ],
                },
            )
            if receipt.get("status") != "applied":
                raise RuntimeError(f"transaction refused: {receipt}")
        # The history answers a few hundred ms after the last write; a relay started before then
        # fails its first sync and retries one interval later, a minute in the idle count.
        self.wait_history()
        relay = private(
            self.root / "relay.json",
            {
                "address": "127.0.0.1",
                "port": self.relay_port,
                "hostname": "localhost",
                "certificate": tls["certificate"],
                "private_key": tls["key"],
                "ca": self.ca,
                "mirror": {
                    "directory": str(self.root / "mirror"),
                    "cluster": CONTEXT["cluster"],
                    "namespaces": NAMES,
                    "upstreams": [
                        {"address": "127.0.0.1", "port": self.authority_port}
                    ],
                    "token": MIRROR,
                    "interval_ms": self.interval_ms,
                },
                "credentials": [
                    {
                        "token": READER,
                        "principal": "reader",
                        "policy_epoch": 1,
                        "namespaces": ["*"],
                    }
                ],
            },
        )
        self.launch(
            "relay",
            [
                str(self.relay),
                "--config",
                str(relay),
                "--runtime-dir",
                str(self.root / "relay-run"),
            ],
        )
        self.wait(
            "relay",
            lambda health: (
                health.get("ready") and health.get("mirror", {}).get("ready")
            ),
        )

    def dump(self) -> None:
        for name in self.children:
            log = self.root / f"{name}.log"
            if log.exists():
                tail = log.read_text(errors="replace")[-4000:]
                print(f"--- {name}.log (end)\n{tail}", flush=True)

    def stop(self) -> None:
        for child in self.children.values():
            if child.poll() is None:
                child.send_signal(signal.SIGTERM)
        for child in self.children.values():
            try:
                child.wait(20)
            except subprocess.TimeoutExpired:
                child.kill()
                child.wait()
        self.children.clear()
        shutil.rmtree(self.root, ignore_errors=True)
