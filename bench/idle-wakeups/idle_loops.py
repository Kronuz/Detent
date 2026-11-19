"""Count an idle single-voter detentd's and an idle detent-relay's loop wakeups (loop-idle-waits A1).

With no client, once both report ready and have settled, the script reads over --seconds each
process's rusage (interrupt and package-idle wakeups, CPU) through the usage helper, the
authority's owner turns (`owner_turn_ns.count` in /v1/health) and the relay's turns (`turns`,
where its build reports it). It prints one JSON line.

    idle_loops.py --build BUILD --usage USAGE_HELPER --label NAME [--seconds 10]
"""

from __future__ import annotations

import argparse
import json
import subprocess
import time
from pathlib import Path
from typing import Any

from pair import Pair


def usage(helper: Path, pid: int) -> dict[str, int]:
    line = subprocess.check_output([str(helper), str(pid)], text=True).strip()
    return {
        key: int(value) for key, value in (part.split("=", 1) for part in line.split())
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--usage", type=Path, required=True)
    parser.add_argument("--label", required=True)
    parser.add_argument("--seconds", type=float, default=10)
    args = parser.parse_args()
    with Pair(args.build.resolve()) as pair:
        time.sleep(2)

        def sample() -> dict[str, Any]:
            authority = pair.health(pair.authority_port) or {}
            relay = pair.health(pair.relay_port) or {}
            return {
                "detentd": usage(args.usage, pair.children["detentd"].pid),
                "relay": usage(args.usage, pair.children["relay"].pid),
                "owner_turns": authority["owner_turn_ns"]["count"],
                "relay_turns": relay.get("turns"),
                "at": time.monotonic(),
            }

        before = sample()
        time.sleep(args.seconds)
        after = sample()
    seconds = after["at"] - before["at"]
    result: dict[str, Any] = {"label": args.label, "seconds": round(seconds, 3)}
    for name in ("detentd", "relay"):
        delta = {key: after[name][key] - before[name][key] for key in after[name]}
        result[name] = {
            "interrupt_wakeups_per_s": round(delta["interrupt"] / seconds, 1),
            "package_idle_wakeups_per_s": round(delta["package_idle"] / seconds, 1),
            "cpu_percent_of_core": round(
                100 * (delta["user_ns"] + delta["system_ns"]) / 1e9 / seconds, 3
            ),
        }
    result["detentd"]["owner_turns_per_s"] = round(
        (after["owner_turns"] - before["owner_turns"]) / seconds, 1
    )
    if after["relay_turns"] is not None:
        result["relay"]["turns_per_s"] = round(
            (after["relay_turns"] - before["relay_turns"]) / seconds, 1
        )
    print(json.dumps(result, sort_keys=True), flush=True)


if __name__ == "__main__":
    main()
