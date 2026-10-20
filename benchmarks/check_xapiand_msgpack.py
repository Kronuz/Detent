from __future__ import annotations

import argparse
import json
import socket
import subprocess
import time
import urllib.error
import urllib.request
from pathlib import Path


def request(
    base: str,
    method: str,
    path: str,
    body: bytes | None = None,
    content_type: str = "application/json",
) -> tuple[int, bytes]:
    req = urllib.request.Request(
        base + path, data=body, method=method, headers={"Content-Type": content_type}
    )
    try:
        with urllib.request.urlopen(req, timeout=10) as response:
            return response.status, response.read()
    except urllib.error.HTTPError as error:
        return error.code, error.read()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    hold = subprocess.run(["quiet", "check"], check=False)
    if hold.returncode not in (0, 2):
        raise SystemExit("Strict host hold: deferred")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    with socket.socket() as reservation:
        reservation.bind(("127.0.0.1", 0))
        port = reservation.getsockname()[1]
    base = f"http://127.0.0.1:{port}"
    with (output / "node.log").open("wb") as log:
        process = subprocess.Popen(
            [
                "nice",
                "-n",
                "19",
                str(args.binary.resolve()),
                "--port",
                str(port),
                "--name",
                "msgpack-compat",
                "--solo",
                "-D",
                str(output / "data"),
            ],
            stdout=log,
            stderr=subprocess.STDOUT,
        )
        try:
            deadline = time.monotonic() + 30
            while True:
                if process.poll() is not None:
                    raise RuntimeError("Xapiand exited before readiness")
                try:
                    if request(base, "GET", "/")[0] == 200:
                        break
                except OSError:
                    pass
                if time.monotonic() >= deadline:
                    raise RuntimeError("Readiness deadline expired")
                time.sleep(0.05)
            results: dict[str, int] = {}
            status, _ = request(
                base, "PUT", "/smoke/1?commit=true", b'{"body":"the quick brown fox"}'
            )
            assert 200 <= status < 300, status
            results["json_put"] = status
            status, _ = request(
                base,
                "PUT",
                "/smoke/2?commit=true",
                bytes.fromhex("81a4626f6479b374686520717569636b2062726f776e20666f78"),
                "application/msgpack",
            )
            assert 200 <= status < 300, status
            results["msgpack_put"] = status
            status, body = request(
                base, "POST", "/smoke/:search", b'{"_query":{"body":"brown"}}'
            )
            assert status == 200, (status, body)
            assert json.loads(body)["count"] == 2, body
            results["search_count"] = 2
            for name, data, kind in [
                ("invalid_msgpack", b"\xc1", "application/msgpack"),
                ("invalid_json", b'{"body":', "application/json"),
            ]:
                status, _ = request(base, "PUT", "/smoke/3?commit=true", data, kind)
                assert 400 <= status < 600, (name, status)
                results[name] = status
                status, _ = request(base, "GET", "/smoke/1")
                assert status == 200, status
            (output / "result.json").write_text(json.dumps(results, indent=2) + "\n")
            print(json.dumps(results))
        finally:
            process.terminate()
            try:
                process.wait(timeout=20)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()


if __name__ == "__main__":
    main()
