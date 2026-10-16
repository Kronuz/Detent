from __future__ import annotations

import asyncio
import random
import time
import unittest
from builtins import ExceptionGroup
from collections import Counter

from pacing import Schedule, paced_mutations


class ScheduleTests(unittest.TestCase):
    def test_ticks_and_late_refusals_are_bounded(self) -> None:
        schedule = Schedule(10, 12, 2, 2)
        self.assertEqual(schedule.take(10), (range(1), 0))
        self.assertEqual(schedule.take(10.5), (range(1, 2), 0))
        self.assertEqual(schedule.take(11.9), (range(2, 4), 0))
        self.assertEqual(schedule.take(12), (range(4, 4), 0))
        overdue = Schedule(0, 20, 1000, 4)
        ticks, missed = overdue.take(10.5)
        self.assertEqual(list(ticks), [10497, 10498, 10499, 10500])
        self.assertEqual(missed, 10497)
        self.assertEqual(overdue.next_at(), 10.501)
        self.assertEqual(overdue.take(21), (range(20000, 20000), 9499))

    def test_invalid_rates_refuse_before_scheduling(self) -> None:
        for rate in (0, -1, float("nan"), float("inf"), 1000001):
            with self.assertRaises(ValueError):
                Schedule(0, 1, rate, 2)


class SlowWriter:
    def __init__(self, *, fail: bool = False) -> None:
        self.producers = 2
        self.seed = 17
        self.counts: Counter[str] = Counter()
        self.active: set[int] = set()
        self.peak = 0
        self.finished = 0
        self.fail = fail

    async def mutate_once(self, producer: int, rng: random.Random, turn: int) -> None:
        if producer in self.active:
            raise RuntimeError("overlapping producer originals")
        self.active.add(producer)
        self.peak = max(self.peak, len(self.active))
        try:
            await asyncio.sleep(0.02)
            if self.fail:
                raise RuntimeError("accepted failure")
        finally:
            self.active.remove(producer)
            self.finished += 1


class PacingTests(unittest.IsolatedAsyncioTestCase):
    async def test_busy_producers_refuse_without_pending_queue(self) -> None:
        writer = SlowWriter()
        await paced_mutations(writer, 1000, time.monotonic() + 0.1)
        self.assertEqual(writer.active, set())
        self.assertEqual(writer.peak, 2)
        self.assertGreater(writer.counts["generator_occupied_refusals"], 0)
        self.assertEqual(writer.counts["admitted_workloads"], writer.finished)
        self.assertEqual(
            writer.counts["scheduled_workloads"],
            writer.counts["admitted_workloads"]
            + writer.counts["generator_occupied_refusals"]
            + writer.counts["generator_late_refusals"],
        )

    async def test_low_rate_keeps_full_measurement_interval(self) -> None:
        writer = SlowWriter()
        deadline = time.monotonic() + 0.08
        await paced_mutations(writer, 1, deadline)
        self.assertGreaterEqual(time.monotonic(), deadline)
        self.assertEqual(writer.finished, 1)

    async def test_failure_drains_all_accepted_tasks(self) -> None:
        writer = SlowWriter(fail=True)
        with self.assertRaises(ExceptionGroup):
            await paced_mutations(writer, 1000, time.monotonic() + 1)
        self.assertEqual(writer.active, set())
        self.assertEqual(writer.counts["admitted_workloads"], writer.finished)
