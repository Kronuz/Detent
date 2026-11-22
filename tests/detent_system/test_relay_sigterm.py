"""A SIGTERM under load: the relay finishes the responses in flight, exits 0 within its bound.

usage: test_relay_sigterm.py DETENT_AUTHORITY DETENT_RELAY TLS_FIXTURE [BOUND_MS]

Three authority voters and a relay run in the foreground; eight clients loop HTTPS /v1/health on the
relay (a new connection each). The relay gets SIGTERM and must exit 0 within --shutdown-timeout-ms;
exit 0 says its drain found no admitted request unanswered. A response the relay began and did not
finish fails the test. After the signal, a connection refused, reset or closed before any response
byte is the stop doing its work: it ends the TLS handshake and the read of a request not yet read
(Kronuz/http), which the client cannot tell apart. Before the signal, any of them fails the test.
"""

from __future__ import annotations

import hashlib
import json
import shutil
import signal
import socket
import ssl
import subprocess
import sys
import threading
import time
from pathlib import Path

CONTEXT = {
    "cluster": "63" + "00" * 15,
    "group": "67" + "00" * 15,
    "epoch": "65" + "00" * 15,
}
TOKEN = "0123456789abcdef0123456789abcdef"


def free_port() -> int:
    with socket.socket() as reservation:
        reservation.bind(("127.0.0.1", 0))
        return int(reservation.getsockname()[1])


def request(port: int, context: ssl.SSLContext) -> str:
    """One health request on a new connection: ok, unanswered (no response byte) or torn."""
    data = b""
    try:
        with (
            socket.create_connection(("127.0.0.1", port), timeout=5) as raw,
            context.wrap_socket(raw, server_hostname="localhost") as tls,
        ):
            tls.sendall(
                b"GET /v1/health HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n"
            )
            while chunk := tls.recv(65536):
                data += chunk
    except OSError:
        # Refused, reset, a failed handshake, a broken pipe, a timeout: torn once the reply began.
        return "torn" if data else "unanswered"
    if not data:
        return "unanswered"
    head, separator, body = data.partition(b"\r\n\r\n")
    if not separator or not head.startswith(b"HTTP/1.1 "):
        return "torn"
    for line in head.split(b"\r\n")[1:]:
        name, _, value = line.partition(b":")
        if name.lower() == b"content-length" and int(value) != len(body):
            return "torn"
    return "ok"


NAMES = ("one", "two", "three")


def authority_configurations(
    certificates: Path, directory: Path, peers: list[int]
) -> list[Path]:
    """One file a voter: the authority starts only as three voters."""
    voters = [
        {
            "id": index + 1,
            "host": "127.0.0.1",
            "port": port,
            "certificate_sha256": hashlib.sha256(
                ssl.PEM_cert_to_DER_cert((certificates / f"{name}.pem").read_text())
            ).hexdigest(),
        }
        for index, (name, port) in enumerate(zip(NAMES, peers))
    ]
    paths = []
    for index, name in enumerate(NAMES):
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
            "publisher_bind": "127.0.0.1",
            "publisher_name": "localhost",
            "credentials": [
                {"token": TOKEN, "producer": "writer", "namespaces": ["ns"]}
            ],
        }
        if sys.platform.startswith("linux"):
            configuration["storage_backend"] = "native"
        path = directory / f"authority-{index}.json"
        path.write_text(json.dumps(configuration))
        path.chmod(0o600)
        paths.append(path)
    return paths


def relay_configuration(
    certificates: Path, directory: Path, port: int, upstream: int
) -> Path:
    configuration = {
        "address": "127.0.0.1",
        "port": port,
        "hostname": "localhost",
        "certificate": str(certificates / "one.pem"),
        "private_key": str(certificates / "one.key"),
        "ca": str(certificates / "ca.pem"),
        "mirror": {
            "directory": str(directory / "mirror"),
            "cluster": CONTEXT["cluster"],
            "namespaces": ["ns"],
            "upstreams": [{"address": "127.0.0.1", "port": upstream}],
            "timeout_ms": 2000,
        },
        "secrets": "relay-secrets.json",
    }
    secrets = {
        "upstream_token": TOKEN,
        "credentials": [
            {"token": "cd" * 16, "principal": "reader", "namespaces": ["ns"]}
        ],
    }
    (directory / "relay-secrets.json").write_text(json.dumps(secrets))
    (directory / "relay-secrets.json").chmod(0o600)
    path = directory / "detent-relay.json"
    path.write_text(json.dumps(configuration))
    return path


def main() -> int:
    authority, relay, certificates = (
        Path(argument).resolve() for argument in sys.argv[1:4]
    )
    bound = int(sys.argv[4]) if len(sys.argv) > 4 else 10000
    directory = Path.cwd() / ".scratch" / f"relay-sigterm-{time.monotonic_ns()}"
    directory.mkdir(parents=True)
    context = ssl.create_default_context(cafile=str(certificates / "ca.pem"))
    peers = [free_port() for _ in NAMES]
    publisher, port = free_port(), free_port()
    commands = {
        f"authority-{index}": [
            str(authority),
            "--authority",
            str(path),
            "--port",
            str(publisher if index == 0 else 0),
            "--runtime-dir",
            str(directory / f"runtime-{index}"),
        ]
        for index, path in enumerate(
            authority_configurations(certificates, directory, peers)
        )
    } | {
        "relay": [
            str(relay),
            "--config",
            str(relay_configuration(certificates, directory, port, publisher)),
            "--runtime-dir",
            str(directory / "relay-run"),
            "--shutdown-timeout-ms",
            str(bound),
        ],
    }
    children: dict[str, subprocess.Popen[bytes]] = {}
    results: list[tuple[str, float, float]] = []  # (outcome, sent, settled), monotonic
    stopping = threading.Event()
    lock = threading.Lock()
    try:
        for name, command in commands.items():
            log = (directory / f"{name}.log").open("wb")
            children[name] = subprocess.Popen(
                command, stdout=log, stderr=subprocess.STDOUT
            )
        deadline = time.monotonic() + 30
        while request(port, context) != "ok":
            assert children["relay"].poll() is None, "the relay exited at startup"
            assert time.monotonic() < deadline, "the relay never answered"
            time.sleep(0.05)

        def load() -> None:
            while not stopping.is_set():
                sent = time.monotonic()
                outcome = request(port, context)
                with lock:
                    results.append((outcome, sent, time.monotonic()))

        threads = [threading.Thread(target=load) for _ in range(8)]
        for thread in threads:
            thread.start()
        time.sleep(1.5)
        signalled = time.monotonic()
        children["relay"].send_signal(signal.SIGTERM)
        status = children["relay"].wait(bound / 1000 + 10)
        elapsed = time.monotonic() - signalled
        stopping.set()
        for thread in threads:
            thread.join()
        assert status == 0, f"the relay exited with status {status}"
        assert elapsed * 1000 <= bound, (
            f"the stop took {elapsed:.1f} s, over {bound} ms"
        )
        ok = sum(outcome == "ok" for outcome, _, _ in results)
        torn = sum(outcome == "torn" for outcome, _, _ in results)
        early = sum(
            outcome == "unanswered" and settled < signalled
            for outcome, _, settled in results
        )
        unanswered = sum(outcome == "unanswered" for outcome, _, _ in results) - early
        assert ok >= 8, f"only {ok} requests completed"
        assert not torn, f"{torn} responses were cut short"
        assert not early, f"{early} requests went unanswered before the SIGTERM"
        straddled = sum(
            outcome == "ok" and sent < signalled < settled
            for outcome, sent, settled in results
        )
        print(
            f"relay SIGTERM under load: {ok} requests completed ({straddled} sent before the "
            f"signal and answered after it), none cut short; {unanswered} connections refused "
            f"or closed unread after the signal; exit 0 in {elapsed:.2f} s",
            flush=True,
        )
    except BaseException:
        stopping.set()
        for log in sorted(directory.glob("*.log")):
            print(
                f"--- {log.name}\n{log.read_text(errors='replace')[-2000:]}", flush=True
            )
        raise
    finally:
        stopping.set()
        for child in children.values():
            if child.poll() is None:
                child.terminate()
        for child in children.values():
            try:
                child.wait(15)
            except subprocess.TimeoutExpired:
                child.kill()
        shutil.rmtree(directory, ignore_errors=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
