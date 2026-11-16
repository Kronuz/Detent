"""The relay's namespace mirror over HTTP (namespace-s8, "The service").

Three detentd voters; relay A reads them, relay B reads A, both as W8 readers with a credential.
A1: every block, base and section, and every W5/W6 answer (none, another incarnation, R = 0 ..
head + 1, raw and zstd), is byte-identical at the authority, A and B, and on A's and B's disks.
A2: A and B restart, fetch no file their mirror holds, and answer unchanged.
A3: the relays refuse member routes and a `voters` configuration and link no consensus.

    test_relay_service.py DETENTD DETENT-RELAY RELAY-PROBE CERTIFICATES
"""

from __future__ import annotations

import hashlib
import http.client
import json
import os
import shutil
import signal
import socket
import ssl
import subprocess
import sys
import time
from pathlib import Path
from typing import Any

CONTEXT = {"cluster": "63" + "00" * 15, "group": "67" + "00" * 15, "epoch": "65" + "00" * 15}
WRITER = "a1" * 16  # the producer
MIRROR = "b2" * 16  # relay A at the authority
READER = "c3" * 16  # downstream readers at either relay, and relay B at A
NAMES = ["a", "b", "c", "d"]  # "d" is written rarely: its small roll-ups stay sections
# Small seals and roll-ups, and a block limit of twice the live bytes, so a short run reaches
# bases, blocks and the sections between roll-ups.
LAYOUT = {"seal_transactions": 3, "period": 4, "carry_bytes": 64, "carry_count": 2, "alpha_num": 8, "alpha_den": 1}


def check(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def free_port() -> int:
    with socket.socket() as reservation:
        reservation.bind(("127.0.0.1", 0))
        return int(reservation.getsockname()[1])


def private(path: Path, value: dict[str, Any]) -> Path:
    path.write_text(json.dumps(value))
    path.chmod(0o600)
    return path


class Run:
    def __init__(self, detentd: Path, relay: Path, probe: Path, certificates: Path) -> None:
        self.detentd, self.relay, self.prober, self.certificates = detentd, relay, probe, certificates
        self.directory = Path.cwd() / ".scratch" / f"relay-service-{os.getpid()}"
        self.directory.mkdir(parents=True)
        self.context = ssl.create_default_context(cafile=str(certificates / "ca.pem"))
        self.peers = [free_port() for _ in range(3)]
        self.http = [free_port() for _ in range(3)]
        self.relay_ports = {"a": free_port(), "b": free_port()}
        self.children: dict[str, subprocess.Popen[bytes]] = {}
        self.sequence = 0
        self.leading: int | None = None
        self.started = time.monotonic()

    # Processes.
    def start(self, name: str, command: list[str]) -> None:
        log = (self.directory / f"{name}.log").open("ab")
        self.children[name] = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
        log.close()

    def stop(self, name: str) -> None:
        child = self.children.pop(name)
        child.send_signal(signal.SIGTERM)
        try:
            child.wait(20)
        except subprocess.TimeoutExpired:
            child.kill()
            child.wait()
            raise AssertionError(f"{name} did not stop on SIGTERM")

    def voters(self) -> None:
        voters = []
        for index, node in enumerate(("one", "two", "three")):
            der = ssl.PEM_cert_to_DER_cert((self.certificates / f"{node}.pem").read_text())
            voters.append(
                {"id": index + 1, "host": "127.0.0.1", "port": self.peers[index],
                 "certificate_sha256": hashlib.sha256(der).hexdigest()}
            )
        for index, node in enumerate(("one", "two", "three")):
            tls = {"certificate": str(self.certificates / f"{node}.pem"),
                   "key": str(self.certificates / f"{node}.key"), "ca": str(self.certificates / "ca.pem")}
            configuration = CONTEXT | {
                "node": index + 1,
                "voters": voters,
                "directory": str(self.directory / f"state-{index}"),
                "peer_tls": tls,
                "publisher_tls": tls,
                "publisher_bind": "127.0.0.1",
                "publisher_name": "localhost",
                "application_limits": {"state_bytes": 8 << 20, "layout": LAYOUT},  # files stay under a peer fetch
                "credentials": [
                    {"token": WRITER, "producer": "writer", "namespaces": NAMES},
                    {"token": MIRROR, "producer": "relay", "principal": "relay", "publish": False,
                     "namespaces": ["*"]},
                ],
            }
            if sys.platform.startswith("linux"):
                configuration["storage_backend"] = "native"
            path = private(self.directory / f"voter-{index}.json", configuration)
            self.start(f"voter-{index}", [str(self.detentd), "--authority", str(path), "--port",
                                          str(self.http[index]), "--runtime-dir",
                                          str(self.directory / f"runtime-{index}")])

    def relay_configuration(self, name: str, upstreams: list[int], token: str, **extra: Any) -> Path:
        configuration = {
            "address": "127.0.0.1",
            "port": self.relay_ports[name],
            "hostname": "localhost",
            "certificate": str(self.certificates / "one.pem"),
            "private_key": str(self.certificates / "one.key"),
            "ca": str(self.certificates / "ca.pem"),
            "mirror": {
                "directory": str(self.directory / f"mirror-{name}"),
                "cluster": CONTEXT["cluster"],
                "namespaces": NAMES,
                "upstreams": [{"address": "127.0.0.1", "port": port} for port in upstreams],
                "token": token,
                "interval_ms": 100,
            },
            "credentials": [{"token": READER, "principal": "reader", "policy_epoch": 1, "namespaces": ["*"]}],
        } | extra
        return private(self.directory / f"relay-{name}.json", configuration)

    def start_relay(self, name: str) -> None:
        upstreams, token = (self.http, MIRROR) if name == "a" else ([self.relay_ports["a"]], READER)
        path = self.relay_configuration(name, upstreams, token)
        self.start(f"relay-{name}", [str(self.relay), "--config", str(path), "--runtime-dir",
                                     str(self.directory / f"relay-run-{name}")])

    # HTTP.
    def call(self, port: int, path: str, body: dict[str, Any] | None = None,
             token: str = WRITER) -> tuple[int, bytes]:
        connection = http.client.HTTPSConnection("localhost", port, context=self.context, timeout=10)
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
        return json.loads(body) if status == 200 else None

    def leader(self) -> int:
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            for index, port in enumerate(self.http):
                health = self.health(port)
                if health and health.get("role") == 2 and not health.get("fenced"):
                    return index
            time.sleep(0.05)
        raise AssertionError("the authority elected no leader")

    def write(self, path: str, fields: dict[str, Any]) -> dict[str, Any]:
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            if self.leading is None:
                self.leading = self.leader()
            try:
                status, body = self.call(self.http[self.leading], path, CONTEXT | fields)
            except (OSError, http.client.HTTPException):
                status, body = 0, b""
            if status == 200:
                return json.loads(body)
            self.leading = None
            time.sleep(0.05)
        raise AssertionError(f"{path} was not applied")

    def churn(self, count: int, salt: int) -> None:
        for step in range(count):
            name = "d" if step % 9 == 4 else NAMES[step % 3]
            self.sequence += 1
            operations = [{"key": f"{name}:k{(step + salt) % 5}", "value": {"default": step * 7 + salt}}]
            if step % 11 == 10:
                operations.append({"key": f"{name}:k{(step + salt + 1) % 5}", "value": None})
            receipt = self.write("/v2/transactions", {"generation": 1, "sequence": self.sequence,
                                                       "namespace": name, "operations": operations})
            check(receipt.get("status") == "applied", f"transaction refused: {receipt}")

    def relay_ready(self, name: str) -> dict[str, Any]:
        deadline = time.monotonic() + 60
        while time.monotonic() < deadline:
            child = self.children[f"relay-{name}"]
            check(child.poll() is None, f"relay {name} exited: {child.returncode}")
            health = self.health(self.relay_ports[name])
            if health and health.get("ready") and health.get("mirror", {}).get("ready"):
                return health
            time.sleep(0.05)
        raise AssertionError(f"relay {name} never became ready")

    def probe(self, port: int, token: str) -> dict[str, Any]:
        result = subprocess.run([str(self.prober), str(self.certificates / "ca.pem"), str(port), token,
                                 CONTEXT["cluster"], *NAMES], capture_output=True, text=True, timeout=60)
        check(result.returncode == 0, f"probe of {port} failed: {result.stderr}")
        return json.loads(result.stdout)

    def converged(self, ports: dict[str, tuple[int, str]]) -> dict[str, dict[str, Any]]:
        """Probes every endpoint until all answer alike; the writes are done, so the leader's is final."""
        deadline = time.monotonic() + 60
        last: dict[str, dict[str, Any]] = {}
        while time.monotonic() < deadline:
            last = {name: self.probe(*where) for name, where in ports.items()}
            if all(answer == last["authority"] for answer in last.values()):
                return last
            time.sleep(0.2)
        differing = {name for name, answer in last.items() if answer != last["authority"]}
        raise AssertionError(f"answers never converged; differing: {differing}")

    def on_disk(self, name: str, answer: dict[str, Any]) -> None:
        """Each file the relay serves is its own durable copy: m-<hash of its name>, these bytes."""
        for file in answer["files"]:
            stored = self.directory / f"mirror-{name}" / ("m-" + hashlib.sha256(file["name"].encode()).hexdigest()[:32])
            check(stored.is_file(), f"relay {name} holds no copy of {file['name']}")
            check(hashlib.sha256(stored.read_bytes()).hexdigest() == file["sha"],
                  f"relay {name}'s copy of {file['name']} differs")

    def log(self, line: str) -> None:
        print(f"[{time.monotonic() - self.started:6.1f}s] {line}", flush=True)

    def endpoints(self) -> dict[str, tuple[int, str]]:
        return {"authority": (self.http[self.leader()], MIRROR), "a": (self.relay_ports["a"], READER),
                "b": (self.relay_ports["b"], READER)}

    def a3(self) -> None:
        for path in ("/v2/open", "/v2/close", "/v2/transactions", "/v2/read", "/v2/result", "/v2/distribution",
                     "/v1/transactions", "/v2/administer"):
            for name in ("a", "b"):
                status, _ = self.call(self.relay_ports[name], path, CONTEXT | {"expected_generation": 0}, READER)
                check(status == 404, f"relay {name} answered member route {path} with {status}")
        # A relay configured as a member does not start.
        bad = self.relay_configuration("a", self.http, MIRROR, voters=[{"id": 1}])
        refused = subprocess.run([str(self.relay), "--config", str(bad), "--status", "--runtime-dir",
                                  str(self.directory / "relay-run-refused")], capture_output=True, timeout=30)
        check(refused.returncode != 0, "a relay configuration with voters was accepted")
        # W8: the relay links no consensus (Itanium-mangled kronuz::consensus).
        symbols = subprocess.run(["nm", str(self.relay)], capture_output=True, timeout=120).stdout
        check(b"6kronuz9consensus" not in symbols, "detent-relay links kronuz::consensus")
        self.log("A3: member routes 404 on both relays; a voters configuration refused; no consensus symbols")


def main() -> int:
    run = Run(*(Path(argument).resolve() for argument in sys.argv[1:5]))
    try:
        run.voters()
        run.leader()
        run.write("/v2/open", {"expected_generation": 0, "nonce": "6e" + "00" * 15})
        run.churn(12, 0)  # history before any relay
        run.start_relay("a")
        run.start_relay("b")
        run.relay_ready("a")
        run.relay_ready("b")
        run.churn(27, 1)  # history the relays follow live
        run.log("writes done")
        first = run.converged(run.endpoints())
        kinds = {file["kind"] for file in first["authority"]["files"]}
        check(kinds == {"base", "block", "section"}, f"the layouts reach only {kinds}")
        for name in ("a", "b"):
            run.on_disk(name, first[name])
        answers = len(first["authority"]["frames"])
        run.log(f"A1: {len(first['authority']['files'])} files ({sorted(kinds)}) and {answers} W5/W6 answers "
              f"byte-identical at the authority, A and B; heads {first['authority']['heads']}")

        held = {file["name"] for file in first["authority"]["files"]}
        run.stop("relay-b")
        run.stop("relay-a")
        run.start_relay("a")
        run.start_relay("b")
        restarted = {name: run.relay_ready(name)["mirror"] for name in ("a", "b")}
        for name, mirror in restarted.items():
            check(mirror["fetched_files"] == 0 and mirror["reused_files"] > 0,
                  f"relay {name} fetched again after a restart: {mirror}")
        again = {name: run.probe(*where) for name, where in run.endpoints().items()}
        for name in ("a", "b"):
            check(again[name] == first[name], f"relay {name} answers differently after a restart")
        # The next seals: the restarted relays follow them again.
        run.churn(12, 2)
        later = run.converged(run.endpoints())
        new = {file["name"] for file in later["authority"]["files"]} - held
        check(bool(new), "the later writes sealed no new file")
        for name in ("a", "b"):
            run.on_disk(name, later[name])
        run.log(f"A2: after a restart A and B fetched 0 files, reused {restarted['a']['reused_files']} and "
              f"{restarted['b']['reused_files']}, answered unchanged; then followed {len(new)} new files")
        run.a3()
        return 0
    except BaseException:
        for log in sorted(run.directory.glob("*.log")):
            print(f"--- {log.name}\n{log.read_text(errors='replace')[-3000:]}", flush=True)
        raise
    finally:
        for name in list(run.children):
            try:
                run.stop(name)
            except AssertionError:
                pass
        shutil.rmtree(run.directory, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
