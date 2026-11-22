"""A SIGTERM under write load: the agent drains within its bound, exits 0, and restarts from a
whole generation.

An authority voter takes a stream of rewrites while one managed agent follows it. The agent gets
SIGTERM mid-stream and must exit 0 within --shutdown-timeout-ms (the bound the agent is started
with); the writes then finish, the agent restarts on the store it left, converges to the final
position, and the independent SDK oracle accepts every value.
"""

from __future__ import annotations

import argparse
import asyncio
import signal
import sys
import time
from pathlib import Path

from authority import Authority
from managed import ManagedCohort
from run_relay_pages import Reported, check, rewrite, seed, widen
from runtime import Actors, Evidence
from writers import Writers


async def run(arguments: argparse.Namespace) -> None:
    output = arguments.output.resolve()
    output.mkdir(parents=True, mode=0o700, exist_ok=False)
    evidence = Evidence(output / "events.jsonl", 64 * 1024 * 1024)
    certificates = arguments.certificates.resolve()
    async with Actors(evidence, 30) as actors, Reported():
        authority = Authority(
            arguments.binary.resolve(), certificates, output, actors, 1
        )
        widen(output)
        await authority.start()
        writers = Writers(authority, arguments.records, 1, 1)
        await seed(writers)
        directory = output / "agent"
        directory.mkdir(mode=0o700)
        cohort = ManagedCohort(
            authority,
            actors,
            directory,
            arguments.agents.resolve(),
            arguments.local_oracle.resolve(),
            certificates,
            1,
            1,
            source_timeout_ms=60000,
            selection_timeout=120,
            shutdown_timeout_ms=arguments.bound,
        )
        await cohort.start()
        await cohort.verify(writers)
        ordinals = list(range(writers.records))
        stream = asyncio.create_task(_stream(writers, ordinals, arguments.rounds))
        await asyncio.sleep(0.5)
        process = cohort.members[0].process
        began = time.monotonic()
        process.send_signal(signal.SIGTERM)
        code = await asyncio.wait_for(process.wait(), arguments.bound / 1000 + 10)
        elapsed = time.monotonic() - began
        check(code == 0, f"the agent exited with status {code}")
        check(
            elapsed * 1000 <= arguments.bound,
            f"the stop took {elapsed:.1f} s, over {arguments.bound} ms",
        )
        await stream
        await cohort.restart_agent(0)
        await cohort.verify(writers)
        print(
            f"agent SIGTERM under write load: exit 0 in {elapsed:.2f} s, "
            "restart converged and verified",
            flush=True,
        )
        await cohort.close()
    print("agent sigterm: ok", flush=True)


async def _stream(writers: Writers, ordinals: list[int], rounds: int) -> None:
    for step in range(rounds):
        await rewrite(writers, ordinals, 1)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True, help="detent-authority")
    parser.add_argument("--agents", type=Path, required=True, help="detent-agent")
    parser.add_argument("--local-oracle", type=Path, required=True)
    parser.add_argument("--certificates", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--records", type=int, default=2000)
    parser.add_argument("--rounds", type=int, default=20)
    parser.add_argument("--bound", type=int, default=10000, help="milliseconds")
    arguments = parser.parse_args()
    try:
        asyncio.run(run(arguments))
    except Exception as error:
        print(f"run_agent_sigterm: {error!r}", file=sys.stderr, flush=True)
        raise SystemExit(1) from error


if __name__ == "__main__":
    main()
