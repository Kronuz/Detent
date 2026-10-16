"""Offer fixed-rate logical workloads without queueing behind occupied producers."""

from __future__ import annotations

import asyncio
import math
import random
import time
from collections import Counter
from typing import Protocol


class PacedWriter(Protocol):
    producers: int
    seed: int
    counts: Counter[str]

    async def mutate_once(
        self, producer: int, rng: random.Random, turn: int
    ) -> None: ...


def validate_rate(rate: float | None) -> None:
    if rate is not None and (not math.isfinite(rate) or not 0 < rate <= 1000000):
        raise ValueError(
            "offered workload rate must be finite and between zero and 1M/s"
        )


class Schedule:
    def __init__(self, start: float, end: float, rate: float, budget: int) -> None:
        validate_rate(rate)
        if budget < 1:
            raise ValueError("invalid bounded workload schedule")
        self.start, self.end, self.rate, self.budget = start, end, rate, budget
        self.total = math.ceil(max(0, end - start) * rate)
        self.cursor = 0

    def take(self, now: float) -> tuple[range, int]:
        due = min(self.total, max(0, math.floor((now - self.start) * self.rate) + 1))
        first = max(self.cursor, due - self.budget)
        missed = first - self.cursor
        if now >= self.end:
            missed = self.total - self.cursor
            first = due = self.total
        self.cursor = due
        return range(first, due), missed

    def next_at(self) -> float:
        return min(self.end, self.start + self.cursor / self.rate)


async def paced_mutations(writer: PacedWriter, rate: float, deadline: float) -> None:
    schedule = Schedule(time.monotonic(), deadline, rate, writer.producers)
    randoms = [random.Random(writer.seed + index) for index in range(writer.producers)]
    turns = [0] * writer.producers
    pending: list[asyncio.Task[None] | None] = [None] * writer.producers
    async with asyncio.TaskGroup() as group:
        while schedule.cursor < schedule.total:
            ticks, missed = schedule.take(time.monotonic())
            writer.counts["scheduled_workloads"] += len(ticks) + missed
            writer.counts["generator_late_refusals"] += missed
            for tick in ticks:
                producer = tick % writer.producers
                original = pending[producer]
                if original is not None and not original.done():
                    writer.counts["generator_occupied_refusals"] += 1
                    continue
                if original is not None:
                    original.result()
                writer.counts["admitted_workloads"] += 1
                pending[producer] = group.create_task(
                    writer.mutate_once(producer, randoms[producer], turns[producer])
                )
                turns[producer] += 1
            if schedule.cursor < schedule.total:
                await asyncio.sleep(max(0, schedule.next_at() - time.monotonic()))
        await asyncio.sleep(max(0, deadline - time.monotonic()))
