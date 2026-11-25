"""D141 end to end: three voters restarted with a raised `keys` grow past the old ceiling.

Three authority voters run with application_limits.keys = OLD. A writer fills OLD keys in one
namespace, and the next key fails `capacity`. One agent follows the voters, another a relay.
Every voter then stops, and all three restart on the same stores with keys = NEW; the relay and
both agents keep running. The retry of the refused sequence returns its original `capacity`
receipt, the writer then grows the namespace to NEW keys, and each agent's namespace cursor keeps
its cluster and incarnation while the independent SDK oracle reads all NEW values from it.

Run through test_restart_capacity.sh. unittest discovery imports this module: it has no tests.
"""

from __future__ import annotations

import argparse
import asyncio
import json
import sys
import time
from pathlib import Path

from authority import Authority
from managed import POSITION_AT, ManagedCohort, table_entries
from runtime import Actors, Evidence
from writers import Writers

NAMESPACE = "load0"
BATCH = 100  # operations a transaction, within the default 128 mutations


def cursor(generations: Path, name: str) -> tuple[bytes, int]:
    """The cluster and incarnation of `name`'s cursor in the selected table, and its position."""
    entries = table_entries((generations / "selected").read_bytes())
    if entries is None:
        raise RuntimeError("selected table is not a whole DTTABLE1")
    if name not in entries:
        raise RuntimeError(f"no selected entry for {name}")
    fields = entries[name]
    return fields[:POSITION_AT], int.from_bytes(
        fields[POSITION_AT : POSITION_AT + 8], "big"
    )


def limit(directory: Path, keys: int) -> None:
    """Each voter's configuration with application_limits.keys set to `keys`."""
    for path in sorted(directory.glob("authority-*.json")):
        configuration = json.loads(path.read_text())
        configuration["application_limits"] = {"keys": keys}
        path.write_text(json.dumps(configuration))


async def write(writers: Writers, ordinals: list[int]) -> bool:
    """Sets each key in `ordinals` to its ordinal plus one, BATCH keys a transaction; False at
    the first transaction refused for capacity."""
    for offset in range(0, len(ordinals), BATCH):
        batch = ordinals[offset : offset + BATCH]
        operations = [
            {"key": f"{NAMESPACE}:key{ordinal}", "value": {"default": ordinal + 1}}
            for ordinal in batch
        ]
        if not await writers.transaction(0, operations):
            return False
        for ordinal in batch:
            writers.values[ordinal] = ordinal + 1
    return True


def check(condition: bool, what: str) -> None:
    if not condition:
        raise RuntimeError(what)


async def run(arguments: argparse.Namespace) -> None:
    old, new = arguments.old_keys, arguments.new_keys
    output = arguments.output.resolve()
    output.mkdir(parents=True, mode=0o700, exist_ok=False)
    evidence = Evidence(output / "events.jsonl", 64 * 1024 * 1024)
    certificates = arguments.certificates.resolve()
    async with Actors(evidence, 30) as actors:
        authority = Authority(
            arguments.binary.resolve(), certificates, output, actors, 1
        )
        limit(output, old)
        await authority.start()
        writers = Writers(authority, new, 1, 1)
        opened = await writers.successful(
            0, "/v2/open", {"expected_generation": 0, "nonce": f"{1:032x}"}
        )
        check(
            opened.get("generation") == 1, f"unexpected producer generation: {opened}"
        )
        check(
            await write(writers, list(range(old))),
            "the old ceiling refused its own keys",
        )
        cohorts = []
        for label, relay in (
            ("direct-", None),
            ("relayed-", arguments.relays.resolve()),
        ):
            directory = output / label.rstrip("-")
            directory.mkdir(mode=0o700)
            cohorts.append(
                ManagedCohort(
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
                )
            )
        for cohort in cohorts:
            await cohort.start()
            await cohort.wait_selected(cohort.positions(writers))
        before = {
            cohort.label: cursor(
                cohort.directory / "agent-0" / "generations", NAMESPACE
            )
            for cohort in cohorts
        }

        refused_sequence = writers.sequences[0] + 1
        refused = [{"key": f"{NAMESPACE}:key{old}", "value": {"default": old + 1}}]
        check(
            not await writers.transaction(0, refused),
            "the old ceiling admitted one key more",
        )
        receipt = writers.receipts[0]
        evidence.record("refused_at_ceiling", keys=old, receipt=receipt)

        # The coordinated restart: every voter stops, then all start with the raised limit.
        began = time.monotonic()
        await actors.stop(authority.members)
        limit(output, new)
        for index in range(3):
            authority.members[index] = await authority.start_member(index)
        await authority.leader()
        restarted = time.monotonic() - began
        survivors = [
            member for cohort in cohorts for member in cohort.members + cohort.relays
        ]
        check(
            all(member.process.returncode is None for member in survivors),
            "a relay or agent exited across the restart",
        )

        retried = await writers.successful(
            0,
            "/v2/transactions",
            {
                "generation": 1,
                "sequence": refused_sequence,
                "namespace": NAMESPACE,
                "operations": refused,
            },
        )
        check(retried == receipt, f"the retry lost its receipt: {retried} != {receipt}")
        check(
            await write(writers, list(range(old, new))),
            "the raised limit refused a key",
        )
        for cohort in cohorts:
            await cohort.verify(writers)
        after = {
            cohort.label: cursor(
                cohort.directory / "agent-0" / "generations", NAMESPACE
            )
            for cohort in cohorts
        }
        for label, (identity, position) in after.items():
            check(
                identity == before[label][0], f"{label}agent took another incarnation"
            )
            check(position > before[label][1], f"{label}agent did not advance")
        evidence.record(
            "raised",
            old=old,
            new=new,
            restart_seconds=restarted,
            positions={label: [before[label][1], after[label][1]] for label in after},
        )
        print(
            f"restart capacity: {old} to {new} keys; voters back in {restarted:.2f} s; "
            f"agents {[(before[label][1], after[label][1]) for label in after]}",
            flush=True,
        )
        for cohort in reversed(cohorts):
            await cohort.close()
    print("restart capacity: ok", flush=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True, help="detent-authority")
    parser.add_argument("--agents", type=Path, required=True, help="detent-agent")
    parser.add_argument("--relays", type=Path, required=True, help="detent-relay")
    parser.add_argument("--local-oracle", type=Path, required=True)
    parser.add_argument("--certificates", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--old-keys", type=int, default=1000)
    parser.add_argument("--new-keys", type=int, default=2000)
    arguments = parser.parse_args()
    if not 100 <= arguments.old_keys < arguments.new_keys <= 100000:
        raise SystemExit("keys must rise, within 100 to 100,000")
    try:
        asyncio.run(run(arguments))
    except Exception as error:
        print(f"test_restart_capacity: {error!r}", file=sys.stderr, flush=True)
        raise SystemExit(1) from error


if __name__ == "__main__":
    main()
