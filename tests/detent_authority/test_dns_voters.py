"""Authority voters named by host name (D60): three daemon processes configured with DNS names.

usage: test_dns_voters.py DETENTD TLS_FIXTURE (names | unresolvable)
  names:        all three voters are "localhost" on distinct ports; the cluster elects a leader and
                serves a write over the names consensus resolves. Each named voter also
                accepts a connection over 127.0.0.1 and, on a dual-stack host, ::1.
  unresolvable: voter 3's host is a .invalid name (RFC 6761) that never resolves; all three daemons
                start, voters 1 and 2 form quorum, elect a leader, report ready and serve a write.
"""

from __future__ import annotations

import hashlib
import http.client
import json
import shutil
import socket
import ssl
import subprocess
import sys
import time
from pathlib import Path
from typing import Any

from test_multivoter import CONTEXT, TOKEN, request

NAMES = ["one", "two", "three"]


def configure(
    certificates: Path, directory: Path, hosts: list[str]
) -> tuple[list[int], list[Path]]:
    reservations = [socket.socket() for _ in range(6)]
    for reservation in reservations:
        reservation.bind(("127.0.0.1", 0))
    ports = [reservation.getsockname()[1] for reservation in reservations]
    voters = []
    for index, name in enumerate(NAMES):
        der = ssl.PEM_cert_to_DER_cert((certificates / f"{name}.pem").read_text())
        voters.append(
            {
                "id": index + 1,
                "host": hosts[index],
                "port": ports[index],
                "certificate_sha256": hashlib.sha256(der).hexdigest(),
            }
        )
    configurations = []
    for index, name in enumerate(NAMES):
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
                {"token": TOKEN, "producer": "writer", "namespaces": ["ns"]}
            ],
        }
        if sys.platform.startswith("linux"):
            configuration["storage_backend"] = "native"
        path = directory / f"node-{index}.json"
        path.write_text(json.dumps(configuration))
        path.chmod(0o600)
        configurations.append(path)
    for reservation in reservations:
        reservation.close()
    return ports, configurations


def dual_stack() -> bool:
    """True where an IPv6 socket takes IPv4 too and ::1 exists."""
    try:
        with socket.socket(socket.AF_INET6) as probe:
            if probe.getsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY):
                return False
            probe.bind(("::1", 0))
    except OSError:
        return False
    return True


def connects(address: str, port: int) -> bool:
    family = socket.AF_INET6 if ":" in address else socket.AF_INET
    try:
        with socket.socket(family) as peer:
            peer.settimeout(2)
            peer.connect((address, port))
    except OSError:
        return False
    return True


def check_dual_stack(port: int) -> None:
    """A named local voter accepts a peer over 127.0.0.1 and, where the OS is dual-stack, ::1."""
    assert connects("127.0.0.1", port), "named voter refused 127.0.0.1"
    if dual_stack():
        assert connects("::1", port), "named voter refused ::1 on a dual-stack host"
        print("named voter accepted 127.0.0.1 and ::1 (dual-stack ran)", flush=True)
    else:
        print(
            "named voter accepted 127.0.0.1 (no dual-stack here: ::1 skipped)",
            flush=True,
        )


def run(
    binary: Path, certificates: Path, directory: Path, hosts: list[str], scenario: str
) -> None:
    directory.mkdir(parents=True)
    ports, configurations = configure(certificates, directory, hosts)
    context = ssl.create_default_context(cafile=str(certificates / "ca.pem"))
    children: list[subprocess.Popen[bytes]] = []
    logs = []
    try:
        began = time.monotonic()
        for index, path in enumerate(configurations):
            log = (directory / f"node-{index}.log").open("ab")
            logs.append(log)
            children.append(
                subprocess.Popen(
                    [
                        str(binary),
                        "--authority",
                        str(path),
                        "--port",
                        str(ports[index + 3]),
                        "--runtime-dir",
                        str(directory / f"runtime-{index}"),
                    ],
                    stdout=log,
                    stderr=subprocess.STDOUT,
                )
            )

        def health(index: int) -> dict[str, Any] | None:
            if children[index].poll() is not None:
                raise AssertionError(f"node {index} exited at startup")
            try:
                return request(ports[index + 3], context, "/v1/health")[1]
            except (OSError, http.client.HTTPException, ValueError):
                return None

        deadline = time.monotonic() + 30
        while True:
            bodies = {index: health(index) for index in range(3)}
            started = all(body is not None for body in bodies.values())
            quorum = [
                index
                for index in (0, 1)
                if bodies[index] and bodies[index].get("ready") is True
            ]
            leaders = [
                index
                for index in (0, 1, 2)
                if bodies[index] and bodies[index].get("role") == 2
            ]
            if started and len(quorum) == 2 and len(leaders) == 1:
                break
            assert time.monotonic() < deadline, f"no leader and quorum: {bodies}"
            time.sleep(0.05)
        print(
            f"hosts {hosts}: ready after {time.monotonic() - began:.2f} s", flush=True
        )
        leader = leaders[0]
        status, receipt = request(
            ports[leader + 3],
            context,
            "/v2/open",
            {"expected_generation": 0, "nonce": "6e" + "00" * 15},
        )
        assert status == 200, (status, receipt)
        if scenario == "names":
            check_dual_stack(ports[0])
    except BaseException:
        for log in directory.glob("*.log"):
            print(log.name, log.read_text(), file=sys.stderr)
        raise
    finally:
        for child in children:
            if child.poll() is None:
                child.terminate()
        for child in children:
            try:
                child.wait(timeout=5)
            except subprocess.TimeoutExpired:
                child.kill()
                child.wait(timeout=5)
        for log in logs:
            log.close()
        shutil.rmtree(directory, ignore_errors=True)


def main() -> None:
    binary = Path(sys.argv[1]).resolve()
    certificates = Path(sys.argv[2]).resolve()
    scenario = sys.argv[3]
    hosts = {
        "names": ["localhost"] * 3,
        "unresolvable": ["localhost", "localhost", "voter-3.invalid"],
    }[scenario]
    run(
        binary,
        certificates,
        Path.cwd() / ".scratch" / f"dns-voters-{scenario}",
        hosts,
        scenario,
    )
    print(f"dns voters {scenario}: passed")


if __name__ == "__main__":
    main()
