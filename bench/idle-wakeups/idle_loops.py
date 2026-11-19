"""Count an idle single-voter detentd's and an idle detent-relay's loop wakeups (loop-idle-waits A1).

One detentd with no peers and one relay mirroring it run with no client; once both report ready and
have settled, the script reads, over --seconds, each process's rusage (interrupt and package-idle
wakeups, CPU) through the usage helper, the authority's owner turns (`owner_turn_ns.count` in
/v1/health) and the relay's turns (`turns`, where its build reports it). It prints one JSON line.

    idle_loops.py --build BUILD --usage USAGE_HELPER --label NAME [--seconds 10]
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import signal
import socket
import ssl
import subprocess
import tempfile
import time
import urllib.request
from pathlib import Path
from typing import Any

CONTEXT = {
    "cluster": "63" + "00" * 15,
    "group": "67" + "00" * 15,
    "epoch": "65" + "00" * 15,
}
WRITER = "a1" * 16
MIRROR = "b2" * 16
READER = "c3" * 16


def free_port() -> int:
    with socket.socket() as reservation:
        reservation.bind(("127.0.0.1", 0))
        return int(reservation.getsockname()[1])


def private(path: Path, value: dict[str, Any]) -> Path:
    path.write_text(json.dumps(value))
    path.chmod(0o600)
    return path


def health(port: int, context: ssl.SSLContext) -> dict[str, Any]:
    with urllib.request.urlopen(
        f"https://localhost:{port}/v1/health", context=context, timeout=5
    ) as response:
        result: dict[str, Any] = json.load(response)
    return result


def usage(helper: Path, pid: int) -> dict[str, int]:
    line = subprocess.check_output([str(helper), str(pid)], text=True).strip()
    return {
        key: int(value) for key, value in (part.split("=", 1) for part in line.split())
    }


def wait_ready(
    name: str, child: subprocess.Popen[bytes], probe: Any, timeout: float = 60
) -> dict[str, Any]:
    deadline = time.monotonic() + timeout
    last: object = None
    while time.monotonic() < deadline:
        if child.poll() is not None:
            raise RuntimeError(f"{name} exited {child.returncode}")
        try:
            last = probe()
            if last:
                return last  # type: ignore[no-any-return]
        except OSError as error:
            last = error
        time.sleep(0.1)
    raise RuntimeError(f"{name} not ready: {last!r}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--usage", type=Path, required=True)
    parser.add_argument("--label", required=True)
    parser.add_argument("--seconds", type=float, default=10)
    args = parser.parse_args()
    source = Path(__file__).resolve().parents[2]
    build = args.build.resolve()
    detentd, relay = build / "src/detentd", build / "src/detent-relay"
    for binary in (detentd, relay, args.usage):
        if not Path(binary).is_file():
            raise RuntimeError(f"missing {binary}")
    scratch = source / ".scratch"
    scratch.mkdir(exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix="idle-loops-", dir=scratch))
    children: dict[str, subprocess.Popen[bytes]] = {}
    try:
        certificates = root / "certificates"
        subprocess.run(
            [
                "cmake",
                f"-DOUTPUT={certificates}",
                f"-DOPENSSL={shutil.which('openssl')}",
                "-P",
                str(source / "tests/detent_authority/tls_certificates.cmake"),
            ],
            check=True,
            stdout=subprocess.DEVNULL,
            env={
                **os.environ,
                "OPENSSL_CONF": str(source / "tests/detent_authority/tls_ambient.cnf"),
            },
        )
        context = ssl.create_default_context(cafile=str(certificates / "ca.pem"))
        tls = {
            "certificate": str(certificates / "one.pem"),
            "key": str(certificates / "one.key"),
            "ca": str(certificates / "ca.pem"),
        }
        authority_port, relay_port = free_port(), free_port()
        authority = private(
            root / "authority.json",
            CONTEXT
            | {
                "directory": str(root / "authority-data"),
                "publisher_tls": tls,
                "publisher_bind": "127.0.0.1",
                "publisher_name": "localhost",
                "credentials": [
                    {"token": WRITER, "producer": "writer", "namespaces": ["a"]},
                    {
                        "token": MIRROR,
                        "producer": "relay",
                        "principal": "relay",
                        "publish": False,
                        "namespaces": ["*"],
                    },
                ],
            },
        )
        configuration = private(
            root / "relay.json",
            {
                "address": "127.0.0.1",
                "port": relay_port,
                "hostname": "localhost",
                "certificate": tls["certificate"],
                "private_key": tls["key"],
                "ca": tls["ca"],
                "mirror": {
                    "directory": str(root / "mirror"),
                    "cluster": CONTEXT["cluster"],
                    "namespaces": ["a"],
                    "upstreams": [{"address": "127.0.0.1", "port": authority_port}],
                    "token": MIRROR,
                },
                "credentials": [
                    {
                        "token": READER,
                        "principal": "reader",
                        "policy_epoch": 1,
                        "namespaces": ["*"],
                    }
                ],
            },
        )
        for name, command in (
            (
                "detentd",
                [
                    str(detentd),
                    "--authority",
                    str(authority),
                    "--port",
                    str(authority_port),
                    "--runtime-dir",
                    str(root / "authority-run"),
                ],
            ),
            (
                "relay",
                [
                    str(relay),
                    "--config",
                    str(configuration),
                    "--runtime-dir",
                    str(root / "relay-run"),
                ],
            ),
        ):
            with (root / f"{name}.log").open("wb") as log:
                children[name] = subprocess.Popen(
                    command, stdout=log, stderr=subprocess.STDOUT
                )
            if name == "detentd":
                wait_ready(
                    name,
                    children[name],
                    lambda: health(authority_port, context).get("ready"),
                )

        def relay_ready() -> bool:
            state = health(relay_port, context)
            return bool(state.get("ready") and state.get("mirror", {}).get("ready"))

        wait_ready("relay", children["relay"], relay_ready)
        time.sleep(2)

        def sample() -> dict[str, Any]:
            return {
                "detentd": usage(args.usage, children["detentd"].pid),
                "relay": usage(args.usage, children["relay"].pid),
                "owner_turns": health(authority_port, context)["owner_turn_ns"][
                    "count"
                ],
                "relay_turns": health(relay_port, context).get("turns"),
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
        print(json.dumps(result, sort_keys=True))
    except Exception:
        for name in children:
            log = root / f"{name}.log"
            if log.exists():
                print(
                    f"--- {name}.log (end)\n" + log.read_text(errors="replace")[-4000:]
                )
        raise
    finally:
        for child in children.values():
            child.send_signal(signal.SIGTERM)
        for child in children.values():
            try:
                child.wait(20)
            except subprocess.TimeoutExpired:
                child.kill()
                child.wait()
        shutil.rmtree(root, ignore_errors=True)


if __name__ == "__main__":
    main()
