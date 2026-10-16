"""Fixed-size observations of actual HTTP workers and executor queueing."""

from __future__ import annotations

import asyncio
import threading
import time
from collections.abc import Generator
from concurrent.futures import ThreadPoolExecutor
from contextlib import contextmanager


class GeneratorAdmissionRefused(RuntimeError):
    """No blocking HTTP original was submitted before its admission deadline."""


class HTTPActivity:
    def __init__(self) -> None:
        self.lock = threading.Lock()
        self.active = 0
        self.peak = 0
        self.completed = 0
        self.failed = 0
        self.queue_ns = [0] * 64
        self.service_ns = [0] * 64

    @contextmanager
    def observe(self, submitted_ns: int) -> Generator[None]:
        started = time.monotonic_ns()
        with self.lock:
            self.active += 1
            self.peak = max(self.peak, self.active)
            self.queue_ns[min(63, max(0, started - submitted_ns).bit_length())] += 1
        failed = False
        try:
            yield
        except BaseException:
            failed = True
            raise
        finally:
            elapsed = time.monotonic_ns() - started
            with self.lock:
                self.active -= 1
                self.completed += 1
                self.failed += failed
                self.service_ns[min(63, elapsed.bit_length())] += 1

    def snapshot(self, *, reset: bool = False) -> dict[str, int | list[int]]:
        with self.lock:
            result: dict[str, int | list[int]] = {
                "active": self.active,
                "peak_active": self.peak,
                "completed": self.completed,
                "failed": self.failed,
                "queue_ns_log2": self.queue_ns.copy(),
                "service_ns_log2": self.service_ns.copy(),
            }
            if reset:
                self.peak = self.active
                self.completed = self.failed = 0
                self.queue_ns = [0] * 64
                self.service_ns = [0] * 64
            return result


def configure_workers(producers: int, requested: int | None) -> int:
    workers = producers if requested is None else requested
    if not 1 <= workers <= 264:
        raise ValueError("HTTP worker count must be between 1 and 264")
    asyncio.get_running_loop().set_default_executor(
        ThreadPoolExecutor(max_workers=workers, thread_name_prefix="detent-load")
    )
    return workers
