"""Qualify real managed preparation and fresh reads of the preserved 5M corpus."""

from __future__ import annotations

import argparse
import fcntl
import hashlib
import json
import os
import platform
import re
import resource
import signal
import socket
import subprocess
import sys
import time
from collections.abc import Generator
from contextlib import contextmanager
from datetime import UTC, datetime
from pathlib import Path
from types import FrameType
from typing import Any, cast

REPO = Path(__file__).resolve().parents[2]
SOURCE = REPO / ".scratch/reader-capsule-20261029/canonical-source"
BUILD = REPO / ".scratch/reader-capsule-20261029/canonical-build"
SAVED = REPO / ".scratch/fast-client-build/managed-5m-r3"
AGENT = BUILD / "src/detent-agent"
READER = BUILD / "src/detent"
PROBE = BUILD / "tests/detent_system/detent_system_prepared_client_probe"
PROBES = {
    "private": REPO
    / ".scratch/reader-capsule-20261029/private-build/tests/detent_system/detent_system_prepared_client_probe",
    "canonical": PROBE,
}
ACTIVE_PUBLISHER: subprocess.Popen[bytes] | None = None


def load_object(path: Path) -> dict[str, Any]:
    value: Any = json.loads(path.read_text())
    if not isinstance(value, dict):
        raise TypeError(f"expected object in {path}")
    return value


def write_json(path: Path, value: object, mode: int = 0o600) -> None:
    path.write_text(json.dumps(value, indent=2) + "\n")
    path.chmod(mode)


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def output(args: list[str], env: dict[str, str]) -> str:
    result = subprocess.run(
        args, env=env, capture_output=True, text=True, timeout=30, check=False
    )
    if result.returncode:
        raise RuntimeError(
            f"command failed ({result.returncode}): {args[0]}: {result.stderr[:500]}"
        )
    return result.stdout


def fresh_read(
    directory: Path, key: str, expected: int, env: dict[str, str]
) -> dict[str, Any]:
    value: Any = json.loads(
        output([str(PROBE), "--read", str(directory), key, str(expected)], env)
    )
    if not isinstance(value, dict) or value.get("records") != 5_000_000:
        raise RuntimeError("fresh probe did not verify the 5M generation")
    return value


@contextmanager
def publisher(
    public: Path, log: Path, env: dict[str, str]
) -> Generator[float, None, None]:
    global ACTIVE_PUBLISHER
    started = time.monotonic()
    with log.open("wb") as stream:
        process = subprocess.Popen(
            [
                sys.executable,
                str(Path(__file__).resolve()),
                "--supervise",
                str(os.getpid()),
                str(log.with_suffix(".child.json")),
                str(AGENT),
                "--config",
                str(public),
            ],
            stdin=subprocess.DEVNULL,
            stdout=stream,
            stderr=subprocess.STDOUT,
            env=env,
        )
        failure: BaseException | None = None
        try:
            while time.monotonic() - started < 210:
                if process.poll() is not None:
                    raise RuntimeError(f"publisher exited before readiness; see {log}")
                with log.open("rb") as reader:
                    reader.seek(max(0, log.stat().st_size - 65536))
                    if (
                        b"managed local store ready, selected generation"
                        in reader.read()
                    ):
                        break
                time.sleep(0.05)
            else:
                raise TimeoutError(
                    f"publisher readiness exceeded controller bound; see {log}"
                )
            ACTIVE_PUBLISHER = process
            yield time.monotonic() - started
        except BaseException as error:
            failure = error
            raise
        finally:
            ACTIVE_PUBLISHER = None
            if process.poll() is None:
                process.send_signal(signal.SIGTERM)
                try:
                    process.wait(timeout=60)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
                    raise RuntimeError(
                        "publisher exceeded shutdown drainage bound"
                    ) from None
            if process.returncode and failure is None:
                raise RuntimeError(f"publisher exit={process.returncode}; see {log}")


class Arguments(argparse.Namespace):
    def __init__(self) -> None:
        self.output: Path = (
            REPO / ".scratch/reader-capsule-20261029/managed-publisher-5m"
        )
        self.rounds: int = 5


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--rounds", type=int)
    args = Arguments()
    parser.parse_args(namespace=args)
    if sys.platform != "darwin":
        raise RuntimeError("This profile requires macOS APFS cloning and time-l units")
    if args.rounds <= 0:
        raise ValueError("rounds must be positive")
    subprocess.run(
        [
            "git",
            "-C",
            str(SOURCE),
            "diff",
            "HEAD",
            "--exit-code",
            "--",
            "src",
            "include",
            "tests",
        ],
        check=True,
    )
    args.output.mkdir(mode=0o700, parents=True, exist_ok=False)
    report: dict[str, object] = {
        "status": "incomplete",
        "rounds": args.rounds,
        "started_utc": datetime.now(UTC).isoformat(),
        "platform": platform.platform(),
        "script_sha256": digest(Path(__file__)),
        "method": "Real legacy-to-capsule managed publication and restart on cloned5M state. Alternating private/canonical readers per key/round; separate warmup before every measured process,100000 steady reads after first-read timing.40keys by5rounds per resource and publisher state. Live/stopped states are sequential. SDK timing excludes executable launch; time-l reports whole-reader maximumRSS. Supervised publisher hardbound300s plus60s drainage; publisher peakRSS from kernel child rusage.",
    }
    env = {
        k: v
        for k, v in os.environ.items()
        if not k.startswith(("DETENT_", "DYLD_", "LD_PRELOAD", "Malloc"))
    }
    samples: list[dict[str, Any]] = []
    try:
        report["code"] = (
            "Detent:"
            + output(["git", "-C", str(SOURCE), "rev-parse", "HEAD"], env).strip()
        )
        report["binaries"] = {
            "agent": digest(AGENT),
            "cli": digest(READER),
            **{name: digest(path) for name, path in PROBES.items()},
        }
        directory = args.output / "generations"
        raw = (SAVED / "generations/selected").read_bytes()
        if len(raw) != 176:
            raise RuntimeError("unexpected selected record")
        with (
            SAVED / "generations" / ("generation-" + raw[16:32].hex() + ".lease")
        ).open("rb") as lease:
            fcntl.flock(lease.fileno(), fcntl.LOCK_SH | fcntl.LOCK_NB)
            subprocess.run(
                ["cp", "-cR", str(SAVED / "generations"), str(directory)], check=True
            )
            if (SAVED / "generations/selected").read_bytes() != raw:
                raise RuntimeError("source selection changed")
        if not list(directory.glob("*.read")) or any(
            path.stat().st_size != 192 for path in directory.glob("*.read")
        ):
            raise RuntimeError("expected legacy prepared source")
        old_selection = digest(directory / "selected")
        packs = sorted(p.name for p in directory.glob("*.pack"))
        managed = load_object(SAVED / "managed.json")
        managed["ca"] = str((SAVED / managed["ca"]).absolute())
        managed.pop("image_format", None)
        managed["reader_metadata"] = "capsule"
        report["publisher_capacity"] = {
            k: managed[k]
            for k in ("heap_bytes", "mapping_bytes", "disk_bytes", "image_limits")
        }
        cases = load_object(REPO / "benchmarks/prepared-pack-access-5m-20261025.json")[
            "cases"
        ]
        if len(cases) != 40:
            raise RuntimeError("Expected exactly forty saved keys")
        public = args.output / "agent.json"
        write_json(
            public,
            {
                "managed_path": "managed.json",
                "generation_dir": "generations",
                "runtime_dir": "runtime",
            },
            0o644,
        )
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as unavailable:
            unavailable.bind(("127.0.0.1", 0))
            managed["address"] = "127.0.0.1"
            managed["port"] = cast(tuple[str, int], unavailable.getsockname())[1]
            write_json(args.output / "managed.json", managed)
            with publisher(
                public, args.output / "publisher-upgrade.log", env
            ) as seconds:
                report["upgrade_ready_seconds"] = seconds
                selected = digest(directory / "selected")
                chosen = (directory / "selected").read_bytes()[16:32].hex()
                if (
                    directory / ("generation-" + chosen + ".read")
                ).stat().st_size != 624:
                    raise RuntimeError("real publisher did not emit capsule")
                if (
                    selected == old_selection
                    or sorted(p.name for p in directory.glob("*.pack")) != packs
                ):
                    raise RuntimeError(
                        "old-head preparation failed to reuse existing packs"
                    )
                measure(args, directory, public, cases, "live", env, samples)
            measure(args, directory, public, cases, "stopped", env, samples)
            with publisher(
                public, args.output / "publisher-restart.log", env
            ) as seconds:
                report["prepared_restart_ready_seconds"] = seconds
                if digest(directory / "selected") != selected:
                    raise RuntimeError("already-prepared restart republished the head")
                first = cases[0]
                fresh_read(directory, first["key"], first["expected_int"], env)
        report["publisher_processes"] = {
            name: load_object(args.output / name)
            for name in ("publisher-upgrade.child.json", "publisher-restart.child.json")
        }
        report["latency_gate"] = {
            state + "/" + resource: {
                "samples": len(group),
                "below_1ms": sum(row["first_live_ns"] < 1000000 for row in group),
            }
            for state in ("live", "stopped")
            for resource in PROBES
            for group in [
                [
                    row
                    for row in samples
                    if row["arm"] == state and row["resource"] == resource
                ]
            ]
        }
        report.update(
            status="passed", samples=samples, packs=len(packs), selected_sha256=selected
        )
    except KeyboardInterrupt:
        report.update(
            status="interrupted", error="controller interrupted qualification"
        )
        raise
    except Exception as error:
        report.update(status="failed", error=str(error))
        raise
    finally:
        report["samples"] = samples
        write_json(args.output / "result.json", report)


def measure(
    args: Arguments,
    directory: Path,
    public: Path,
    cases: list[dict[str, Any]],
    arm: str,
    env: dict[str, str],
    samples: list[dict[str, Any]],
) -> None:
    for round_number in range(args.rounds):
        for index, case in enumerate(cases):
            key, expected = case["key"], case["expected_int"]
            order = (
                ("private", "canonical")
                if (index + round_number) % 2 == 0
                else ("canonical", "private")
            )
            for name in order:
                if arm == "live" and (
                    ACTIVE_PUBLISHER is None or ACTIVE_PUBLISHER.poll() is not None
                ):
                    raise RuntimeError("publisher exited during live profile")
                command = [
                    str(PROBES[name]),
                    "--read",
                    str(directory),
                    key,
                    str(expected),
                ]
                subprocess.run(
                    command, env=env, capture_output=True, timeout=30, check=True
                )
                result = subprocess.run(
                    ["/usr/bin/time", "-l", *command, "100000"],
                    env=env,
                    capture_output=True,
                    text=True,
                    timeout=30,
                    check=True,
                )
                sample: dict[str, Any] = json.loads(result.stdout)
                rss = re.search(
                    r"^\s*(\d+)\s+maximum resident set size\s*$",
                    result.stderr,
                    re.MULTILINE,
                )
                if sample.get("records") != 5000000 or not rss:
                    raise RuntimeError("invalid cardinality orRSS")
                sample.update(
                    arm=arm,
                    resource=name,
                    round=round_number + 1,
                    case=index,
                    maximum_rss_bytes=int(rss[1]),
                )
                samples.append(sample)
                with (args.output / "samples.jsonl").open("a") as stream:
                    stream.write(json.dumps(sample) + "\n")
            if round_number == 0:
                actual = output([str(READER), "get", key, "--config", str(public)], env)
                if actual.strip() != str(expected):
                    raise RuntimeError("ordinaryCLI value differs")


def supervise(arguments: list[str]) -> int:
    expected = int(arguments[0])
    status = Path(arguments[1])
    if os.getppid() != expected:
        return 125
    stopping = False

    def stop(signum: int, frame: FrameType | None) -> None:
        nonlocal stopping
        stopping = True

    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    child = subprocess.Popen(
        arguments[2:], stdin=subprocess.DEVNULL, start_new_session=True
    )
    start = time.time()
    expired = False
    try:
        while child.poll() is None and not stopping:
            if os.getppid() != expected or time.time() - start > 300:
                expired = True
                break
            time.sleep(0.1)
    finally:
        if child.poll() is None:
            os.killpg(child.pid, signal.SIGTERM)
            try:
                child.wait(timeout=60)
            except subprocess.TimeoutExpired:
                os.killpg(child.pid, signal.SIGKILL)
                child.wait()
                expired = True
        usage = resource.getrusage(resource.RUSAGE_CHILDREN)
        write_json(
            status,
            {
                "pid": child.pid,
                "returncode": child.returncode,
                "forced": expired,
                "maximum_rss_bytes": usage.ru_maxrss
                if sys.platform == "darwin"
                else int(usage.ru_maxrss * 1024),
            },
        )
    return 124 if expired else int(child.returncode or 0)


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--supervise":
        raise SystemExit(supervise(sys.argv[2:]))
    main()
