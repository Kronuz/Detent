"""A restarted authority member rebinds its port although another process holds it for a while.

usage: test_restart_rebind.py TLS_FIXTURE SCRATCH

A killed member restarts on the port it had. It sets SO_REUSEADDR, so its own sockets never block
it, but the port was released after the harness reserved it and another process can take it
meanwhile: the member then exits with "bind: Address already in use". The test kills each of the
three members once, squats on its HTTP port for a random 0-1 s and requires
Authority.restart_member to return with the member answering. (One kill per member and run: a
port that has served a request stays in TIME_WAIT and refuses the squatter.) The stand-in daemon
binds the way detent-authority does and serves TLS health.
"""

from __future__ import annotations

import argparse
import asyncio
import json
import random
import socket
import ssl
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from authority import Authority
from runtime import Actors, Evidence


class Handler(BaseHTTPRequestHandler):
    def do_GET(self) -> None:
        body = json.dumps({"role": 2}).encode()
        self.send_response(200)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *_: object) -> None:
        pass


class Server(ThreadingHTTPServer):
    allow_reuse_address = True  # the daemon's acceptors set SO_REUSEADDR


def serve(arguments: list[str]) -> int:
    configuration = json.loads(
        Path(arguments[arguments.index("--authority") + 1]).read_text()
    )
    port = int(arguments[arguments.index("--port") + 1])
    try:
        server = Server((configuration["publisher_bind"], port), Handler)
    except OSError as error:
        print(f"detent-authority: bind: {error.strerror}", flush=True)
        return 1
    tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    tls_files = configuration["publisher_tls"]
    tls.load_cert_chain(tls_files["certificate"], tls_files["key"])
    server.socket = tls.wrap_socket(server.socket, server_side=True)
    server.serve_forever()
    return 0


async def rounds(certificates: Path, scratch: Path) -> None:
    scratch.mkdir(parents=True, exist_ok=True)
    binary = scratch / "detent-authority"
    binary.write_text(f'#!/bin/sh\nexec "{sys.executable}" "{__file__}" --serve "$@"\n')
    binary.chmod(0o755)
    evidence = Evidence(
        scratch / f"events-{random.randrange(1 << 32):08x}.jsonl", 1 << 20
    )
    async with Actors(evidence, 10) as actors:
        authority = Authority(binary, certificates, scratch, actors, 1)
        authority.release_ports()
        for index in range(3):
            authority.members.append(await authority.start_member(index))
        for index in range(3):
            await authority.kill_member(index)
            squat = socket.socket()
            squat.bind((authority.address, authority.ports[index + 3]))
            threading.Timer(random.uniform(0, 1), squat.close).start()
            await authority.restart_member(index)
            status, _ = await authority.request(index, "/v1/health")
            assert status == 200, f"member {index} answered {status}"
    print("3 kills with the port squatted: every member rebound")


def main() -> int:
    if "--serve" in sys.argv:
        return serve(sys.argv[2:])
    parser = argparse.ArgumentParser()
    parser.add_argument("certificates", type=Path)
    parser.add_argument("scratch", type=Path)
    arguments = parser.parse_args()
    asyncio.run(rounds(arguments.certificates, arguments.scratch))
    return 0


if __name__ == "__main__":
    sys.exit(main())
