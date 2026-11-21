"""The fixture's HTTPS calls dial the address its servers bind, not what "localhost" resolves to.

usage: test_localhost_dial.py TLS_FIXTURE [--natural N]

The daemons listen on 127.0.0.1 and present a certificate for "localhost". Resolving that name
tries ::1 first on macOS, and a connect to a ::1 port nothing listens on can connect the socket to
itself (IPv4 refuses that with EINVAL; IPv6 does not); TLS then reads its own ClientHello and fails
with UNEXPECTED_MESSAGE. The default run puts an echo listener on [::1]:PORT, which returns those
same bytes every time, beside a TLS server on 127.0.0.1:PORT, and requires Authority._exchange to
reach the server. --natural N makes N calls to a port served on 127.0.0.1 alone and fails on any
error: it measures how often the self-connection happens on its own. Exit 77 (skip) without ::1.
"""

from __future__ import annotations

import argparse
import http.server
import json
import socket
import ssl
import sys
import threading
import time
from collections import Counter
from pathlib import Path
from types import SimpleNamespace
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parent))

from authority import Authority

HEALTH = {"sessions": 0}


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self) -> None:
        body = json.dumps(HEALTH).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *_: Any) -> None:
        pass


class Echo:
    """A [::1] listener that returns what it reads, as a self-connected socket does."""

    def __init__(self, port: int) -> None:
        self.socket = socket.socket(socket.AF_INET6)
        self.socket.bind(("::1", port))
        self.socket.listen()
        self.accepted = 0
        threading.Thread(target=self.serve, daemon=True).start()

    def serve(self) -> None:
        while True:
            try:
                connection, _ = self.socket.accept()
            except OSError:
                return
            self.accepted += 1
            with connection:
                connection.settimeout(5)
                try:
                    while data := connection.recv(65536):
                        connection.sendall(data)
                except OSError:
                    pass


def has_ipv6() -> bool:
    try:
        with socket.socket(socket.AF_INET6) as probe:
            probe.bind(("::1", 0))
    except OSError:
        return False
    return True


def serve(certificates: Path) -> http.server.ThreadingHTTPServer:
    tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    tls.load_cert_chain(str(certificates / "one.pem"), str(certificates / "one.key"))
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    server.socket = tls.wrap_socket(server.socket, server_side=True)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server


def exchange(certificates: Path, port: int) -> tuple[int, dict[str, Any]]:
    """One Authority._exchange GET to `port`, on a fixture holding only what the call reads."""
    fixture = SimpleNamespace(
        address="127.0.0.1",
        context=ssl.create_default_context(cafile=str(certificates / "ca.pem")),
        ports=[0] * 6,
    )
    return Authority._exchange(fixture, 0, "/v1/health", None, 0, port)  # type: ignore[arg-type]


def squatted(certificates: Path) -> None:
    server = serve(certificates)
    port = int(server.server_address[1])
    echo = Echo(port)
    try:
        for _ in range(20):
            assert exchange(certificates, port) == (200, HEALTH)
        assert echo.accepted == 0, f"{echo.accepted} calls went to [::1]:{port}"
    finally:
        echo.socket.close()
        server.shutdown()
        server.server_close()
    print("20 calls reached 127.0.0.1 beside a [::1] echo listener")


def natural(certificates: Path, count: int) -> int:
    server = serve(certificates)
    port = int(server.server_address[1])
    outcomes: Counter[str] = Counter()
    started = time.monotonic()
    try:
        for _ in range(count):
            try:
                outcomes[
                    "ok" if exchange(certificates, port) == (200, HEALTH) else "wrong"
                ] += 1
            except Exception as error:  # noqa: BLE001 - every failure kind is the measurement
                outcomes[f"{type(error).__name__}: {error}"] += 1
    finally:
        server.shutdown()
        server.server_close()
    elapsed = time.monotonic() - started
    print(
        json.dumps({"calls": count, "seconds": round(elapsed, 1), "outcomes": outcomes})
    )
    return 0 if outcomes["ok"] == count else 1


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("certificates", type=Path)
    parser.add_argument("--natural", type=int)
    arguments = parser.parse_args()
    if not has_ipv6():
        print("skip: no ::1 on this host")
        return 77
    if arguments.natural:
        return natural(arguments.certificates, arguments.natural)
    squatted(arguments.certificates)
    return 0


if __name__ == "__main__":
    sys.exit(main())
