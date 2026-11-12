"""A relay and an authority voter bound to :: serve an IPv4 client and an IPv6 client.

usage: test_dual_stack.py DETENTD DETENT_RELAY TLS_FIXTURE

Each daemon listens on "::" (the `[::]` wildcard); the test sends HTTPS /v1/health to it over
127.0.0.1 and over ::1 and says which families ran. Exit 77 (skip) where the host has no ::1.
"""

from __future__ import annotations

import hashlib
import json
import shutil
import socket
import ssl
import subprocess
import sys
import time
from pathlib import Path
from typing import Any

CONTEXT = {
    "cluster": "63" + "00" * 15,
    "group": "67" + "00" * 15,
    "epoch": "65" + "00" * 15,
}
TOKEN = "0123456789abcdef0123456789abcdef"


def has_ipv6() -> bool:
    try:
        with socket.socket(socket.AF_INET6) as probe:
            probe.bind(("::1", 0))
    except OSError:
        return False
    return True


def free_port() -> int:
    # Reserved on :: for both families, so no IPv4 listener holds the port the daemon binds.
    with socket.socket(socket.AF_INET6) as reservation:
        reservation.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 0)
        reservation.bind(("::", 0))
        return int(reservation.getsockname()[1])


def health(address: str, port: int, context: ssl.SSLContext) -> dict[str, Any]:
    """GET /v1/health over one family, verifying the certificate as "localhost"."""
    family = socket.AF_INET6 if ":" in address else socket.AF_INET
    with socket.socket(family) as raw:
        raw.settimeout(3)
        raw.connect((address, port))
        with context.wrap_socket(raw, server_hostname="localhost") as tls:
            tls.sendall(b"GET /v1/health HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n")
            data = b""
            while chunk := tls.recv(65536):
                data += chunk
    head, _, body = data.partition(b"\r\n\r\n")
    # 503 is the relay before its first generation: still an answer over this family.
    assert head.startswith((b"HTTP/1.1 200", b"HTTP/1.1 503")), head
    result = json.loads(body[body.index(b"{") :])
    assert isinstance(result, dict)
    return result


def wait_health(
    address: str, port: int, context: ssl.SSLContext, process: subprocess.Popen[bytes]
) -> dict[str, Any]:
    deadline = time.monotonic() + 30
    while True:
        assert process.poll() is None, f"daemon exited at startup: {process.returncode}"
        try:
            return health(address, port, context)
        except (OSError, ValueError, AssertionError):
            assert time.monotonic() < deadline, f"no health over {address}"
            time.sleep(0.05)


def authority_configuration(certificates: Path, directory: Path, ports: list[int]) -> Path:
    voters = []
    for index, name in enumerate(["one", "two", "three"]):
        der = ssl.PEM_cert_to_DER_cert((certificates / f"{name}.pem").read_text())
        voters.append(
            {
                "id": index + 1,
                "host": "::1",
                "port": ports[index],
                "certificate_sha256": hashlib.sha256(der).hexdigest(),
            }
        )
    tls = {
        "certificate": str(certificates / "one.pem"),
        "key": str(certificates / "one.key"),
        "ca": str(certificates / "ca.pem"),
    }
    configuration: dict[str, Any] = CONTEXT | {
        "node": 1,
        "voters": voters,
        "directory": str(directory / "state"),
        "peer_tls": tls,
        "publisher_tls": tls,
        "publisher_bind": "::",
        "publisher_name": "localhost",
        "credentials": [{"token": TOKEN, "producer": "writer", "namespaces": ["ns"]}],
    }
    if sys.platform.startswith("linux"):
        configuration["storage_backend"] = "native"
    path = directory / "authority.json"
    path.write_text(json.dumps(configuration))
    path.chmod(0o600)
    return path


def relay_configuration(certificates: Path, directory: Path, port: int) -> Path:
    upstream = CONTEXT | {
        "principal": "reader",
        "policy_epoch": 1,
        "namespaces": ["ns"],
        "token": TOKEN,
        "authority_endpoints": [{"address": "::1", "port": free_port()}],
        "hostname": "localhost",
        "ca": str(certificates / "ca.pem"),
        "delivery_mode": "subscription",
        "image_format": "paged-v2",
        "timeout_ms": 2000,
    }
    source = directory / "upstream.json"
    source.write_text(json.dumps(upstream))
    source.chmod(0o600)
    configuration = {
        "address": "::",
        "port": port,
        "hostname": "localhost",
        "certificate": str(certificates / "one.pem"),
        "private_key": str(certificates / "one.key"),
        "ca": str(certificates / "ca.pem"),
        "bundles": [
            {"source": str(source), "directory": str(directory / "generations"), "mode": "fresh"}
        ],
        "credentials": [
            {"token": "cd" * 16, "principal": "reader", "policy_epoch": 1, "bundles": [0]}
        ],
    }
    path = directory / "relay.json"
    path.write_text(json.dumps(configuration))
    path.chmod(0o600)
    return path


def main() -> int:
    detentd, relay, certificates = (Path(argument).resolve() for argument in sys.argv[1:4])
    if not has_ipv6():
        print("skip: no ::1 on this host")
        return 77
    with socket.socket(socket.AF_INET6) as probe:
        v4_too = not probe.getsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY)
    directory = Path.cwd() / ".scratch" / f"dual-stack-{time.monotonic_ns()}"
    directory.mkdir(parents=True)
    context = ssl.create_default_context(cafile=str(certificates / "ca.pem"))
    ports = [free_port() for _ in range(5)]
    commands = {
        "authority": [
            str(detentd),
            "--authority",
            str(authority_configuration(certificates, directory, ports[:3])),
            "--port",
            str(ports[3]),
            "--runtime-dir",
            str(directory / "runtime"),
        ],
        "relay": [
            str(relay),
            "--config",
            str(relay_configuration(certificates, directory, ports[4])),
            "--runtime-dir",
            str(directory / "relay-run"),
        ],
    }
    served = {"authority": ports[3], "relay": ports[4]}
    children: dict[str, subprocess.Popen[bytes]] = {}
    try:
        for name, command in commands.items():
            log = (directory / f"{name}.log").open("wb")
            children[name] = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
        for name, port in served.items():
            wait_health("::1", port, context, children[name])
            families = ["IPv6"]
            # The OS default decides whether a plain v6 socket takes IPv4; Detent must not depend on it.
            health("127.0.0.1", port, context)
            families.append("IPv4")
            print(f"{name} bound to [::]: served {' and '.join(families)} clients", flush=True)
        print(f"host v6_only default: {'off' if v4_too else 'on'}", flush=True)
    except BaseException:
        for log in sorted(directory.glob("*.log")):
            print(f"--- {log.name}\n{log.read_text(errors='replace')[-2000:]}", flush=True)
        raise
    finally:
        for child in children.values():
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
