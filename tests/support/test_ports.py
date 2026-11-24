"""lease_ports hands a port to one process at a time and only ports a bind accepts.

usage: test_ports.py
"""

from __future__ import annotations

import os
import re
import socket
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent

# Leases the single port LOW=PORT in a fresh process; prints "ok" or the error.
CHILD = """
import sys
sys.path.insert(0, sys.argv[1])
import ports
ports.LOW, ports.HIGH, ports.ATTEMPTS = int(sys.argv[2]), int(sys.argv[2]) + 1, 1
try:
    print(ports.lease_ports(1))
except RuntimeError as error:
    print("refused")
"""


class LeasePorts(unittest.TestCase):
    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        os.environ["DETENT_TEST_PORT_DIR"] = self.directory.name
        sys.path.insert(0, str(HERE))
        import ports

        self.ports = ports
        self.addCleanup(self.directory.cleanup)

    def lease_elsewhere(self, port: int) -> str:
        return subprocess.run(
            [sys.executable, "-c", CHILD, str(HERE), str(port)],
            capture_output=True,
            text=True,
            check=True,
            env=os.environ,
        ).stdout.strip()

    def test_a_leased_port_is_refused_to_another_process_until_released(self) -> None:
        (port,) = self.ports.lease_ports(1)
        self.assertEqual(self.lease_elsewhere(port), "refused")
        self.ports.release_ports([port])
        self.assertEqual(self.lease_elsewhere(port), f"[{port}]")

    def test_a_port_that_does_not_bind_is_not_leased(self) -> None:
        with socket.socket() as taken:
            taken.bind(("127.0.0.1", 0))
            taken.listen()
            port = taken.getsockname()[1]
            self.assertEqual(self.lease_elsewhere(port), "refused")

    def test_leases_come_from_below_the_ephemeral_range(self) -> None:
        leased = self.ports.lease_ports(20)
        self.assertEqual(len(set(leased)), 20)
        self.assertTrue(all(20000 <= port < 30000 for port in leased))

    def test_no_test_reserves_a_port_and_releases_it_for_a_daemon(self) -> None:
        # Sockets bound to port 0 that stay open while used (the first two) and the ::1 probes
        # that only ask whether IPv6 exists are the other uses.
        held = {"first_read_gate.py", "test_localhost_dial.py", Path(__file__).name}
        bind = re.compile(r"\.bind\(\((?!\"::1\")[^)]*, ?0\)\)")
        offenders = [
            f"{path.relative_to(HERE.parent)}:{number}"
            for path in sorted(HERE.parent.rglob("*.py"))
            if path.name not in held and path.name != "ports.py"
            for number, line in enumerate(path.read_text().splitlines(), 1)
            if bind.search(line)
        ]
        self.assertEqual(offenders, [])


if __name__ == "__main__":
    unittest.main()
