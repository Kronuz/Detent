"""The relay's namespace mirror over HTTP (namespace-s8, "The service").

Three detentd voters; relay A reads them, relay B reads A, both as W8 readers with a credential.
A1: every block, base and section, and every W5/W6 answer (none, another incarnation, R = 0 ..
head + 1, raw and zstd), is byte-identical at the authority, A and B, and on A's and B's disks,
also when every other W5 next answers `409 session_busy`; the answer at no cursor, read here
without HttpUpstream, equals the probe's at every endpoint. Relay C reads a voter that lags behind
its mirror and moves to the current one.
A2: A and B restart, fetch no file their mirror holds, and answer unchanged.
A3: the relays refuse member routes and a `voters` configuration and link no consensus.

    test_relay_service.py DETENTD DETENT-RELAY RELAY-PROBE CERTIFICATES
"""

from __future__ import annotations

import hashlib
import http.client
import http.server
import itertools
import json
import os
import shutil
import signal
import socket
import ssl
import subprocess
import sys
import threading
import time
from pathlib import Path
from typing import Any

CONTEXT = {
    "cluster": "63" + "00" * 15,
    "group": "67" + "00" * 15,
    "epoch": "65" + "00" * 15,
}
WRITER = "a1" * 16  # the producer
MIRROR = "b2" * 16  # relay A at the authority
READER = "c3" * 16  # downstream readers at either relay, and relay B at A
NAMES = ["a", "b", "c", "d"]  # "d" is written rarely: its small roll-ups stay sections
# Small seals and roll-ups, and a block limit of twice the live bytes, so a short run reaches
# bases, blocks and the sections between roll-ups.
LAYOUT = {
    "seal_transactions": 3,
    "period": 4,
    "carry_bytes": 64,
    "carry_count": 2,
    "alpha_num": 8,
    "alpha_den": 1,
}


def check(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


class Proxy:
    """A TLS proxy in front of one voter that forwards every request unchanged, except: when `busy`,
    it answers every other W5 next `409 session_busy` itself, as a voter does while the session's
    previous answer retires; when `lagging`, it answers every layouts request with the first one it
    forwarded, as a voter cut off from the quorum does."""

    def __init__(
        self,
        certificates: Path,
        context: ssl.SSLContext,
        upstream: int,
        busy: bool = False,
    ) -> None:
        self.busy, self.lagging, self.stale = 0, False, 0
        self.first: tuple[int, dict[str, str], bytes] | None = None
        nexts = itertools.count()
        proxy = self

        class Handler(http.server.BaseHTTPRequestHandler):
            protocol_version = "HTTP/1.1"

            def do_POST(self) -> None:
                body = self.rfile.read(int(self.headers.get("Content-Length", "0")))
                if (
                    busy
                    and self.path == "/v2/distribution/session/next"
                    and next(nexts) % 2
                ):
                    proxy.busy += 1
                    self.answer(
                        409,
                        {"Content-Type": "application/json"},
                        b'{"error":"session_busy"}',
                    )
                    return
                layouts = self.path == "/v2/history/layouts"
                if layouts and proxy.lagging and proxy.first:
                    proxy.stale += 1
                    self.answer(*proxy.first)
                    return
                connection = http.client.HTTPSConnection(
                    "localhost", upstream, context=context, timeout=10
                )
                try:
                    connection.request(
                        "POST",
                        self.path,
                        body,
                        {
                            "Authorization": self.headers["Authorization"],
                            "Content-Type": "application/json",
                            "Connection": "close",
                        },
                    )
                    response = connection.getresponse()
                    headers = {
                        k: v
                        for k, v in response.getheaders()
                        if k.lower() == "content-type"
                        or k.lower().startswith("x-detent-")
                    }
                    answer = (response.status, headers, response.read())
                    if layouts and answer[0] == 200 and not proxy.first:
                        proxy.first = answer
                    self.answer(*answer)
                finally:
                    connection.close()

            def answer(self, status: int, headers: dict[str, str], body: bytes) -> None:
                self.send_response(status)
                for name, value in headers.items():
                    self.send_header(name, value)
                self.send_header("Content-Length", str(len(body)))
                self.send_header("Connection", "close")
                self.end_headers()
                self.wfile.write(body)

            def log_message(self, *_: Any) -> None:
                pass

        tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        tls.load_cert_chain(
            str(certificates / "one.pem"), str(certificates / "one.key")
        )
        self.server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.server.socket = tls.wrap_socket(self.server.socket, server_side=True)
        self.port = int(self.server.server_address[1])
        threading.Thread(target=self.server.serve_forever, daemon=True).start()

    def close(self) -> None:
        self.server.shutdown()
        self.server.server_close()


def free_port() -> int:
    with socket.socket() as reservation:
        reservation.bind(("127.0.0.1", 0))
        return int(reservation.getsockname()[1])


def private(path: Path, value: dict[str, Any]) -> Path:
    path.write_text(json.dumps(value))
    path.chmod(0o600)
    return path


class Run:
    def __init__(
        self, detentd: Path, relay: Path, probe: Path, certificates: Path
    ) -> None:
        self.detentd, self.relay, self.prober, self.certificates = (
            detentd,
            relay,
            probe,
            certificates,
        )
        self.directory = Path.cwd() / ".scratch" / f"relay-service-{os.getpid()}"
        self.directory.mkdir(parents=True)
        self.context = ssl.create_default_context(cafile=str(certificates / "ca.pem"))
        self.peers = [free_port() for _ in range(3)]
        self.http = [free_port() for _ in range(3)]
        self.relay_ports = {"a": free_port(), "b": free_port(), "c": free_port()}
        self.upstreams: dict[str, tuple[list[int], str]] = {}
        self.children: dict[str, subprocess.Popen[bytes]] = {}
        self.sequence = 0
        self.leading: int | None = None
        self.started = time.monotonic()

    # Processes.
    def start(self, name: str, command: list[str]) -> None:
        log = (self.directory / f"{name}.log").open("ab")
        self.children[name] = subprocess.Popen(
            command, stdout=log, stderr=subprocess.STDOUT
        )
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
            der = ssl.PEM_cert_to_DER_cert(
                (self.certificates / f"{node}.pem").read_text()
            )
            voters.append(
                {
                    "id": index + 1,
                    "host": "127.0.0.1",
                    "port": self.peers[index],
                    "certificate_sha256": hashlib.sha256(der).hexdigest(),
                }
            )
        for index, node in enumerate(("one", "two", "three")):
            tls = {
                "certificate": str(self.certificates / f"{node}.pem"),
                "key": str(self.certificates / f"{node}.key"),
                "ca": str(self.certificates / "ca.pem"),
            }
            configuration = CONTEXT | {
                "node": index + 1,
                "voters": voters,
                "directory": str(self.directory / f"state-{index}"),
                "peer_tls": tls,
                "publisher_tls": tls,
                "publisher_bind": "127.0.0.1",
                "publisher_name": "localhost",
                "application_limits": {
                    "state_bytes": 8 << 20,
                    "layout": LAYOUT,
                },  # files stay under a peer fetch
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
            }
            if sys.platform.startswith("linux"):
                configuration["storage_backend"] = "native"
            path = private(self.directory / f"voter-{index}.json", configuration)
            self.start(
                f"voter-{index}",
                [
                    str(self.detentd),
                    "--authority",
                    str(path),
                    "--port",
                    str(self.http[index]),
                    "--runtime-dir",
                    str(self.directory / f"runtime-{index}"),
                ],
            )

    def relay_configuration(
        self, name: str, upstreams: list[int], token: str, **extra: Any
    ) -> Path:
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
                "upstreams": [
                    {"address": "127.0.0.1", "port": port} for port in upstreams
                ],
                "token": token,
                "interval_ms": 100,
            },
            "credentials": [
                {
                    "token": READER,
                    "principal": "reader",
                    "policy_epoch": 1,
                    "namespaces": ["*"],
                }
            ],
        } | extra
        return private(self.directory / f"relay-{name}.json", configuration)

    def start_relay(self, name: str) -> None:
        path = self.relay_configuration(name, *self.upstreams[name])
        self.start(
            f"relay-{name}",
            [
                str(self.relay),
                "--config",
                str(path),
                "--runtime-dir",
                str(self.directory / f"relay-run-{name}"),
            ],
        )

    # HTTP.
    def call(
        self,
        port: int,
        path: str,
        body: dict[str, Any] | None = None,
        token: str = WRITER,
    ) -> tuple[int, bytes]:
        connection = http.client.HTTPSConnection(
            "localhost", port, context=self.context, timeout=10
        )
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
                status, body = self.call(
                    self.http[self.leading], path, CONTEXT | fields
                )
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
            operations = [
                {
                    "key": f"{name}:k{(step + salt) % 5}",
                    "value": {"default": step * 7 + salt},
                }
            ]
            if step % 11 == 10:
                operations.append(
                    {"key": f"{name}:k{(step + salt + 1) % 5}", "value": None}
                )
            receipt = self.write(
                "/v2/transactions",
                {
                    "generation": 1,
                    "sequence": self.sequence,
                    "namespace": name,
                    "operations": operations,
                },
            )
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
        result = subprocess.run(
            [
                str(self.prober),
                str(self.certificates / "ca.pem"),
                str(port),
                token,
                CONTEXT["cluster"],
                *NAMES,
            ],
            capture_output=True,
            text=True,
            timeout=60,
            check=False,
        )
        check(result.returncode == 0, f"probe of {port} failed: {result.stderr}")
        return json.loads(result.stdout)

    def direct(self, port: int, token: str) -> str:
        """The W6 body at no cursor for every name, read with this module's HTTP and DTDIST01
        parsing rather than HttpUpstream: the digest of the raw fragments' payloads."""
        names = [[name] for name in NAMES]
        status, body = self.call(
            port,
            "/v2/distribution/session/open",
            {"version": 2, "profile": 1, "coding": "raw", "namespaces": names},
            token,
        )
        check(status == 200, f"session open at {port} answered {status}: {body!r}")
        session = json.loads(body)["session"]
        status, body = self.call(
            port,
            "/v2/distribution/session/next",
            {"version": 2, "session": session, "namespaces": names, "wait_ms": 0},
            token,
        )
        check(status == 200, f"next at {port} answered {status}: {body[:256]!r}")
        payload = b""
        while body:  # a 96-byte header, its payload size big-endian at byte 12
            check(body[:8] == b"DTDIST01", f"next at {port}: not a fragment")
            size = int.from_bytes(body[12:16], "big")
            payload, body = payload + body[96 : 96 + size], body[96 + size :]
        check(bool(payload), f"next at {port} carried no frames")
        return hashlib.sha256(payload).hexdigest()

    def converged(self, ports: dict[str, tuple[int, str]]) -> dict[str, dict[str, Any]]:
        """Probes every endpoint until all answer alike; the writes are done, so the leader's is final."""
        deadline = time.monotonic() + 60
        last: dict[str, dict[str, Any]] = {}
        while time.monotonic() < deadline:
            last = {name: self.probe(*where) for name, where in ports.items()}
            if all(answer == last["authority"] for answer in last.values()):
                return last
            time.sleep(0.2)
        differing = {
            name for name, answer in last.items() if answer != last["authority"]
        }
        raise AssertionError(f"answers never converged; differing: {differing}")

    def on_disk(self, name: str, answer: dict[str, Any]) -> None:
        """Each file the relay serves is its own durable copy: m-<hash of its name>, these bytes."""
        for file in answer["files"]:
            stored = (
                self.directory
                / f"mirror-{name}"
                / ("m-" + hashlib.sha256(file["name"].encode()).hexdigest()[:32])
            )
            check(stored.is_file(), f"relay {name} holds no copy of {file['name']}")
            check(
                hashlib.sha256(stored.read_bytes()).hexdigest() == file["sha"],
                f"relay {name}'s copy of {file['name']} differs",
            )

    def log(self, line: str) -> None:
        print(f"[{time.monotonic() - self.started:6.1f}s] {line}", flush=True)

    def endpoints(self) -> dict[str, tuple[int, str]]:
        return {
            "authority": (self.http[self.leader()], MIRROR),
            "a": (self.relay_ports["a"], READER),
            "b": (self.relay_ports["b"], READER),
        }

    def a3(self) -> None:
        for path in (
            "/v2/open",
            "/v2/close",
            "/v2/transactions",
            "/v2/read",
            "/v2/result",
            "/v2/distribution",
            "/v1/transactions",
            "/v2/administer",
        ):
            for name in ("a", "b"):
                status, _ = self.call(
                    self.relay_ports[name],
                    path,
                    CONTEXT | {"expected_generation": 0},
                    READER,
                )
                check(
                    status == 404,
                    f"relay {name} answered member route {path} with {status}",
                )
        # A relay configured as a member does not start.
        bad = self.relay_configuration("a", self.http, MIRROR, voters=[{"id": 1}])
        refused = subprocess.run(
            [
                str(self.relay),
                "--config",
                str(bad),
                "--status",
                "--runtime-dir",
                str(self.directory / "relay-run-refused"),
            ],
            capture_output=True,
            timeout=30,
            check=False,
        )
        check(refused.returncode != 0, "a relay configuration with voters was accepted")
        # W8: the relay links no consensus (Itanium-mangled kronuz::consensus).
        symbols = subprocess.run(
            ["nm", str(self.relay)], capture_output=True, timeout=120, check=False
        ).stdout
        check(
            b"6kronuz9consensus" not in symbols, "detent-relay links kronuz::consensus"
        )
        self.log(
            "A3: member routes 404 on both relays; a voters configuration refused; no consensus symbols"
        )


def main() -> int:
    run = Run(*(Path(argument).resolve() for argument in sys.argv[1:5]))
    try:
        run.voters()
        run.leader()
        run.write("/v2/open", {"expected_generation": 0, "nonce": "6e" + "00" * 15})
        run.churn(12, 0)  # history before any relay
        # Relay C reads a proxy of the leader first, then the leader itself.
        leader = run.http[run.leader()]
        lagging = Proxy(run.certificates, run.context, leader)
        run.upstreams = {
            "a": (run.http, MIRROR),
            "b": ([run.relay_ports["a"]], READER),
            "c": ([lagging.port, leader], MIRROR),
        }
        for name in ("a", "b", "c"):
            run.start_relay(name)
        for name in ("a", "b", "c"):
            run.relay_ready(name)
        run.churn(27, 1)  # history the relays follow live
        run.log("writes done")
        first = run.converged(run.endpoints() | {"c": (run.relay_ports["c"], READER)})
        kinds = {file["kind"] for file in first["authority"]["files"]}
        check(kinds == {"base", "block", "section"}, f"the layouts reach only {kinds}")
        for name in ("a", "b"):
            run.on_disk(name, first[name])
        # The comparison holds frames: every name reached, the answers at R = 0 and at the head
        # differ, and the answer at no cursor read without HttpUpstream is the probe's everywhere.
        heads = first["authority"]["heads"]
        check(
            sorted(heads) == NAMES and all(at > 0 for at in heads.values()),
            f"the authority's answers reach heads {heads}",
        )
        frames = first["authority"]["frames"]
        for name, at in heads.items():
            check(
                frames[f"raw {name} 0"] != frames[f"raw {name} {at}"],
                f"{name}'s answers at 0 and at its head are alike",
            )
        for name, where in run.endpoints().items():
            check(
                run.direct(*where) == frames["raw *"],
                f"{name}'s answer at no cursor differs from the probe's",
            )
        answers = len(frames)
        run.log(
            f"A1: {len(first['authority']['files'])} files ({sorted(kinds)}) and {answers} W5/W6 answers "
            f"byte-identical at the authority, A and B, the answer at no cursor also read directly; "
            f"heads {heads}"
        )
        # Relay C is at the authority's seal; its first upstream now answers an older one.
        lagging.lagging = True
        # W5's session contract: a next on a busy session (409 session_busy) is asked again.
        proxy = Proxy(run.certificates, run.context, run.http[run.leader()], busy=True)
        try:
            via = run.probe(proxy.port, MIRROR)
        finally:
            proxy.close()
        check(
            via == first["authority"],
            "the probe through busy nexts answered differently",
        )
        check(proxy.busy > 0, "the proxy answered no next busy")
        run.log(
            f"W5: {proxy.busy} nexts answered 409 session_busy; the reader asked again, answers unchanged"
        )

        held = {file["name"] for file in first["authority"]["files"]}
        run.stop("relay-b")
        run.stop("relay-a")
        run.start_relay("a")
        run.start_relay("b")
        restarted = {name: run.relay_ready(name)["mirror"] for name in ("a", "b")}
        for name, mirror in restarted.items():
            check(
                mirror["fetched_files"] == 0 and mirror["reused_files"] > 0,
                f"relay {name} fetched again after a restart: {mirror}",
            )
        again = {name: run.probe(*where) for name, where in run.endpoints().items()}
        for name in ("a", "b"):
            check(
                again[name] == first[name],
                f"relay {name} answers differently after a restart",
            )
        # The next seals: the restarted relays follow them again, and C moves off its lagging voter.
        run.churn(12, 2)
        later = run.converged(run.endpoints() | {"c": (run.relay_ports["c"], READER)})
        lagging.close()
        check(lagging.stale > 0, "the lagging voter answered no layouts request")
        run.log(
            f"D9: relay C refused {lagging.stale} older layouts answers and followed the leader"
        )
        new = {file["name"] for file in later["authority"]["files"]} - held
        check(bool(new), "the later writes sealed no new file")
        for name in ("a", "b"):
            run.on_disk(name, later[name])
        run.log(
            f"A2: after a restart A and B fetched 0 files, reused {restarted['a']['reused_files']} and "
            f"{restarted['b']['reused_files']}, answered unchanged; then followed {len(new)} new files"
        )
        run.a3()
        return 0
    except BaseException:
        for log in sorted(run.directory.glob("*.log")):
            print(
                f"--- {log.name}\n{log.read_text(errors='replace')[-3000:]}", flush=True
            )
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
