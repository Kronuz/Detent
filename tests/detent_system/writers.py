"""One outstanding command per producer, exact retries and complete final reads."""

from __future__ import annotations

import asyncio
import http.client
import random
import struct
import time
from array import array
from collections import Counter
from typing import Any, BinaryIO, Protocol

from http_activity import GeneratorAdmissionRefused


class AuthorityRequests(Protocol):
    leader_index: int

    async def request(
        self,
        index: int,
        path: str,
        fields: dict[str, Any] | None = None,
        producer: int = 0,
        *,
        admission_deadline: float | None = None,
    ) -> tuple[int, dict[str, Any]]: ...

    async def leader(self) -> int: ...


class Writers:
    def __init__(
        self,
        authority: AuthorityRequests,
        records: int,
        producers: int,
        seed: int,
    ) -> None:
        if records < producers:
            raise ValueError("writer scenario needs at least one record per producer")
        self.authority = authority
        self.records = records
        self.producers = producers
        self.seed = seed
        self.values = array("q", [0]) * records
        self.sequences = [0] * producers
        self.receipts: list[dict[str, Any] | None] = [None] * producers
        self.counts: Counter[str] = Counter()
        self.latency_total = 0.0
        self.latency_maximum = 0.0

    async def successful(
        self, producer: int, path: str, fields: dict[str, Any]
    ) -> dict[str, Any]:
        deadline = time.monotonic() + 30
        attempts = 0
        unknown = False
        while time.monotonic() < deadline:
            attempts += 1
            self.counts["offered_attempts"] += 1
            try:
                code, reply = await self.authority.request(
                    self.authority.leader_index,
                    path,
                    fields,
                    producer,
                    admission_deadline=deadline,
                )
            except GeneratorAdmissionRefused:
                self.counts["generator_http_admission_refusals"] += 1
                if attempts > 1:
                    self.counts["unresolved_at_generator_deadline"] += 1
                raise
            except (OSError, http.client.HTTPException):
                self.counts["transport_unknown"] += 1
                unknown = True
                await self.authority.leader()
                continue
            terminal = code == 429 and reply.get("status") == "capacity"
            terminal |= (
                code == 409
                and reply.get("status") == "conflict"
                and path in ("/v2/transactions", "/v2/result")
            )
            if code == 200 or terminal:
                if attempts > 1:
                    self.counts["retried_commands"] += 1
                if unknown:
                    self.counts["reconciled_unknown_commands"] += 1
                return reply
            if code not in (429, 503):
                raise RuntimeError(f"unexpected authority response: {code} {reply}")
            self.counts["retryable_responses"] += 1
            await asyncio.sleep(0.05)
            await self.authority.leader()
        raise RuntimeError(
            f"unreconciled producer {producer} request after thirty seconds"
        )

    def write_expected(self, stream: BinaryIO, revision: int) -> None:
        stream.write(struct.pack("<QQQ", self.records, self.producers, revision))
        stream.writelines(struct.pack("<q", value) for value in self.values)

    async def transaction(
        self, producer: int, operations: list[dict[str, Any]]
    ) -> bool:
        sequence = self.sequences[producer] + 1
        fields = {
            "generation": 1,
            "sequence": sequence,
            "namespace": f"load{producer}",
            "operations": operations,
        }
        self.counts["logical_commands"] += 1
        started = time.monotonic()
        receipt = await self.successful(producer, "/v2/transactions", fields)
        if receipt.get("generation") != 1 or receipt.get("sequence") != sequence:
            raise RuntimeError(f"unexpected producer receipt: {receipt}")
        if receipt.get("status") not in ("applied", "no_change", "capacity"):
            raise RuntimeError(f"transaction was not applied: {receipt}")
        elapsed = time.monotonic() - started
        self.latency_total += elapsed
        self.latency_maximum = max(self.latency_maximum, elapsed)
        self.sequences[producer] = sequence
        self.receipts[producer] = receipt
        applied = receipt["status"] != "capacity"
        self.counts["acknowledged" if applied else "terminal_capacity_rejections"] += 1
        return applied

    async def prepare(self, producer: int) -> None:
        opened = await self.successful(
            producer,
            "/v2/open",
            {"expected_generation": 0, "nonce": f"{producer + 1:032x}"},
        )
        if opened.get("generation") != 1:
            raise RuntimeError(f"unexpected producer generation: {opened}")
        operations: list[dict[str, Any]] = []
        for ordinal in range(producer, self.records, self.producers):
            operations.append(
                {
                    "key": f"load{producer}:key{ordinal}",
                    "value": {"type": "int", "default": 0},
                }
            )
            if len(operations) == 128:
                if not await self.transaction(producer, operations):
                    raise RuntimeError(
                        "authority capacity cannot seed configured corpus"
                    )
                operations = []
        if operations and not await self.transaction(producer, operations):
            raise RuntimeError("authority capacity cannot seed configured corpus")

    async def mutate(self, producer: int, deadline: float) -> None:
        rng = random.Random(self.seed + producer)
        count = len(range(producer, self.records, self.producers))
        while time.monotonic() < deadline:
            ordinal = producer + self.producers * rng.randrange(count)
            value = self.values[ordinal] + 1
            applied = await self.transaction(
                producer,
                [
                    {
                        "key": f"load{producer}:key{ordinal}",
                        "value": {"type": "int", "default": value},
                    }
                ],
            )
            if applied:
                self.values[ordinal] = value

    async def verify(self, producer: int) -> None:
        actual = await self.successful(
            producer,
            "/v2/result",
            {"generation": 1, "sequence": self.sequences[producer]},
        )
        if actual != self.receipts[producer]:
            raise RuntimeError("retained producer receipt changed")
        ordinals = range(producer, self.records, self.producers)
        for offset in range(0, len(ordinals), 128):
            batch = ordinals[offset : offset + 128]
            keys = [f"load{producer}:key{ordinal}" for ordinal in batch]
            reply = await self.successful(
                producer, "/v2/read", {"namespace": f"load{producer}", "keys": keys}
            )
            expected = {
                key: {"type": "int", "default": self.values[ordinal]}
                for key, ordinal in zip(keys, batch, strict=True)
            }
            if reply.get("knobs") != expected:
                raise RuntimeError(
                    f"complete-state oracle mismatch for producer {producer}"
                )

    async def phase(self, operation: str, deadline: float = 0) -> None:
        async with asyncio.TaskGroup() as group:
            for producer in range(self.producers):
                if operation == "prepare":
                    group.create_task(self.prepare(producer))
                elif operation == "mutate":
                    group.create_task(self.mutate(producer, deadline))
                elif operation == "verify":
                    group.create_task(self.verify(producer))
                else:
                    raise ValueError("unknown writer phase")
