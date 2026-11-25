"""The seal-answer window at the authority (relay-mirror-idle-wake reviews R4-1 and R5-2).

A single voter and a relay mirroring it (bench/idle-wakeups' Pair) with the relay's `timeout_ms` T, so
its watch parks T/2. With DELAY_MS, the relay reads the authority through a TCP proxy that holds the
first bytes of each connection DELAY_MS (network latency before the request arrives). Each trial
writes once to "a" (the relay syncs it at once), waits for the authority's layouts seal to move (D136
seals the write about seal_after later, at a phase of the relay's park nothing here chooses), and
times the relay's layouts seal catching up. The authority answers the parked next 204 when the seal
is applied, with the seal (D165), so the relay syncs however late that 204 reaches it
(docs/mechanisms/relay-mirror-wait.md). A trial misses when the relay is still behind after LIMIT_S.
Exit 1 on any miss.

With AUTH_WAIT_MS, the authority's `subscription_limits.wait_ms` (0 is allowed): a session that
grants no park answers every next at once, so only the answer's seal tells a seal from idle (R5-2).

With MODE "other", the relay mirrors only "a" and each trial writes "b": the seal still moves the
layout seal of "a", which the relay must adopt, and it lands at any phase of the relay's parks.

    seal_window.py SRC BUILD [TIMEOUT_MS] [TRIALS] [DELAY_MS] [MODE] [AUTH_WAIT_MS]
"""

from __future__ import annotations

import asyncio
import json
import ssl
import sys
import threading
import time
from pathlib import Path

LIMIT_S = 1.5  # plus 3 x DELAY_MS: the sync after the 204 makes at least two requests through the proxy


class DelayProxy:
    """127.0.0.1:port -> 127.0.0.1:target, the first client bytes of each connection held `delay` s."""

    def __init__(self, target: int, delay: float) -> None:
        self.target, self.delay = target, delay
        self.loop = asyncio.new_event_loop()
        ready = threading.Event()
        self.port = 0

        async def serve() -> None:
            server = await asyncio.start_server(self.handle, "127.0.0.1", 0)
            self.port = server.sockets[0].getsockname()[1]
            ready.set()
            async with server:
                await server.serve_forever()

        self.thread = threading.Thread(target=lambda: self.loop.run_until_complete(serve()), daemon=True)
        self.thread.start()
        ready.wait()

    async def handle(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
        try:
            up_reader, up_writer = await asyncio.open_connection("127.0.0.1", self.target)
        except OSError:
            writer.close()
            return

        async def up() -> None:
            first = True
            try:
                while data := await reader.read(65536):
                    if first:
                        await asyncio.sleep(self.delay)
                        first = False
                    up_writer.write(data)
                    await up_writer.drain()
            except OSError:
                pass
            finally:
                up_writer.close()

        async def down() -> None:
            try:
                while data := await up_reader.read(65536):
                    writer.write(data)
                    await writer.drain()
            except OSError:
                pass
            finally:
                writer.close()

        await asyncio.gather(up(), down())


def main() -> int:
    source, build = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
    timeout_ms = int(sys.argv[3]) if len(sys.argv) > 3 else 200
    trials = int(sys.argv[4]) if len(sys.argv) > 4 else 40
    delay_ms = int(sys.argv[5]) if len(sys.argv) > 5 else 0
    other = len(sys.argv) > 6 and sys.argv[6] == "other"
    auth_wait_ms = int(sys.argv[7]) if len(sys.argv) > 7 else None
    written = "b" if other else "a"
    limit = LIMIT_S + 3 * delay_ms / 1000
    sys.path.insert(0, str(source / "bench/idle-wakeups"))
    from pair import MIRROR, NAMES, READER, Pair  # noqa: PLC0415

    names = ["a"] if other else NAMES

    class Timed(Pair):
        def launch(self, name: str, command: list[str]) -> None:
            if name == "authority" and auth_wait_ms is not None:
                path = self.root / "authority.json"
                configuration = json.loads(path.read_text())
                configuration["subscription_limits"] = {"wait_ms": auth_wait_ms}
                path.write_text(json.dumps(configuration))
                path.chmod(0o600)
            if name == "relay":
                path = self.root / "relay.json"
                configuration = json.loads(path.read_text())
                configuration["mirror"]["timeout_ms"] = timeout_ms
                if other:
                    configuration["mirror"]["namespaces"] = ["a"]
                if delay_ms:
                    self.proxy = DelayProxy(self.authority_port, delay_ms / 1000)
                    configuration["mirror"]["upstreams"] = [{"address": "127.0.0.1", "port": self.proxy.port}]
                path.write_text(json.dumps(configuration))
                path.chmod(0o600)
            super().launch(name, command)

    misses: list[tuple[int, float | None]] = []
    with Timed(build, interval_ms=100) as pair:

        def seal(port: int, token: str) -> int | None:
            try:
                status, body = pair.call(port, "/v2/history/layouts", {"namespaces": names}, token)
            except (OSError, ssl.SSLError):
                return None
            return int.from_bytes(body[24:32], "little") if status == 200 else None

        def mirror() -> dict[str, object]:
            return dict((pair.health(pair.relay_port) or {}).get("mirror", {}))

        time.sleep(2)
        rest = mirror()
        time.sleep(2)
        print(f"at rest: relay syncs {int(mirror().get('syncs', 0)) - int(rest.get('syncs', 0))} in 2 s "
              f"(timeout_ms {timeout_ms}, authority wait_ms {auth_wait_ms}, proxy delay {delay_ms} ms, "
              f"writes to {written!r}, mirrors {names})", flush=True)
        for trial in range(trials):
            while (before := seal(pair.authority_port, MIRROR)) is None:
                time.sleep(0.01)
            start = mirror()
            pair.sequence += 1
            pair.write("/v2/transactions", {
                "generation": 1, "sequence": pair.sequence, "namespace": written,
                "operations": [{"key": f"{written}:k0", "value": {"default": pair.sequence}}],
            })
            deadline = time.monotonic() + 10
            while (target := seal(pair.authority_port, MIRROR)) in (before, None):
                if time.monotonic() > deadline:
                    print(f"trial {trial}: the authority never sealed the write", flush=True)
                    return 3
                time.sleep(0.005)
            sealed = time.monotonic()
            reached = None
            while time.monotonic() - sealed < limit:
                if seal(pair.relay_port, READER) == target:
                    reached = time.monotonic() - sealed
                    break
                time.sleep(0.01)
            stats = mirror()
            counts = (f"syncs +{int(stats.get('syncs', 0)) - int(start.get('syncs', 0))}, failed syncs "
                      f"+{int(stats.get('sync_failures', 0)) - int(start.get('sync_failures', 0))}")
            if reached is None:
                late = None
                if not misses:  # the first miss: when the relay serves it with nothing else written
                    while time.monotonic() - sealed < 40:
                        if seal(pair.relay_port, READER) == target:
                            late = time.monotonic() - sealed
                            break
                        time.sleep(0.05)
                misses.append((trial, late))
                print(f"trial {trial}: seal {before} -> {target}: MISS, the relay still served the old seal after "
                      f"{limit} s ({counts})"
                      + (f"; it served it {late:.2f} s after the seal" if late is not None else ""), flush=True)
            else:
                print(f"trial {trial}: seal {before} -> {target}: relay in {reached * 1000:.0f} ms ({counts})",
                      flush=True)
    print(f"RESULT: {len(misses)} of {trials} seals missed for {limit} s at timeout_ms {timeout_ms}, "
          f"proxy delay {delay_ms} ms: {misses}", flush=True)
    return 1 if misses else 0


if __name__ == "__main__":
    sys.exit(main())
