"""Real HTTPS authority failover with three independent daemon processes."""

from __future__ import annotations

import hashlib
import http.client
import json
import os
import shutil
import socket
import ssl
import subprocess
import sys
import time
from pathlib import Path
from typing import Any, BinaryIO

TOKEN = "0123456789abcdef0123456789abcdef"
CONTEXT = {
    "cluster": "63" + "00" * 15,
    "group": "67" + "00" * 15,
    "epoch": "65" + "00" * 15,
}


def request(
    port: int, context: ssl.SSLContext, path: str, fields: dict[str, Any] | None = None
) -> tuple[int, dict[str, Any]]:
    connection = http.client.HTTPSConnection(
        "localhost", port, timeout=2, context=context
    )
    try:
        if fields is None:
            connection.request("GET", path, headers={"Connection": "close"})
        else:
            connection.request(
                "POST",
                path,
                json.dumps(CONTEXT | fields),
                {"Authorization": f"Bearer {TOKEN}", "Connection": "close"},
            )
        response = connection.getresponse()
        return response.status, json.loads(response.read())
    finally:
        connection.close()


def main() -> None:
    binary = Path(sys.argv[1]).resolve()
    certificates = Path(sys.argv[2]).resolve()
    directory = Path.cwd() / ".scratch" / f"authority-multivoter-{os.getpid()}"
    directory.mkdir(parents=True)
    reservations = [socket.socket() for _ in range(6)]
    for reservation in reservations:
        reservation.bind(("127.0.0.1", 0))
    ports = [reservation.getsockname()[1] for reservation in reservations]
    names = ["one", "two", "three"]
    voters = []
    for index, name in enumerate(names):
        der = ssl.PEM_cert_to_DER_cert((certificates / f"{name}.pem").read_text())
        voters.append(
            {
                "id": index + 1,
                "host": "127.0.0.1",
                "port": ports[index],
                "certificate_sha256": hashlib.sha256(der).hexdigest(),
            }
        )
    for index, name in enumerate(names):
        tls = {
            "certificate": str(certificates / f"{name}.pem"),
            "key": str(certificates / f"{name}.key"),
            "ca": str(certificates / "ca.pem"),
        }
        configuration = CONTEXT | {
            "node": index + 1,
            "voters": voters,
            "directory": str(directory / f"state-{index}"),
            "peer_tls": tls,
            "publisher_tls": tls,
            "publisher_bind": "0.0.0.0",
            "publisher_name": "localhost",
            "credentials": [
                {"token": TOKEN, "producer": "writer", "namespaces": ["ns"]}
            ],
        }
        path = directory / f"node-{index}.json"
        path.write_text(json.dumps(configuration))
        path.chmod(0o600)
    for reservation in reservations:
        reservation.close()
    children: dict[int, subprocess.Popen[bytes]] = {}
    logs: list[BinaryIO] = []
    context = ssl.create_default_context(cafile=str(certificates / "ca.pem"))

    def start(index: int) -> None:
        log = (directory / f"node-{index}.log").open("ab")
        logs.append(log)
        children[index] = subprocess.Popen(
            [
                str(binary),
                "--authority",
                str(directory / f"node-{index}.json"),
                "--port",
                str(ports[index + 3]),
                "--runtime-dir",
                str(directory / f"runtime-{index}"),
                "--workers",
                "1",
            ],
            stdout=log,
            stderr=subprocess.STDOUT,
        )

    def leader() -> int:
        deadline = time.monotonic() + 10
        observed: dict[int, Any] = {}
        while time.monotonic() < deadline:
            for index, child in children.items():
                if child.poll() is not None:
                    continue
                try:
                    status, body = request(ports[index + 3], context, "/v1/health")
                    observed[index] = body
                    if (
                        status == 200
                        and body.get("role") == 2
                        and not body.get("fenced")
                    ):
                        return index
                except (OSError, http.client.HTTPException) as error:
                    observed[index] = str(error)
            time.sleep(0.02)
        raise AssertionError(
            f"three-process authority did not elect an available leader: {observed}"
        )

    def successful(path: str, fields: dict[str, Any]) -> tuple[int, dict[str, Any]]:
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            index = leader()
            status, body = request(ports[index + 3], context, path, fields)
            if status == 200:
                return index, body
            time.sleep(0.02)
        raise AssertionError(f"request failed: {path}")

    try:
        for index in range(3):
            start(index)
        successful("/v2/open", {"expected_generation": 0, "nonce": "6e" + "00" * 15})
        status = subprocess.run(
            [
                str(binary),
                "--status",
                "--authority",
                str(directory / "node-0.json"),
                "--port",
                str(ports[3]),
                "--runtime-dir",
                str(directory / "runtime-0"),
            ],
            capture_output=True,
            text=True,
            timeout=5,
            check=False,
        )
        assert status.returncode == 0 and "durable_fixed_voters" in status.stdout, (
            status.stdout + status.stderr
        )
        transaction = {
            "generation": 1,
            "sequence": 1,
            "namespace": "ns",
            "operations": [
                {
                    "key": "ns:key",
                    "value": {"type": "int", "default": 10},
                    "expected_revision": 0,
                    "absent_namespace_revision": 0,
                }
            ],
        }
        index, receipt = successful("/v2/transactions", transaction)
        _, duplicate = successful("/v2/transactions", transaction)
        assert receipt == duplicate, "retry changed the durable receipt"
        lost_transaction = {
            "generation": 1,
            "sequence": 2,
            "namespace": "ns",
            "operations": [
                {
                    "key": "ns:key",
                    "value": {"type": "int", "default": 20},
                    "expected_revision": 1,
                }
            ],
        }
        index = leader()
        lost = http.client.HTTPSConnection(
            "localhost", ports[index + 3], timeout=2, context=context
        )
        try:
            lost.request(
                "POST",
                "/v2/transactions",
                json.dumps(CONTEXT | lost_transaction),
                {"Authorization": f"Bearer {TOKEN}"},
            )
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                _, observed = successful(
                    "/v2/read", {"namespace": "ns", "keys": ["ns:key"]}
                )
                if observed["key_revisions"]["ns:key"] == 2:
                    break
                time.sleep(0.02)
            else:
                raise AssertionError("unconsumed-response transaction did not commit")
        finally:
            # Deliberately discard the original HTTP response without reading it.
            lost.close()
        _, retried = successful("/v2/transactions", lost_transaction)
        assert retried["revision"] == 2, "lost response retry applied a second mutation"
        children[index].kill()
        children[index].wait(timeout=5)
        _, recovered = successful("/v2/read", {"namespace": "ns", "keys": ["ns:key"]})
        assert recovered["key_revisions"]["ns:key"] == 2
        start(index)
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            try:
                _, health = request(ports[index + 3], context, "/v1/health")
                if health.get("revision") == 2 and health.get("peer_admitted", 0) >= 4:
                    break
            except (OSError, http.client.HTTPException):
                pass
            time.sleep(0.02)
        else:
            raise AssertionError("restarted voter did not recover and reconnect")
        print(
            "Three HTTPS daemons: durable receipt, retry, leader kill, strong read and restart passed"
        )
    except BaseException:
        for log in directory.glob("*.log"):
            print(log.name, log.read_text(), file=sys.stderr)
        raise
    finally:
        for child in children.values():
            if child.poll() is None:
                child.terminate()
        for child in children.values():
            try:
                child.wait(timeout=5)
            except subprocess.TimeoutExpired:
                child.kill()
                child.wait(timeout=5)
        for log in logs:
            log.close()
        shutil.rmtree(directory)


if __name__ == "__main__":
    main()
