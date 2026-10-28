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

import readiness_load

TOKEN = "0123456789abcdef0123456789abcdef"
CONTEXT = {
    "cluster": "63" + "00" * 15,
    "group": "67" + "00" * 15,
    "epoch": "65" + "00" * 15,
}


class StartupBindCollision(RuntimeError):
    pass


def request(
    port: int,
    context: ssl.SSLContext,
    path: str,
    fields: dict[str, Any] | None = None,
    timeout: float = 2,
    token: str = TOKEN,
) -> tuple[int, dict[str, Any]]:
    connection = http.client.HTTPSConnection(
        "localhost", port, timeout=timeout, context=context
    )
    try:
        if fields is None:
            connection.request("GET", path, headers={"Connection": "close"})
        else:
            connection.request(
                "POST",
                path,
                json.dumps(CONTEXT | fields),
                {"Authorization": f"Bearer {token}", "Connection": "close"},
            )
        response = connection.getresponse()
        return response.status, json.loads(response.read())
    finally:
        connection.close()


def main() -> None:
    binary = Path(sys.argv[1]).resolve()
    certificates = Path(sys.argv[2]).resolve()
    load = "--load" in sys.argv[3:]
    snapshots = "--snapshots" in sys.argv[3:]
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
        configuration: dict[str, Any] = CONTEXT | {
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
        if snapshots:
            configuration["consensus_limits"] = {"log_entries": 64}
        if load:
            configuration["credentials"].extend(readiness_load.credentials())
        if sys.platform.startswith("linux"):
            configuration["storage_backend"] = "native"
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

    def await_startup() -> None:
        pending = set(children)
        deadline = time.monotonic() + 10
        while pending and time.monotonic() < deadline:
            for index in tuple(pending):
                assert children[index].poll() is None, f"node {index} exited at startup"
                try:
                    request(ports[index + 3], context, "/v1/health")
                    pending.remove(index)
                except (OSError, http.client.HTTPException):
                    pass
            if pending:
                time.sleep(0.02)
        assert not pending, f"nodes did not start: {sorted(pending)}"

    def leader() -> int:
        deadline = time.monotonic() + 10
        observed: dict[int, Any] = {}
        while time.monotonic() < deadline:
            for index, child in children.items():
                if child.poll() is not None:
                    continue
                try:
                    status, body = request(ports[index + 3], context, "/v1/health")
                    if sys.platform.startswith("linux"):
                        assert body.get("io_native_available") is True, body
                        assert body.get("storage_async") is True, body
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
        try:
            await_startup()
            successful(
                "/v2/open", {"expected_generation": 0, "nonce": "6e" + "00" * 15}
            )
        except AssertionError as error:
            collisions = [
                index
                for index, child in children.items()
                if child.poll() is not None
                and "bind: Address already in use"
                in (directory / f"node-{index}.log").read_text()
            ]
            if collisions:
                raise StartupBindCollision(
                    f"reserved ports were taken before startup: nodes {collisions}"
                ) from error
            raise
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

        if snapshots:
            snapshot_leader = leader()
            lagging = next(index for index in range(3) if index != snapshot_leader)
            offline_commit = request(ports[lagging + 3], context, "/v1/health")[1][
                "committed"
            ]
            children[lagging].kill()
            children[lagging].wait(timeout=5)
            last_receipt: dict[str, Any] = {}
            for sequence in range(3, 101):
                _, last_receipt = successful(
                    "/v2/transactions",
                    {
                        "generation": 1,
                        "sequence": sequence,
                        "namespace": "ns",
                        "operations": [
                            {
                                "key": "ns:key",
                                "value": {"type": "int", "default": sequence},
                                "expected_revision": sequence - 1,
                            }
                        ],
                    },
                )
                assert last_receipt["revision"] == sequence
            surviving = [index for index in range(3) if children[index].poll() is None]
            deadline = time.monotonic() + 15
            while True:
                checkpoints = [
                    request(ports[index + 3], context, "/v1/health")[1]
                    for index in surviving
                ]
                if all(
                    health["checkpoint_published_boundary"] > offline_commit
                    for health in checkpoints
                ):
                    break
                assert time.monotonic() < deadline, checkpoints
                time.sleep(0.3)
            start(lagging)
            deadline = time.monotonic() + 20
            while True:
                try:
                    health = request(ports[lagging + 3], context, "/v1/health")[1]
                    if (
                        health.get("revision") == 100
                        and health.get("checkpoint_published_boundary", 0)
                        > offline_commit
                    ):
                        break
                except (OSError, http.client.HTTPException):
                    pass
                assert time.monotonic() < deadline, (
                    "lagging voter did not install nonempty snapshot"
                )
                time.sleep(0.3)
            _, replayed = successful("/v2/result", {"generation": 1, "sequence": 100})
            assert replayed == last_receipt
            _, read = successful("/v2/read", {"namespace": "ns", "keys": ["ns:key"]})
            assert read["key_revisions"]["ns:key"] == 100

            _, continued = successful(
                "/v2/transactions",
                {
                    "generation": 1,
                    "sequence": 101,
                    "namespace": "ns",
                    "operations": [
                        {
                            "key": "ns:key",
                            "value": {"type": "int", "default": 101},
                            "expected_revision": 100,
                        }
                    ],
                },
            )
            assert continued["revision"] == 101
            replaced = leader()
            children[replaced].kill()
            children[replaced].wait(timeout=5)
            _, after_election = successful(
                "/v2/read", {"namespace": "ns", "keys": ["ns:key"]}
            )
            assert after_election["key_revisions"]["ns:key"] == 101
            _, retained = successful("/v2/result", {"generation": 1, "sequence": 101})
            assert retained == continued
            start(replaced)
            print(
                json.dumps(
                    {
                        "phase": "snapshot_catchup",
                        "survivors": checkpoints,
                        "lagging": health,
                    }
                ),
                flush=True,
            )
        if load:
            load_leader = leader()

            def pressure_request(
                path: str, fields: dict[str, Any], token: str
            ) -> tuple[int, dict[str, Any]]:
                return request(
                    ports[load_leader + 3],
                    context,
                    path,
                    fields,
                    timeout=8,
                    token=token,
                )

            receipts = readiness_load.run_pressure(pressure_request)
            assert receipts, "pressure fixture did not acknowledge any writes"
            time.sleep(0.3)
            original_health = readiness_load.assert_drained(
                lambda: request(ports[load_leader + 3], context, "/v1/health")[1]
            )
            print(
                json.dumps(
                    {"phase": "pressure_owner_health", "health": original_health}
                ),
                flush=True,
            )
            children[load_leader].kill()
            children[load_leader].wait(timeout=5)
            load_leader = leader()

            def recovered_request(
                path: str, fields: dict[str, Any], token: str
            ) -> tuple[int, dict[str, Any]]:
                deadline = time.monotonic() + 20
                while True:
                    candidate = leader()
                    code, reply = request(
                        ports[candidate + 3],
                        context,
                        path,
                        fields,
                        timeout=8,
                        token=token,
                    )
                    if code != 503 or time.monotonic() >= deadline:
                        return code, reply
                    time.sleep(0.02)

            readiness_load.verify_receipts(recovered_request, receipts)
            start(
                next(index for index in range(3) if children[index].poll() is not None)
            )
            time.sleep(0.3)
            sampled = readiness_load.assert_drained(
                lambda: request(ports[load_leader + 3], context, "/v1/health")[1]
            )
            print(
                json.dumps({"phase": "post_pressure_health", "health": sampled}),
                flush=True,
            )

        # Retain the elected leader but remove both quorum partners. A cached
        # local value must not escape as a successful strong read.
        survivor = leader()
        for peer, child in children.items():
            if peer != survivor:
                child.kill()
                child.wait(timeout=5)
        began = time.monotonic()
        code, failure = request(
            ports[survivor + 3],
            context,
            "/v2/read",
            {"namespace": "ns", "keys": ["ns:key"]},
            timeout=8,
        )
        elapsed = time.monotonic() - began
        assert code == 503 and elapsed < 7, (code, failure, elapsed)
        print(
            "Three HTTPS daemons: durable receipt, lost reply, leader kill, restart and bounded no-quorum read passed"
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
    for attempt in range(3):
        try:
            main()
            break
        except StartupBindCollision:
            if attempt == 2:
                raise
            print(
                "Retrying initial startup with fresh ports after a bind collision",
                file=sys.stderr,
            )
