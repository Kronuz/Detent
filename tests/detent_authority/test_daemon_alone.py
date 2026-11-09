"""A fixed three-voter node started alone: --daemonize returns promptly, --status reports not ready."""

from __future__ import annotations

import hashlib
import json
import os
import re
import shutil
import signal
import socket
import ssl
import subprocess
import sys
import time
from pathlib import Path


def main() -> None:
    binary = Path(sys.argv[1]).resolve()
    certificates = Path(sys.argv[2]).resolve()
    directory = Path.cwd() / ".scratch" / f"authority-alone-{os.getpid()}"
    directory.mkdir(parents=True)
    reservations = [socket.socket() for _ in range(4)]
    for reservation in reservations:
        reservation.bind(("127.0.0.1", 0))
    ports = [reservation.getsockname()[1] for reservation in reservations]
    voters = []
    for index, name in enumerate(["one", "two", "three"]):
        der = ssl.PEM_cert_to_DER_cert((certificates / f"{name}.pem").read_text())
        voters.append(
            {
                "id": index + 1,
                "host": "127.0.0.1",
                "port": ports[index],
                "certificate_sha256": hashlib.sha256(der).hexdigest(),
            }
        )
    tls = {
        "certificate": str(certificates / "one.pem"),
        "key": str(certificates / "one.key"),
        "ca": str(certificates / "ca.pem"),
    }
    configuration = {
        "cluster": "63" + "00" * 15,
        "group": "67" + "00" * 15,
        "epoch": "65" + "00" * 15,
        "node": 1,
        "voters": voters,
        "directory": str(directory / "state"),
        "peer_tls": tls,
        "publisher_tls": tls,
        "publisher_bind": "127.0.0.1",
        "publisher_name": "localhost",
        "credentials": [
            {"token": "0123456789abcdef0123456789abcdef", "producer": "writer", "namespaces": ["ns"]}
        ],
    }
    if sys.platform.startswith("linux"):
        configuration["storage_backend"] = "native"
    path = directory / "node.json"
    path.write_text(json.dumps(configuration))
    path.chmod(0o600)
    for reservation in reservations:
        reservation.close()
    common = [
        str(binary),
        "--authority",
        str(path),
        "--port",
        str(ports[3]),
        "--runtime-dir",
        str(directory / "runtime"),
    ]
    daemon = 0
    try:
        started = time.monotonic()
        result = subprocess.run(
            [*common, "--daemonize"], capture_output=True, text=True, timeout=25
        )
        elapsed = time.monotonic() - started
        assert result.returncode == 0, (result.returncode, result.stdout, result.stderr)
        assert elapsed < 10, f"--daemonize of a lone voter took {elapsed:.1f} s"
        status = subprocess.run(
            [*common, "--status"], capture_output=True, text=True, timeout=25
        )
        print(status.stdout.strip())
        assert status.returncode == 0, (status.stdout, status.stderr)
        match = re.search(r"running \(pid (\d+)\)", status.stdout)
        assert match, status.stdout
        daemon = int(match.group(1))
        assert '"ready":false' in status.stdout, status.stdout
        print(f"lone voter: --daemonize returned in {elapsed:.2f} s, --status ready:false")
    finally:
        if daemon:
            os.kill(daemon, signal.SIGTERM)
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                try:
                    os.kill(daemon, 0)
                except ProcessLookupError:
                    break
                time.sleep(0.05)
            else:
                os.kill(daemon, signal.SIGKILL)
        shutil.rmtree(directory, ignore_errors=True)


if __name__ == "__main__":
    main()
