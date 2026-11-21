"""A three-voter authority's SIGTERM-to-exit time at a given state size (F13).

usage: stop_scale.py --build BUILD --label LABEL --out OUT --keys KEYS
       stop_scale.py --check OUT

A run starts three voters (tests/detent_system/authority.py), writes KEYS keys across four producers,
waits for the followers to apply them, then SIGTERMs each follower and the leader in turn and
records each one's seconds from the signal to exit in OUT/LABEL.json. The stop loop retires the
state in budgeted visits of about 32 nodes, so a 1 ms sleep per visit costs seconds at 100k keys.

--check reads every head-* and main-* result in OUT and fails when head's slowest stop exceeds
twice main's slowest plus 0.5 s.
"""

from __future__ import annotations

import argparse
import asyncio
import json
import os
import shutil
import signal
import subprocess
import sys
import time
from pathlib import Path

SOURCE = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(SOURCE / "tests" / "detent_system"))
from authority import Authority
from runtime import Actors, Evidence
from writers import Writers

PRODUCERS = 4


def certificates(directory: Path) -> Path:
    if not (directory / "ca.pem").exists():
        subprocess.run(
            [
                "cmake",
                f"-DOUTPUT={directory}",
                f"-DOPENSSL={shutil.which('openssl')}",
                "-P",
                str(SOURCE / "tests/detent_authority/tls_certificates.cmake"),
            ],
            check=True,
            stdout=subprocess.DEVNULL,
            env={
                **os.environ,
                "OPENSSL_CONF": str(SOURCE / "tests/detent_authority/tls_ambient.cnf"),
            },
        )
    return directory


async def run(build: Path, label: str, out: Path, keys: int) -> int:
    root = out / label
    shutil.rmtree(root, ignore_errors=True)
    root.mkdir(parents=True)
    certs = certificates(out / "certificates")
    evidence = Evidence(root / "events.jsonl", 64 * 1024 * 1024)
    async with Actors(evidence, 120) as actors:
        group = Authority(
            build / "src" / "detent-authority", certs, root, actors, PRODUCERS
        )
        await group.start()
        started = time.monotonic()
        # Each producer opens its session and writes its share in batches of 128.
        writers = Writers(group, keys, PRODUCERS, 0)
        await asyncio.gather(*(writers.prepare(p) for p in range(PRODUCERS)))
        wrote = time.monotonic() - started
        await asyncio.sleep(3)  # the followers apply
        leader = await group.leader()
        stops = []
        for index in [i for i in range(3) if i != leader] + [leader]:
            member = group.members[index]
            signalled = time.monotonic()
            member.process.send_signal(signal.SIGTERM)
            await member.process.wait()
            role = "leader" if index == leader else "follower"
            stops.append(
                {
                    "role": role,
                    "seconds": time.monotonic() - signalled,
                    "exit": member.process.returncode,
                }
            )
            group.excluded.add(index)
    result = {"label": label, "keys": keys, "write_seconds": wrote, "stops": stops}
    (out / f"{label}.json").write_text(json.dumps(result))
    print(json.dumps(result))
    return 0 if all(stop["exit"] == 0 for stop in stops) else 2


def check(out: Path) -> int:
    slowest: dict[str, float] = {}
    for path in sorted(out.glob("*.json")):
        result = json.loads(path.read_text())
        side = result["label"].split("-")[0]
        slowest[side] = max(
            slowest.get(side, 0.0), *(stop["seconds"] for stop in result["stops"])
        )
    if "head" not in slowest or "main" not in slowest:
        print(f"missing results: {slowest}")
        return 2
    bound = 2 * slowest["main"] + 0.5
    verdict = "pass" if slowest["head"] <= bound else "FAIL"
    print(
        f"slowest stop: head {slowest['head']:.3f} s, main {slowest['main']:.3f} s, bound {bound:.3f} s: {verdict}"
    )
    return 0 if verdict == "pass" else 1


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", type=Path)
    parser.add_argument("--label")
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--keys", type=int, default=100_000)
    parser.add_argument("--check", action="store_true")
    arguments = parser.parse_args()
    if arguments.check:
        return check(arguments.out)
    # The voters run in their own directories, so their configuration needs absolute paths.
    build, out = arguments.build.resolve(), arguments.out.resolve()
    return asyncio.run(run(build, arguments.label, out, arguments.keys))


if __name__ == "__main__":
    sys.exit(main())
