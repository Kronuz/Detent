#!/usr/bin/env python3
"""Replay one fixed set of raw HTTP requests against a detentd and record every response byte.

  http_replay.py --detentd BINARY --out DIR [--port N]

Starts the daemon on a fresh state directory, sends each request on its own connection (and a few
pipelined or split ones), and writes the response bytes to DIR/responses.txt with the Date header
removed, the persisted snapshot.json to DIR/snapshot.json and the daemon's log to DIR/daemon.log.
The requests include the request-smuggling and malformed-message classes http-parser rejects, the
zstd and gzip encodings of a snapshot, and conditional and partial updates. Run it with the old
and the new detentd, then compare the files.
"""

from __future__ import annotations

import argparse
import json
import re
import shutil
import socket
import subprocess
import sys
import time
from pathlib import Path


def snapshot(version: int, count: int) -> bytes:
    knobs: dict[str, object] = {}
    for i in range(count):
        if i % 3 == 0:
            knobs[f"ns{i % 5}:flag_{i}"] = {"type": "bool", "default": i % 2 == 0, "rollout_pct": (i * 7) % 101, "overrides": {"internal": True}}
        elif i % 3 == 1:
            knobs[f"ns{i % 5}:int_{i}"] = {"type": "int", "default": i * 1000 - 5}
        else:
            knobs[f"ns{i % 5}:plain_{i}"] = {"type": "bool", "default": False}
    return json.dumps({"schema_version": 1, "version": version, "knobs": knobs}, separators=(",", ":")).encode()


def request(method: str, path: str, body: bytes = b"", headers: dict[str, str] | None = None) -> bytes:
    lines = [f"{method} {path} HTTP/1.1", "Host: localhost"]
    for key, value in (headers or {}).items():
        lines.append(f"{key}: {value}")
    if body or method in ("PUT", "PATCH", "POST"):
        lines.append(f"Content-Length: {len(body)}")
    return ("\r\n".join(lines) + "\r\n\r\n").encode() + body


def exchange(port: int, payload: list[bytes], settle: float = 0.15, limit: float = 3.0) -> bytes:
    """Sends each chunk of payload (with a short pause between them) and reads until the peer closes or goes quiet."""
    with socket.create_connection(("127.0.0.1", port), timeout=1.0) as s:
        for chunk in payload:
            try:
                s.sendall(chunk)
            except OSError:
                break
            time.sleep(settle)
        received = b""
        deadline = time.monotonic() + limit
        s.settimeout(0.5)
        quiet = 0
        while time.monotonic() < deadline:
            try:
                data = s.recv(1 << 20)
            except TimeoutError:
                quiet += 1
                if quiet >= 2:
                    break
                continue
            except OSError as error:
                received += f"<{type(error).__name__}>".encode()
                break
            if not data:
                received += b"<closed>"
                break
            received += data
        return received


DATE = re.compile(rb"^Date: .*\r\n", re.MULTILINE)


def normalize(response: bytes) -> bytes:
    return DATE.sub(b"", response)


def cases(first: bytes, second: bytes) -> list[tuple[str, list[bytes]]]:
    post = "POST /v1/snapshot HTTP/1.1\r\nHost: localhost\r\n"
    out: list[tuple[str, list[bytes]]] = [
        ("health", [request("GET", "/v1/health")]),
        ("get-empty", [request("GET", "/v1/snapshot")]),
        ("put-bad-json", [request("PUT", "/v1/snapshot", b"{")]),
        ("put-bad-version", [request("PUT", "/v1/snapshot", b'{"schema_version":1,"version":0,"knobs":{}}')]),
        ("put-first", [request("PUT", "/v1/snapshot", first)]),
        ("put-stale", [request("PUT", "/v1/snapshot", first)]),
        ("get-identity", [request("GET", "/v1/snapshot")]),
        ("get-zstd", [request("GET", "/v1/snapshot", headers={"Accept-Encoding": "zstd"})]),
        ("get-gzip", [request("GET", "/v1/snapshot", headers={"Accept-Encoding": "gzip"})]),
        ("get-zstd-gzip", [request("GET", "/v1/snapshot", headers={"Accept-Encoding": "zstd, gzip"})]),
        ("get-etag", [request("GET", "/v1/snapshot", headers={"If-None-Match": '"1"'})]),
        ("get-since", [request("GET", "/v1/snapshot?since=1", headers={"Accept-Encoding": "zstd"})]),
        ("put-second", [request("PUT", "/v1/snapshot", second)]),
        ("get-since-diff", [request("GET", "/v1/snapshot?since=1", headers={"Accept-Encoding": "zstd"})]),
        ("get-since-diff-gzip", [request("GET", "/v1/snapshot?since=1", headers={"Accept-Encoding": "gzip"})]),
        ("get-since-diff-identity", [request("GET", "/v1/snapshot?since=1")]),
        ("patch-knob", [request("PATCH", "/v1/snapshot", b'{"schema_version":1,"version":3,"knobs":{"ns0:new_flag":{"type":"bool","default":true}}}')]),
        ("patch-bad", [request("PATCH", "/v1/snapshot", b'{"schema_version":1,"version":2,"knobs":{}}')]),
        ("get-after-patch", [request("GET", "/v1/snapshot", headers={"Accept-Encoding": "zstd"})]),
        ("unknown-path", [request("GET", "/nope")]),
        ("wrong-method", [request("DELETE", "/v1/snapshot")]),
        ("head", [request("HEAD", "/v1/snapshot")]),
        ("options", [request("OPTIONS", "/v1/snapshot")]),
        ("pipelined", [request("GET", "/v1/health") + request("GET", "/v1/health") + request("GET", "/nope")]),
        ("pipelined-split", [request("GET", "/v1/health")[:10], request("GET", "/v1/health")[10:] + request("GET", "/v1/health")]),
        ("connection-close", [request("GET", "/v1/health", headers={"Connection": "close"})]),
        ("http10", [b"GET /v1/health HTTP/1.0\r\n\r\n"]),
        ("http10-keepalive", [b"GET /v1/health HTTP/1.0\r\nConnection: keep-alive\r\n\r\n"]),
        ("expect-continue", [request("PUT", "/v1/snapshot", first, {"Expect": "100-continue"})]),
        ("chunked-put", [(post.replace("POST", "PUT") + "Transfer-Encoding: chunked\r\n\r\n").encode() + hex(len(second))[2:].encode() + b"\r\n" + second + b"\r\n0\r\n\r\n"]),
        # The smuggling and malformed classes.
        ("bare-lf-request-line", [b"GET /v1/health HTTP/1.1\nHost: a\r\n\r\n"]),
        ("bare-lf-header", [b"GET /v1/health HTTP/1.1\r\nHost: a\nX: y\r\n\r\n"]),
        ("bare-lf-all", [b"GET /v1/health HTTP/1.1\nHost: a\n\n"]),
        ("obs-fold", [b"GET /v1/health HTTP/1.1\r\nHost: a\r\nX: y\r\n  z\r\n\r\n"]),
        ("ctl-in-value", [b"GET /v1/health HTTP/1.1\r\nHost: a\r\nX: y\x01z\r\n\r\n"]),
        ("te-gzip", [(post.replace("POST", "PUT") + "Transfer-Encoding: gzip\r\n\r\n0\r\n\r\n").encode()]),
        ("te-chunked-twice", [(post.replace("POST", "PUT") + "Transfer-Encoding: chunked\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n").encode()]),
        ("te-and-cl", [(post.replace("POST", "PUT") + "Transfer-Encoding: chunked\r\nContent-Length: 5\r\n\r\n0\r\n\r\n").encode()]),
        ("te-cl-smuggle", [(post.replace("POST", "PUT") + "Content-Length: 4\r\nTransfer-Encoding: chunked\r\n\r\n5c\r\nGET /v1/health HTTP/1.1\r\nHost: a\r\n\r\nx=1\r\n0\r\n\r\n").encode()]),
        ("cl-plus", [(post.replace("POST", "PUT") + "Content-Length: +5\r\n\r\nhello").encode()]),
        ("cl-space", [(post.replace("POST", "PUT") + "Content-Length: 5 \r\n\r\nhello").encode()]),
        ("cl-hex", [(post.replace("POST", "PUT") + "Content-Length: 0x5\r\n\r\nhello").encode()]),
        ("cl-list", [(post.replace("POST", "PUT") + "Content-Length: 5, 5\r\n\r\nhello").encode()]),
        ("cl-twice-different", [(post.replace("POST", "PUT") + "Content-Length: 5\r\nContent-Length: 6\r\n\r\nhello!").encode()]),
        ("cl-overflow", [(post.replace("POST", "PUT") + "Content-Length: 99999999999999999999999\r\n\r\nhello").encode()]),
        ("cl-trailer", [(post.replace("POST", "PUT") + "Transfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n0\r\nContent-Length: 9\r\n\r\n").encode()]),
        ("chunk-bad-size", [(post.replace("POST", "PUT") + "Transfer-Encoding: chunked\r\n\r\nzz\r\nhello\r\n0\r\n\r\n").encode()]),
        ("chunk-bare-lf", [(post.replace("POST", "PUT") + "Transfer-Encoding: chunked\r\n\r\n5\nhello\n0\n\n").encode()]),
        ("chunk-plus-size", [(post.replace("POST", "PUT") + "Transfer-Encoding: chunked\r\n\r\n+5\r\nhello\r\n0\r\n\r\n").encode()]),
        ("version-1.2", [b"GET /v1/health HTTP/1.2\r\nHost: a\r\n\r\n"]),
        ("version-0.9", [b"GET /v1/health HTTP/0.9\r\nHost: a\r\n\r\n"]),
        ("version-lower", [b"GET /v1/health http/1.1\r\nHost: a\r\n\r\n"]),
        ("no-version", [b"GET /v1/health\r\n\r\n"]),
        ("unknown-method", [b"FOO /v1/health HTTP/1.1\r\nHost: a\r\n\r\n"]),
        ("space-before-colon", [b"GET /v1/health HTTP/1.1\r\nHost : a\r\n\r\n"]),
        ("no-colon", [b"GET /v1/health HTTP/1.1\r\nHost a\r\n\r\n"]),
        ("long-url", [b"GET /" + b"a" * 70000 + b" HTTP/1.1\r\nHost: a\r\n\r\n"]),
        ("long-header", [b"GET /v1/health HTTP/1.1\r\nHost: a\r\nX: " + b"a" * 70000 + b"\r\n\r\n"]),
        ("garbage", [b"\x16\x03\x01\x02\x00\x01\x00\x01\xfc\x03\x03"]),
        ("upgrade", [b"GET /v1/health HTTP/1.1\r\nHost: a\r\nConnection: Upgrade\r\nUpgrade: websocket\r\n\r\n"]),
        ("connect", [b"CONNECT a:443 HTTP/1.1\r\nHost: a:443\r\n\r\n"]),
        ("big-put", [request("PUT", "/v1/snapshot", snapshot(50, 20000))]),
        ("get-big-zstd", [request("GET", "/v1/snapshot", headers={"Accept-Encoding": "zstd"})]),
        ("get-big-gzip", [request("GET", "/v1/snapshot", headers={"Accept-Encoding": "gzip"})]),
        ("get-big-identity", [request("GET", "/v1/snapshot")]),
    ]
    return out


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--detentd", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--port", type=int, default=18317)
    args = parser.parse_args()
    args.detentd = args.detentd.resolve()
    args.out = args.out.resolve()
    if args.out.exists():
        shutil.rmtree(args.out)
    args.out.mkdir(parents=True)
    state = args.out / "state"
    state.mkdir()
    persist = state / "snapshot.json"
    log = (args.out / "daemon.log").open("wb")
    daemon = subprocess.Popen(
        [str(args.detentd), "--port", str(args.port), "--persist-path", str(persist), "--reactors", "2", "--color", "never", "-vv"],
        stdout=log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, cwd=state,
    )
    try:
        for _ in range(100):
            try:
                socket.create_connection(("127.0.0.1", args.port), timeout=0.2).close()
                break
            except OSError:
                time.sleep(0.1)
        else:
            print("detentd did not start", file=sys.stderr)
            return 2
        first, second = snapshot(1, 40), snapshot(2, 45)
        with (args.out / "responses.txt").open("wb") as out:
            for name, payload in cases(first, second):
                response = normalize(exchange(args.port, payload))
                out.write(f"=== {name} ({len(response)} bytes)\n".encode())
                out.write(response if len(response) < 3000 else response[:1500] + b"\n...[" + str(len(response)).encode() + b" bytes]...\n" + response[-1500:])
                out.write(b"\n")
        time.sleep(0.3)
        if persist.exists():
            shutil.copy(persist, args.out / "snapshot.json")
    finally:
        daemon.terminate()
        try:
            daemon.wait(timeout=10)
        except subprocess.TimeoutExpired:
            daemon.kill()
        log.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
