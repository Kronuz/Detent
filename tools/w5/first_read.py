#!/usr/bin/env python3
"""A/B of the managed first read on a saved 5M store: old build against new build, with an A/A control.

  first_read.py prepare --old BUILD --new BUILD --source AGENT_DIR --ca CA.pem --work DIR
  first_read.py measure --old BUILD --new BUILD --work DIR --cases CASES.json --rounds 5

`prepare` clones the saved agent store once per arm (`cp -cR`), runs that arm's detent-agent
against the clone until "managed local store ready" (startup checks the table's entry and its
`.read` and `.live`) and stops it. `measure` then
reads each case in a fresh process: a warmup process, then the measured one, for the old probe on the old
store (A), the new probe on the new store (B) and the old probe on the old store again (AA). The three arms
rotate their order for every case. Each round writes one file per arm, `DIR/series/{a,b,aa}/run-RRR.txt`, of rows
`caseNN <first read in us> us`; compare them with
  ab_compare.py compare --a DIR/series/a --b DIR/series/b --aa DIR/series/aa
The raw probe JSON of every read is in DIR/samples.jsonl and the count below 1 ms per arm in DIR/summary.txt.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import signal
import socket
import statistics
import subprocess
import sys
import time
from pathlib import Path

PROBE = "tests/detent_system/detent_system_prepared_client_probe"
AGENT = "src/detent-agent"


def sha256(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def clean_env() -> dict[str, str]:
    return {
        k: v for k, v in os.environ.items() if not k.startswith(("DETENT_", "DYLD_", "LD_PRELOAD"))
    }


def prepare_arm(name: str, build: Path, source: Path, ca: Path, work: Path) -> None:
    arm = work / name
    if arm.exists():
        raise SystemExit(f"{arm} exists; remove it first")
    arm.mkdir(parents=True)
    subprocess.run(["cp", "-cR", str(source / "generations"), str(arm / "generations")], check=True)
    if not (arm / "generations" / "selected").is_file():
        raise SystemExit("the saved directory is not an agent store: it has no table")
    managed = json.loads((source / "managed.json").read_text())
    managed["ca"] = str(ca.absolute())
    managed.pop("image_format", None)
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as unavailable:
        unavailable.bind(("127.0.0.1", 0))
        managed["address"] = "127.0.0.1"
        managed["port"] = unavailable.getsockname()[1]
        (arm / "managed.json").write_text(json.dumps(managed, indent=2) + "\n")
        (arm / "managed.json").chmod(0o600)
        (arm / "agent.json").write_text(
            json.dumps(
                {
                    "managed_path": "managed.json",
                    "generation_dir": "generations",
                    "runtime_dir": "runtime",
                }
            )
            + "\n"
        )
        before = {p.name for p in (arm / "generations").iterdir()}
        log = arm / "agent.log"
        started = time.monotonic()
        with log.open("wb") as stream:
            process = subprocess.Popen(
                [str(build / AGENT), "--config", str(arm / "agent.json")],
                stdin=subprocess.DEVNULL,
                stdout=stream,
                stderr=subprocess.STDOUT,
                env=clean_env(),
            )
            try:
                while time.monotonic() - started < 400:
                    if process.poll() is not None:
                        raise SystemExit(f"agent exited early; see {log}")
                    if (
                        b"managed local store ready, selected generation"
                        in log.read_bytes()[-65536:]
                    ):
                        break
                    time.sleep(0.1)
                else:
                    raise SystemExit(f"agent not ready in 400 s; see {log}")
                ready = time.monotonic() - started
            finally:
                if process.poll() is None:
                    process.send_signal(signal.SIGTERM)
                    try:
                        process.wait(timeout=60)
                    except subprocess.TimeoutExpired:
                        process.kill()
    after = {p.name for p in (arm / "generations").iterdir()}
    added = sorted(after - before)
    print(f"{name}: ready in {ready:.1f} s; new files {added}")
    with (work / "prepared.txt").open("a") as out:
        out.write(f"{name} ready {ready:.1f}s\n")
        for n in [*sorted(added), "selected"]:
            p = arm / "generations" / n
            if p.is_file():
                out.write(f"{name} {n} {p.stat().st_size} {sha256(p)}\n")


def read_once(build: Path, directory: Path, key: str, expected: int) -> dict[str, float]:
    result = subprocess.run(
        [str(build / PROBE), "--read", str(directory), key, str(expected)],
        capture_output=True,
        check=False,
        text=True,
        timeout=60,
        env=clean_env(),
    )
    if result.returncode:
        raise RuntimeError(f"probe failed: {result.stderr[:300]}")
    return json.loads(result.stdout)


def measure(old: Path, new: Path, work: Path, cases: list[dict], rounds: int) -> None:
    arms = {
        "a": (old, work / "old" / "generations"),
        "b": (new, work / "new" / "generations"),
        "aa": (old, work / "old" / "generations"),
    }
    series = work / "series"
    for tag in arms:
        (series / tag).mkdir(parents=True, exist_ok=True)
    samples = (work / "samples.jsonl").open("a")
    rows: dict[str, list[float]] = {tag: [] for tag in arms}
    started = time.monotonic()
    for round_number in range(1, rounds + 1):
        lines: dict[str, list[str]] = {tag: [] for tag in arms}
        for index, case in enumerate(cases):
            order = list(arms)
            shift = (round_number * len(cases) + index) % len(order)
            order = order[shift:] + order[:shift]
            for tag in order:
                build, directory = arms[tag]
                read_once(build, directory, case["key"], case["expected_int"])
                sample = read_once(build, directory, case["key"], case["expected_int"])
                micros = sample["first_live_ns"] / 1000
                rows[tag].append(micros)
                lines[tag].append(f"case{index:02d} {micros:.3f} us")
                samples.write(
                    json.dumps({"arm": tag, "round": round_number, "case": index, **sample}) + "\n"
                )
        for tag in arms:
            (series / tag / f"run-{round_number:03d}.txt").write_text("\n".join(lines[tag]) + "\n")
        print(
            f"round {round_number}/{rounds} done at {time.monotonic() - started:.0f} s", flush=True
        )
    samples.close()
    with (work / "summary.txt").open("w") as out:
        for tag, values in rows.items():
            under = sum(v < 1000 for v in values)
            text = f"{tag}: {under}/{len(values)} first reads below 1 ms; median {statistics.median(values):.1f} us; max {max(values):.1f} us"
            print(text)
            out.write(text + "\n")
        out.write(f"binaries old probe {sha256(old / PROBE)} new probe {sha256(new / PROBE)}\n")
        out.write(f"wall seconds {time.monotonic() - started:.0f}\n")


def cross(old: Path, new: Path, work: Path, cases: list[dict]) -> None:
    """Each build's probe reads the store the other build's agent prepared."""
    for probe, store in ((new, "old"), (old, "new"), (old, "old"), (new, "new")):
        bad = 0
        for case in cases[:10]:
            sample = read_once(
                probe, work / store / "generations", case["key"], case["expected_int"]
            )
            bad += not sample
        which = "new" if probe == new else "old"
        print(f"{which} probe on the {store} store: 10 reads, {bad} failures")


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("action", choices=("prepare", "measure", "cross"))
    parser.add_argument("--old", type=Path, required=True)
    parser.add_argument("--new", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--source", type=Path)
    parser.add_argument("--ca", type=Path)
    parser.add_argument("--cases", type=Path)
    parser.add_argument("--rounds", type=int, default=5)
    parser.add_argument("--only", choices=("old", "new"), help="prepare one arm")
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    if args.action == "prepare":
        if not (args.source and args.ca):
            raise SystemExit("prepare needs --source and --ca")
        for name, build in (("old", args.old), ("new", args.new)):
            if args.only in (None, name):
                prepare_arm(name, build, args.source, args.ca, args.work)
        return 0
    cases = json.loads(args.cases.read_text())["cases"] if args.cases else []
    if args.action == "measure":
        measure(args.old, args.new, args.work, cases, args.rounds)
    else:
        cross(args.old, args.new, args.work, cases)
    return 0


if __name__ == "__main__":
    sys.exit(main())
