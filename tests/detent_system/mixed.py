"""Deterministic typed mutations with a compact commit-ordered state oracle."""

from __future__ import annotations

import json
import random
import struct
import time
from array import array
from collections.abc import Mapping
from typing import Any, BinaryIO, override

from authority import CONTEXT
from writers import AuthorityRequests, Writers

MAXIMUM_GENERATION = (1 << 40) - 1
UNSEEDED = -(1 << 63)
ATOMIC_OPERATIONS = 128


def definition(ordinal: int, generation: int) -> dict[str, Any] | None:
    counter = generation if generation >= 0 else -generation - 1
    if ordinal < 0 or not 0 <= counter <= MAXIMUM_GENERATION:
        raise ValueError("mixed definition is outside the generation bounds")
    if generation < 0:
        return None
    if (ordinal + generation) % 2:
        value = (generation * 104729 + ordinal * 17) % (1 << 60)
        return {
            "type": "int",
            "default": -value if (ordinal + generation) & 2 else value,
        }
    default = bool((ordinal // 2 + generation) & 1)
    return {
        "type": "bool",
        "default": default,
        "rollout_pct": None
        if (ordinal + generation) % 3 == 0
        else (ordinal * 13 + generation * 7) % 101,
        "overrides": {
            "force-off": False,
            "force-on": True,
            "opaque\0group": not default,
            "ramp": {"rollout_pct": (ordinal * 17 + generation * 11) % 101},
        },
    }


class MixedWriters(Writers):
    def __init__(
        self,
        authority: AuthorityRequests,
        records: int,
        producers: int,
        seed: int,
        *,
        shared_namespace: str | None = None,
        maximum_records: int = 500000,
    ) -> None:
        if (
            not 1 <= producers <= 256
            or not producers <= records <= maximum_records
            or not 1 <= maximum_records < 2**32
        ):
            raise ValueError(
                "mixed writers exceed the configured producer or record bounds"
            )
        if shared_namespace is not None and (
            not 1 <= len(shared_namespace) <= 198
            or any(
                not (character.isascii() and character.isalnum())
                and character not in "._/-"
                for character in shared_namespace
            )
            or len(shared_namespace) + len(f":key{records - 1}") > 200
        ):
            raise ValueError("invalid shared mixed namespace")
        super().__init__(authority, records, producers, seed)
        self.shared_namespace = shared_namespace
        self.namespaces = tuple(
            sorted({self.namespace(producer) for producer in range(producers)})
        )
        self.values = array("q", [UNSEEDED]) * records
        self.key_revisions = array("Q", [0]) * records
        self.namespace_revisions = dict.fromkeys(self.namespaces, 0)
        self._pending = [False] * producers
        self._deleted: list[list[int]] = [[] for _ in range(producers)]

    def namespace(self, producer: int) -> str:
        if not 0 <= producer < self.producers:
            raise ValueError("mixed producer is outside the configured cohort")
        return self.shared_namespace or f"load{producer}"

    def key(self, ordinal: int) -> str:
        if not 0 <= ordinal < self.records:
            raise ValueError("mixed ordinal is outside the configured corpus")
        return f"{self.namespace(ordinal % self.producers)}:key{ordinal}"

    def _observe(self, edits: list[tuple[int, int]], revision: int) -> None:
        for ordinal, generation in edits:
            previous = self.key_revisions[ordinal]
            if revision < previous:
                self.counts["late_oracle_observations"] += 1
                continue
            if revision == previous and self.values[ordinal] != UNSEEDED:
                actual = definition(ordinal, self.values[ordinal])
                if actual != definition(ordinal, generation):
                    raise RuntimeError(
                        "different mixed definitions at one commit revision"
                    )
            self.key_revisions[ordinal] = revision
            self.values[ordinal] = generation

    async def submit(
        self,
        producer: int,
        edits: list[tuple[int, int]],
        *,
        force_conflict: bool = False,
        conditions: Mapping[int, tuple[int, int | None]] | None = None,
    ) -> dict[str, Any]:
        namespace = self.namespace(producer)
        if self._pending[producer]:
            raise RuntimeError("mixed producer already has an outstanding command")
        if len(edits) > ATOMIC_OPERATIONS or len(
            {ordinal for ordinal, _ in edits}
        ) != len(edits):
            raise ValueError("mixed atomic command exceeds its unique-operation bound")
        operations: list[dict[str, Any]] = []
        for ordinal, generation in edits:
            key = self.key(ordinal)
            if self.namespace(ordinal % self.producers) != namespace:
                raise ValueError("mixed command crosses its authorized namespace")
            operation: dict[str, Any] = {
                "key": key,
                "value": definition(ordinal, generation),
            }
            if force_conflict:
                operation["expected_revision"] = (1 << 64) - 1
            elif conditions is not None and ordinal in conditions:
                expected, absent = conditions[ordinal]
                operation["expected_revision"] = expected
                if absent is not None:
                    operation["absent_namespace_revision"] = absent
            operations.append(operation)
        if not operations:
            operations.append({"key": f"{namespace}:noop{producer}", "value": None})
        sequence = self.sequences[producer] + 1
        fields: dict[str, Any] = {
            "generation": 1,
            "sequence": sequence,
            "namespace": namespace,
            "operations": operations,
        }
        if force_conflict and not edits:
            fields["expected_namespace"] = (1 << 64) - 1
        self.counts["logical_commands"] += 1
        self.counts["atomic_operation_intents"] += len(operations)
        self.counts["largest_atomic_operations"] = max(
            self.counts["largest_atomic_operations"], len(operations)
        )
        started = time.monotonic()
        self._pending[producer] = True
        try:
            receipt = await self.successful(producer, "/v2/transactions", fields)
            status = receipt.get("status")
            revision, namespace_revision = (
                receipt.get("revision"),
                receipt.get("namespace_revision"),
            )
            if (
                type(receipt.get("generation")) is not int
                or receipt.get("generation") != 1
                or type(receipt.get("sequence")) is not int
                or receipt.get("sequence") != sequence
                or status not in ("applied", "no_change", "capacity", "conflict")
                or type(revision) is not int
                or type(namespace_revision) is not int
                or not 0 <= namespace_revision <= revision < (1 << 64)
                or (status == "applied" and revision != namespace_revision)
                or (force_conflict and status != "conflict")
            ):
                raise RuntimeError(f"unexpected mixed producer receipt: {receipt}")
            self.sequences[producer] = sequence
            self.receipts[producer] = receipt
            self.namespace_revisions[namespace] = max(
                self.namespace_revisions[namespace], namespace_revision
            )
            if status in ("applied", "no_change"):
                self._observe(edits, revision)
                self.counts["acknowledged"] += 1
                self.counts[status] += 1
            else:
                self.counts[
                    "terminal_cas_conflicts"
                    if status == "conflict"
                    else "terminal_capacity_rejections"
                ] += 1
            return receipt
        finally:
            self._pending[producer] = False
            elapsed = time.monotonic() - started
            self.latency_total += elapsed
            self.latency_maximum = max(self.latency_maximum, elapsed)

    @override
    async def transaction(
        self, producer: int, operations: list[dict[str, Any]]
    ) -> bool:
        raise ValueError("mixed transactions require submit() generation recipes")

    @override
    async def prepare(self, producer: int) -> None:
        self.namespace(producer)
        opened = await self.successful(
            producer,
            "/v2/open",
            {"expected_generation": 0, "nonce": f"{producer + 1:032x}"},
        )
        if opened.get("generation") != 1:
            raise RuntimeError(f"unexpected mixed producer generation: {opened}")
        ordinals = range(producer, self.records, self.producers)
        for offset in range(0, len(ordinals), ATOMIC_OPERATIONS):
            edits = [
                (ordinal, 0)
                for ordinal in ordinals[offset : offset + ATOMIC_OPERATIONS]
            ]
            if (await self.submit(producer, edits))["status"] not in (
                "applied",
                "no_change",
            ):
                raise RuntimeError("authority capacity cannot seed the mixed corpus")

    def _sample(self, producer: int, count: int, rng: random.Random) -> list[int]:
        if self.shared_namespace is None:
            population = range(producer, self.records, self.producers)
        elif rng.randrange(5):
            population = range(min(self.records, 4096))
        else:
            population = range(self.records)
        return rng.sample(population, min(count, len(population)))

    async def workload(
        self, producer: int, size: int, mode: str, rng: random.Random
    ) -> None:
        if size not in (1, 10, ATOMIC_OPERATIONS, 1000) or mode not in (
            "upsert",
            "delete",
            "reinsert",
            "repeat",
            "conflict",
            "cas",
            "noop",
        ):
            raise ValueError("invalid mixed workload recipe")
        self.counts[f"requested_workloads_{size}"] += 1
        self.counts[f"workload_{mode}"] += 1
        if mode == "noop":
            receipt = await self.submit(producer, [])
            if receipt["status"] != "no_change":
                raise RuntimeError("absent-key deletion changed mixed state")
            return
        ordinals = (
            self._deleted[producer]
            if mode == "reinsert" and self._deleted[producer]
            else self._sample(producer, size, rng)
        )
        self.counts["requested_operation_intents"] += size
        self.counts["sized_operation_intents"] += len(ordinals)
        for offset in range(0, len(ordinals), ATOMIC_OPERATIONS):
            selected = ordinals[offset : offset + ATOMIC_OPERATIONS]
            counter = (self.sequences[producer] + 1) * self.producers + producer + 1
            if counter > MAXIMUM_GENERATION:
                raise RuntimeError("mixed generation counter exhausted")
            conditions: dict[int, tuple[int, int | None]] | None = None
            if mode == "cas":
                reply = await self.successful(
                    producer,
                    "/v2/read",
                    {
                        "namespace": self.namespace(producer),
                        "keys": [self.key(ordinal) for ordinal in selected],
                    },
                )
                revisions = reply.get("key_revisions")
                namespace_revision = reply.get("namespace_revision")
                if (
                    not isinstance(revisions, dict)
                    or type(namespace_revision) is not int
                ):
                    raise RuntimeError("invalid mixed CAS witness")
                conditions = {}
                for ordinal in selected:
                    expected = revisions.get(self.key(ordinal), 0)
                    if (
                        type(expected) is not int
                        or not 0 <= expected <= namespace_revision
                    ):
                        raise RuntimeError("invalid mixed CAS key revision")
                    conditions[ordinal] = (
                        expected,
                        namespace_revision if not expected else None,
                    )
            edits = []
            for ordinal in selected:
                generation = self.values[ordinal] if mode == "repeat" else counter
                if generation == UNSEEDED:
                    raise RuntimeError("mixed workload started before corpus seeding")
                if mode == "delete":
                    generation = -counter - 1
                edits.append((ordinal, generation))
            self.counts["atomic_subbatches"] += 1
            await self.submit(
                producer,
                edits,
                force_conflict=mode == "conflict",
                conditions=conditions,
            )
        if mode == "delete":
            self._deleted[producer] = ordinals
        elif mode == "reinsert":
            self._deleted[producer] = []

    @override
    async def mutate_once(self, producer: int, rng: random.Random, turn: int) -> None:
        recipes = (
            (1, "upsert"),
            (10, "upsert"),
            (1000, "upsert"),
            (1, "repeat"),
            (10, "delete"),
            (10, "reinsert"),
            (1, "conflict"),
            (1, "cas"),
            (1, "noop"),
        )
        size, mode = recipes[turn % len(recipes)]
        await self.workload(producer, size, mode, rng)

    @override
    async def verify(self, producer: int) -> None:
        receipt = await self.successful(
            producer,
            "/v2/result",
            {"generation": 1, "sequence": self.sequences[producer]},
        )
        if receipt != self.receipts[producer]:
            raise RuntimeError("retained mixed producer receipt changed")
        namespace = self.namespace(producer)
        revision = max(
            int(value["revision"]) for value in self.receipts if value is not None
        )
        ordinals = range(producer, self.records, self.producers)
        for offset in range(0, len(ordinals), ATOMIC_OPERATIONS):
            selected = ordinals[offset : offset + ATOMIC_OPERATIONS]
            keys = [self.key(ordinal) for ordinal in selected]
            reply = await self.successful(
                producer, "/v2/read", {"namespace": namespace, "keys": keys}
            )
            expected: dict[str, Any] = {}
            for key, ordinal in zip(keys, selected, strict=True):
                value = definition(ordinal, self.values[ordinal])
                if value is not None:
                    expected[key] = value
            revisions = reply.get("key_revisions")
            if (
                reply.get("authority") != CONTEXT
                or type(reply.get("version")) is not int
                or reply.get("version") != revision
                or type(reply.get("schema_version")) is not int
                or reply.get("schema_version") != 1
                or type(reply.get("namespace_revision")) is not int
                or reply.get("namespace_revision")
                != self.namespace_revisions[namespace]
                or json.dumps(reply.get("knobs"), sort_keys=True)
                != json.dumps(expected, sort_keys=True)
                or not isinstance(revisions, dict)
                or revisions.keys() != expected.keys()
                or any(
                    type(value) is not int
                    or not 0 < value <= self.namespace_revisions[namespace]
                    for value in revisions.values()
                )
            ):
                raise RuntimeError(
                    f"complete mixed definition oracle mismatch for producer {producer}"
                )

    @override
    def write_expected(self, stream: BinaryIO, revision: int) -> None:
        namespace = (self.shared_namespace or "").encode("ascii")
        stream.write(b"DTMIXED1")
        stream.write(
            struct.pack("<QQQQ", self.records, self.producers, revision, len(namespace))
        )
        stream.write(namespace)
        for value in self.values:
            counter = value if value >= 0 else -value - 1
            if not 0 <= counter <= MAXIMUM_GENERATION:
                raise ValueError("unseeded or invalid mixed oracle recipe")
            stream.write(struct.pack("<q", value))
