"""Run run_fleet.py with every relay port also held on [::1] by an echo listener.

usage: fleet_dial_fault.py RUN_FLEET_ARGUMENTS...

The echo returns a client's own bytes, as a socket connected to itself does: the fault behind
`[SSL: UNEXPECTED_MESSAGE]` in a fixture call that resolved "localhost" (test_localhost_dial.py).
The relays listen on 127.0.0.1 alone, so a fixture that dials the bound address passes.
"""

from __future__ import annotations

import runpy
import sys
from pathlib import Path

here = Path(__file__).resolve().parent
sys.path.insert(0, str(here))

import managed
from test_localhost_dial import Echo

start = managed.ManagedCohort.start
listeners: list[Echo] = []


async def squatted_start(self: managed.ManagedCohort) -> None:
    await start(self)
    listeners.extend(Echo(port) for port in self.relay_ports)
    print(f"fleet_dial_fault: [::1] echo on relay ports {self.relay_ports}", flush=True)


managed.ManagedCohort.start = squatted_start  # type: ignore[method-assign]
sys.argv[0] = str(here / "run_fleet.py")
try:
    runpy.run_path(sys.argv[0], run_name="__main__")
finally:
    print(
        f"fleet_dial_fault: echo accepted {sum(echo.accepted for echo in listeners)}",
        flush=True,
    )
