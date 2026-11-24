"""Ports for test daemons that no other test process can take before the daemon binds them.

Peers name each other's ports in their configurations, so a daemon cannot bind port 0 and report
what it got: the harness must choose every port before the first start. Binding port 0 and closing
the socket leaves a gap in which another test (or an outgoing connection's ephemeral source port)
takes the number. `lease_ports` closes it two ways:

  1. Ports come from LOW..HIGH, below the ephemeral range of Linux (32768+) and macOS (49152+),
     so no connection and no daemon bound to port 0 lands in it.
  2. Each port is locked by flock on a file in a directory shared by every test process of the
     user, and the lock lasts until the leasing process exits, so no other test leases it.
"""

from __future__ import annotations

import fcntl
import os
import random
import socket
import tempfile
from pathlib import Path

LOW = 20000
HIGH = 30000
ATTEMPTS = 1000

# port -> lock descriptor, open until released or the process exits
_held: dict[int, int] = {}


def _directory() -> Path:
    directory = Path(
        os.environ.get("DETENT_TEST_PORT_DIR")
        or Path(tempfile.gettempdir()) / f"detent-test-ports-{os.getuid()}"
    )
    directory.mkdir(mode=0o700, parents=True, exist_ok=True)
    return directory


def _free(host: str, port: int) -> bool:
    """True when `host`:`port` binds now; "::" is probed for both families."""
    with socket.socket(socket.AF_INET6 if ":" in host else socket.AF_INET) as probe:
        if host == "::":
            probe.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 0)
        try:
            probe.bind((host, port))
        except OSError:
            return False
    return True


def lease_ports(count: int, host: str = "127.0.0.1") -> list[int]:
    """Returns `count` distinct ports that bind on `host` and are locked to this process."""
    directory = _directory()
    ports: list[int] = []
    for port in random.sample(range(LOW, HIGH), ATTEMPTS):
        if len(ports) == count:
            return ports
        descriptor = os.open(directory / str(port), os.O_CREAT | os.O_RDWR, 0o600)
        try:
            fcntl.flock(descriptor, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError:
            os.close(descriptor)
            continue
        if not _free(host, port):
            os.close(descriptor)
            continue
        _held[port] = descriptor
        ports.append(port)
    if len(ports) == count:
        return ports
    raise RuntimeError(f"no {count} free ports in {LOW}-{HIGH} on {host}")


def release_ports(ports: list[int]) -> None:
    """Unlocks leased ports once the test is done with them (they unlock at exit anyway)."""
    for port in ports:
        if (descriptor := _held.pop(port, None)) is not None:
            os.close(descriptor)
