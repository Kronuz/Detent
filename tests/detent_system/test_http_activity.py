from __future__ import annotations

import threading
import time
import unittest
from concurrent.futures import ThreadPoolExecutor

from http_activity import HTTPActivity


class HTTPActivityTests(unittest.TestCase):
    def test_counts_actual_workers_and_original_completion(self) -> None:
        activity = HTTPActivity()
        entered = threading.Barrier(3)
        release = threading.Event()

        def worker() -> None:
            with activity.observe(time.monotonic_ns() - 10000):
                entered.wait(timeout=3)
                if not release.wait(timeout=3):
                    raise TimeoutError("worker release")

        with ThreadPoolExecutor(max_workers=2) as executor:
            originals = [executor.submit(worker) for _ in range(2)]
            try:
                entered.wait(timeout=3)
                snapshot = activity.snapshot()
                self.assertEqual(snapshot["active"], 2)
                self.assertEqual(snapshot["peak_active"], 2)
                self.assertEqual(snapshot["completed"], 0)
            finally:
                release.set()
            for original in originals:
                original.result(timeout=3)
        snapshot = activity.snapshot()
        self.assertEqual(snapshot["active"], 0)
        self.assertEqual(snapshot["completed"], 2)
        for field in ("queue_ns_log2", "service_ns_log2"):
            buckets = snapshot[field]
            assert isinstance(buckets, list)
            self.assertEqual(len(buckets), 64)
            self.assertEqual(sum(buckets), 2)

    def test_failure_and_reset_preserve_inflight_ownership(self) -> None:
        activity = HTTPActivity()
        with activity.observe(time.monotonic_ns()):
            before = activity.snapshot(reset=True)
            self.assertEqual(before["active"], 1)
            self.assertEqual(activity.snapshot()["active"], 1)
        with (
            self.assertRaisesRegex(RuntimeError, "transport"),
            activity.observe(time.monotonic_ns()),
        ):
            raise RuntimeError("transport")
        settled = activity.snapshot(reset=True)
        self.assertEqual(settled["active"], 0)
        self.assertEqual(settled["completed"], 2)
        self.assertEqual(settled["failed"], 1)
        self.assertEqual(activity.snapshot()["completed"], 0)
