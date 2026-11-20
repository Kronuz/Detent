"""Dense distribution DD3 end to end: a cold agent boots from a real relay over pages.

Three authority voters hold RECORDS keys in one namespace. One agent reads the authority, takes
records and builds its generation; another reads a relay. The relayed agent boots from the relay's
page transfer and builds nothing (its log reports zero bytes hashed to build), and its root, the
root page's content entry at the same position, equals the records agent's. A clustered rewrite
made while the relay is paused (SIGSTOP) then reaches the warm relayed agent as one delta, which
the relay sends as pages. With --expect-refusal, under tests/detent_system/forcing/
relay-root-forced.patch (the relay's full copies name a wrong root), the agent refuses the
transfer and boots from records, to the same root, and its session takes the delta as records.
"""

from __future__ import annotations

import argparse
import asyncio
import http.client
import json
import os
import re
import signal
import socket
import ssl
import sys
import time
from pathlib import Path
from typing import Any

from authority import Authority
from managed import ManagedCohort
from runtime import Actors, Evidence
from writers import Writers

NAMESPACE = "load0"
BATCH = 4096
HASHED = re.compile(rb"hashed \d+ bytes since start: (\d+) to build")
TRANSFERS = re.compile(rb"page transfers (\d+) received \((\d+) bytes\), (\d+) refused")


class Output:
    """Each agent's output, by cohort, and the last counters it logged."""

    def __init__(self) -> None:
        self.text: dict[str, bytearray] = {}

    def observer(self, label: str):
        def observe(index: int, payload: bytes) -> None:
            self.text.setdefault(f"{label}{index}", bytearray()).extend(payload)

        return observe

    def built(self, agent: str) -> int | None:
        found = HASHED.findall(self.text.get(agent, b""))
        return int(found[-1]) if found else None

    def transfers(self, agent: str) -> tuple[int, int, int]:
        found = TRANSFERS.findall(self.text.get(agent, b""))
        return tuple(int(value) for value in found[-1]) if found else (0, 0, 0)  # type: ignore[return-value]


def root(generations: Path, name: str) -> tuple[int, bytes]:
    """The selected generation's position and root-page content entry (DTROOT04 [56,64), [160,208))."""
    table = (generations / "selected").read_bytes()
    at = 64
    for _ in range(int.from_bytes(table[12:16], "big")):
        length = table[at]
        if table[at + 1 : at + 1 + length].decode() == name:
            generation = table[at + 1 + length + 44 : at + 1 + length + 60].hex()
            image = (generations / f"generation-{generation}.image").read_bytes()
            if image[:8] != b"DTROOT04":
                raise RuntimeError("selected root is not DTROOT04")
            return int.from_bytes(image[56:64], "big"), image[160:208]
        at += 137 + length
    raise RuntimeError(f"no selected entry for {name}")


def health(context: ssl.SSLContext, address: str, port: int) -> dict[str, Any]:
    # The fixture certificate names localhost: connect to the address, verify that name.
    raw = socket.create_connection((address, port), timeout=10)
    connection = http.client.HTTPSConnection(
        "localhost", port, context=context, timeout=10
    )
    connection.sock = context.wrap_socket(raw, server_hostname="localhost")
    try:
        connection.request("GET", "/v1/health", headers={"Connection": "close"})
        response = connection.getresponse()
        if response.status != 200:
            raise RuntimeError(f"relay health: {response.status}")
        return json.loads(response.read())
    finally:
        connection.close()


def widen(directory: Path) -> None:
    """Each voter takes BATCH operations a transaction (seeding 100K keys 128 at a time takes
    minutes), and keeps history above the cut up to 4 times the copy (alpha 1, cap 4, against 1/8
    and 2), so the clustered rewrite stays a delta rather than moving the cut past the agent's base."""
    for path in sorted(directory.glob("authority-*.json")):
        configuration = json.loads(path.read_text())
        configuration["application_limits"] = {
            "mutations": BATCH,
            "layout": {"alpha_num": 1, "alpha_den": 1, "cap": 4},
        }
        path.write_text(json.dumps(configuration))


async def seed(writers: Writers) -> None:
    opened = await writers.successful(
        0, "/v2/open", {"expected_generation": 0, "nonce": f"{1:032x}"}
    )
    if opened.get("generation") != 1:
        raise RuntimeError(f"unexpected producer generation: {opened}")
    await rewrite(writers, list(range(writers.records)), 0)


async def rewrite(writers: Writers, ordinals: list[int], step: int = 1) -> None:
    """Each key in `ordinals` set to its value plus `step`, BATCH keys a transaction."""
    for offset in range(0, len(ordinals), BATCH):
        batch = ordinals[offset : offset + BATCH]
        operations = [
            {
                "key": f"{NAMESPACE}:key{ordinal}",
                "value": {"default": writers.values[ordinal] + step},
            }
            for ordinal in batch
        ]
        if not await writers.transaction(0, operations):
            raise RuntimeError("authority capacity refused the write")
        for ordinal in batch:
            writers.values[ordinal] += step


def check(condition: bool, what: str) -> None:
    if not condition:
        raise RuntimeError(what)


async def run(arguments: argparse.Namespace) -> None:
    output = arguments.output.resolve()
    output.mkdir(parents=True, mode=0o700, exist_ok=False)
    evidence = Evidence(output / "events.jsonl", 64 * 1024 * 1024)
    certificates = arguments.certificates.resolve()
    context = ssl.create_default_context(cafile=str(certificates / "ca.pem"))
    logs = Output()
    async with Actors(evidence, 30) as actors:
        authority = Authority(
            arguments.binary.resolve(), certificates, output, actors, 1
        )
        widen(output)
        await authority.start()
        writers = Writers(authority, arguments.records, 1, 1)
        began = time.monotonic()
        await seed(writers)
        evidence.record(
            "seeded", records=arguments.records, seconds=time.monotonic() - began
        )
        cohorts = {}
        for label, relay in (
            ("direct-", None),
            ("relayed-", arguments.relays.resolve()),
        ):
            directory = output / label.rstrip("-")
            directory.mkdir(mode=0o700)
            cohorts[label] = ManagedCohort(
                authority,
                actors,
                directory,
                arguments.agents.resolve(),
                arguments.local_oracle.resolve(),
                certificates,
                1,
                1,
                relay=relay,
                relay_count=1,
                source_timeout_ms=60000,
                selection_timeout=120,
                label=label,
                observe=logs.observer(label),
            )
        direct, relayed = cohorts["direct-"], cohorts["relayed-"]
        await direct.start()
        await direct.verify(writers)
        began = time.monotonic()
        await relayed.start()
        await relayed.verify(writers)
        booted = time.monotonic() - began
        port = relayed.relay_ports[0]
        relay_pages = health(context, authority.address, port)["mirror"]["pages"]
        position, relayed_root = root(
            output / "relayed" / "agent-0" / "generations", NAMESPACE
        )
        direct_position, direct_root = root(
            output / "direct" / "agent-0" / "generations", NAMESPACE
        )
        cold = logs.transfers("relayed-0")
        evidence.record(
            "cold_boot",
            seconds=booted,
            relay=relay_pages,
            transfers=cold,
            built=logs.built("relayed-0"),
            direct_built=logs.built("direct-0"),
        )
        print(
            f"cold boot: {arguments.records} keys in {booted:.2f} s; relay {relay_pages}; "
            f"agent transfers {cold}, built {logs.built('relayed-0')} bytes; "
            f"records agent built {logs.built('direct-0')} bytes",
            flush=True,
        )
        check(
            position == direct_position and relayed_root == direct_root,
            "the roots differ",
        )
        check(direct_root != bytes(48), "the root is empty")
        check((logs.built("direct-0") or 0) > 0, "the records agent built nothing")
        if arguments.expect_refusal:
            check(
                cold[2] >= 1 and cold[0] == 0,
                f"the forced root was not refused: {cold}",
            )
            check(
                (logs.built("relayed-0") or 0) > 0,
                "the refused agent did not build from records",
            )
        else:
            check(
                cold[0] == 1 and cold[2] == 0,
                f"the cold boot was not one page transfer: {cold}",
            )
            check(logs.built("relayed-0") == 0, "the relayed agent built")
            check(
                relay_pages["full_copies"] >= 1 and relay_pages["builds"] >= 1,
                "the relay sent no pages",
            )

        # A clustered rewrite, every key whose decimal ordinal starts with 1 to 5, while the relay is
        # paused, so the warm agent's next from its base takes the whole change at once.
        ordinals = [i for i in range(arguments.records) if str(i)[0] in "12345"]
        relay_process = relayed.relays[0].process
        os.kill(relay_process.pid, signal.SIGSTOP)
        try:
            began = time.monotonic()
            await rewrite(writers, ordinals)
            await direct.wait_selected(direct.positions(writers))
            written = time.monotonic() - began
        finally:
            os.kill(relay_process.pid, signal.SIGCONT)
        await relayed.verify(writers)
        after = health(context, authority.address, port)["mirror"]["pages"]
        moved = logs.transfers("relayed-0")
        position, relayed_root = root(
            output / "relayed" / "agent-0" / "generations", NAMESPACE
        )
        direct_position, direct_root = root(
            output / "direct" / "agent-0" / "generations", NAMESPACE
        )
        evidence.record(
            "clustered_delta",
            keys=len(ordinals),
            seconds=written,
            relay=after,
            transfers=moved,
        )
        print(
            f"clustered delta: {len(ordinals)} keys; relay {after}; agent transfers {moved}",
            flush=True,
        )
        check(
            position == direct_position and relayed_root == direct_root,
            "the roots differ after the delta",
        )
        if arguments.expect_refusal:
            # The session that refused keeps the namespace in "records" (D9) until it reopens.
            check(
                after["deltas"] == relay_pages["deltas"], "a records session got pages"
            )
            check(moved == cold, f"the agent took pages after its refusal: {moved}")
        else:
            check(
                after["deltas"] == relay_pages["deltas"] + 1,
                "the relay did not send the delta as pages",
            )
            check(
                moved[0] == cold[0] + 1,
                f"the agent did not take the delta as pages: {moved}",
            )
        await relayed.close()
        await direct.close()
    print("relay pages: ok", flush=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True, help="detent-authority")
    parser.add_argument("--agents", type=Path, required=True, help="detent-agent")
    parser.add_argument("--relays", type=Path, required=True, help="detent-relay")
    parser.add_argument("--local-oracle", type=Path, required=True)
    parser.add_argument("--certificates", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--records", type=int, default=100000)
    parser.add_argument("--expect-refusal", action="store_true")
    arguments = parser.parse_args()
    if not 1000 <= arguments.records <= 500000:
        raise SystemExit("records must be 1,000 to 500,000")
    try:
        asyncio.run(run(arguments))
    except Exception as error:
        print(f"run_relay_pages: {error!r}", file=sys.stderr, flush=True)
        raise SystemExit(1) from error


if __name__ == "__main__":
    main()
