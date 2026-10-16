from __future__ import annotations

import asyncio
import threading
import time
import unittest
from typing import Any, override

from authority import Authority
from http_activity import GeneratorAdmissionRefused, HTTPActivity, configure_workers


class HeldAuthority(Authority):
    def __init__(self) -> None:
        self.lane = asyncio.Semaphore(1)
        self.http_activity = HTTPActivity()
        self.entered = threading.Event()
        self.release = threading.Event()
        self.submitted = 0

    @override
    def _exchange(
        self,
        index: int,
        path: str,
        fields: dict[str, Any] | None,
        producer: int,
        endpoint_port: int | None,
    ) -> tuple[int, dict[str, Any]]:
        self.submitted += 1
        self.entered.set()
        if not self.release.wait(timeout=3):
            raise TimeoutError("held HTTP original")
        return 200, {"ok": True}


class AdmissionTests(unittest.IsolatedAsyncioTestCase):
    async def test_held_original_retains_slot_through_cancellation(self) -> None:
        configure_workers(1, 1)
        authority = HeldAuthority()
        original = asyncio.create_task(authority.request(0, "/held"))
        try:
            deadline = time.monotonic() + 2
            while not authority.entered.is_set():
                if time.monotonic() >= deadline:
                    raise TimeoutError("original did not enter worker")
                await asyncio.sleep(0.001)
            original.cancel()
            await asyncio.sleep(0.01)
            self.assertFalse(original.done())
            with self.assertRaises(GeneratorAdmissionRefused):
                await authority.request(
                    0, "/unsent", admission_deadline=time.monotonic() + 0.03
                )
            self.assertEqual(authority.submitted, 1)
            self.assertEqual(authority.http_activity.snapshot()["active"], 1)
        finally:
            authority.release.set()
            with self.assertRaises(asyncio.CancelledError):
                await original
        self.assertEqual(authority.http_activity.snapshot()["active"], 0)
        self.assertEqual(await authority.request(0, "/next"), (200, {"ok": True}))
        self.assertEqual(authority.submitted, 2)

    async def test_expired_deadline_does_not_submit_with_a_free_slot(self) -> None:
        authority = HeldAuthority()
        with self.assertRaises(GeneratorAdmissionRefused):
            await authority.request(
                0, "/unsent", admission_deadline=time.monotonic() - 1
            )
        self.assertEqual(authority.submitted, 0)
        authority.release.set()
        self.assertEqual(await authority.request(0, "/next"), (200, {"ok": True}))
