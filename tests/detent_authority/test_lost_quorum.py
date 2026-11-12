"""A node without a leader or a quorum reports "ready":false within election_max plus a health interval (D41).

usage: test_lost_quorum.py DETENTD TLS_FIXTURE
Three scenarios, each on a fresh three-voter cluster in the foreground:
  A: kill the leader and one follower; the surviving follower still names the dead leader (pre-vote
     keeps it) and must stop reporting ready.
  B: kill both followers; the leader steps down through CheckQuorum and must stop reporting ready.
  C: kill the leader only; both survivors must report ready again under a new leader.
"""

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
from typing import Any

TOKEN = "0123456789abcdef0123456789abcdef"
CONTEXT = {
    "cluster": "63" + "00" * 15,
    "group": "67" + "00" * 15,
    "epoch": "65" + "00" * 15,
}
# election_max is 2 s and health is republished every 250 ms: ready:false must land within BOUND (the
# slack covers host load) and then hold for HOLD.
BOUND = 5.0
HOLD = 4.0


def health(port: int, context: ssl.SSLContext) -> dict[str, Any] | None:
    connection = http.client.HTTPSConnection(
        "localhost", port, timeout=2, context=context
    )
    try:
        connection.request("GET", "/v1/health", headers={"Connection": "close"})
        return json.loads(connection.getresponse().read())
    except (OSError, http.client.HTTPException, ValueError):
        return None
    finally:
        connection.close()


class Cluster:
    def __init__(self, binary: Path, certificates: Path, directory: Path) -> None:
        self.directory = directory
        directory.mkdir(parents=True)
        reservations = [socket.socket() for _ in range(6)]
        for reservation in reservations:
            reservation.bind(("127.0.0.1", 0))
        self.ports = [reservation.getsockname()[1] for reservation in reservations]
        names = ["one", "two", "three"]
        voters = []
        for index, name in enumerate(names):
            der = ssl.PEM_cert_to_DER_cert((certificates / f"{name}.pem").read_text())
            voters.append(
                {
                    "id": index + 1,
                    "host": "127.0.0.1",
                    "port": self.ports[index],
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
        for reservation in reservations:
            reservation.close()
        self.context = ssl.create_default_context(cafile=str(certificates / "ca.pem"))
        self.children: dict[int, subprocess.Popen[bytes]] = {}
        for index in range(3):
            log = (directory / f"node-{index}.log").open("ab")
            self.children[index] = subprocess.Popen(
                [
                    str(binary),
                    "--authority",
                    str(directory / f"node-{index}.json"),
                    "--port",
                    str(self.ports[index + 3]),
                    "--runtime-dir",
                    str(directory / f"runtime-{index}"),
                ],
                stdout=log,
                stderr=subprocess.STDOUT,
            )

    def health(self, index: int) -> dict[str, Any] | None:
        return health(self.ports[index + 3], self.context)

    def all_ready(self, timeout: float = 30) -> int:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            bodies = {i: self.health(i) for i in range(3)}
            if all(b and b.get("ready") is True for b in bodies.values()):
                leaders = [i for i, b in bodies.items() if b and b.get("role") == 2]
                if len(leaders) == 1:
                    return leaders[0]
            time.sleep(0.05)
        raise AssertionError(f"cluster never all ready: {bodies}")

    def kill(self, index: int) -> None:
        self.children[index].kill()
        self.children[index].wait(timeout=10)

    def close(self) -> None:
        for child in self.children.values():
            if child.poll() is None:
                child.kill()
                child.wait(timeout=10)
        shutil.rmtree(self.directory, ignore_errors=True)


def watch(cluster: Cluster, index: int, label: str) -> bool:
    """True iff the node reports ready:false within BOUND seconds of the kill and never ready:true after."""
    started = time.monotonic()
    first_false: float | None = None
    late_true = []
    samples = 0
    last: dict[str, Any] | None = None
    while time.monotonic() - started < BOUND + HOLD:
        body = cluster.health(index)
        now = time.monotonic() - started
        if body is not None:
            samples += 1
            last = body
            if body.get("ready") is False and first_false is None:
                first_false = now
            if body.get("ready") is True and first_false is not None:
                late_true.append(round(now, 2))
        time.sleep(0.1)
    summary = (
        {k: last.get(k) for k in ("ready", "role", "term", "fenced")} if last else None
    )
    print(
        f"{label}: node {index} samples={samples} first ready:false at "
        f"{None if first_false is None else round(first_false, 2)} s; ready:true after it in "
        f"{len(late_true)} samples; last={summary}",
        flush=True,
    )
    return first_false is not None and first_false <= BOUND and not late_true


def main() -> int:
    binary = Path(sys.argv[1]).resolve()
    certificates = Path(sys.argv[2]).resolve()
    root = Path.cwd() / ".scratch" / f"lost-quorum-{os.getpid()}"
    ok = True
    for scenario in ("A", "B", "C"):
        cluster = Cluster(binary, certificates, root / scenario)
        try:
            leader = cluster.all_ready()
            followers = [i for i in range(3) if i != leader]
            print(
                f"{scenario}: leader node {leader}, followers {followers}", flush=True
            )
            if scenario == "A":
                cluster.kill(leader)
                cluster.kill(followers[0])
                ok &= watch(
                    cluster, followers[1], "A survivor follower (no leader anywhere)"
                )
            elif scenario == "B":
                cluster.kill(followers[0])
                cluster.kill(followers[1])
                ok &= watch(cluster, leader, "B isolated leader")
            else:
                cluster.kill(leader)
                deadline = time.monotonic() + 20
                recovered = False
                while time.monotonic() < deadline:
                    bodies = [cluster.health(i) for i in followers]
                    if all(b and b.get("ready") is True for b in bodies) and sorted(
                        b.get("role") for b in bodies if b
                    ) == [0, 2]:
                        recovered = True
                        break
                    time.sleep(0.05)
                print(
                    f"C: survivors {followers} recovered ready under a new leader: {recovered}; "
                    f"{[{k: b.get(k) for k in ('ready', 'role', 'term')} if b else None for b in bodies]}",
                    flush=True,
                )
                ok &= recovered
        finally:
            cluster.close()
    if not ok:
        print(
            "FAIL: a node without a leader or quorum kept reporting ready",
            file=sys.stderr,
        )
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
