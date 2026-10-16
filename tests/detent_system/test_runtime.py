from __future__ import annotations

import asyncio
import json
import signal
import sys
import tempfile
import unittest
from builtins import ExceptionGroup
from pathlib import Path
from typing import override
from unittest.mock import patch

from runtime import Actors, Evidence, Scenario


class RuntimeTests(unittest.IsolatedAsyncioTestCase):
    @override
    async def asyncSetUp(self) -> None:
        scratch = Path.cwd() / ".scratch"
        scratch.mkdir(exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=scratch)
        self.directory = Path(self.temporary.name)
        self.evidence = Evidence(self.directory / "events.jsonl", 4096)

    @override
    async def asyncTearDown(self) -> None:
        self.evidence.close()
        self.temporary.cleanup()

    async def test_launch_failure_closes_prior_actor(self) -> None:
        group = Actors(self.evidence, 1)
        with self.assertRaises(FileNotFoundError):
            async with group:
                actor = await group.start(
                    "owned", [sys.executable, "-c", "import time;time.sleep(60)"]
                )
                await group.start("missing", [str(self.directory / "missing")])
        self.assertIsNotNone(actor.process.returncode)
        self.assertEqual(group.children, [])

    async def test_repeated_cancel_waits_for_original_launch(self) -> None:
        spawned = asyncio.Event()
        released = asyncio.Event()
        original = asyncio.create_subprocess_exec

        async def delayed(
            *command: str, stdin: int, stdout: int, stderr: int, limit: int
        ) -> asyncio.subprocess.Process:
            process = await original(
                *command, stdin=stdin, stdout=stdout, stderr=stderr, limit=limit
            )
            spawned.set()
            await released.wait()
            return process

        group = Actors(self.evidence, 1)
        with patch("asyncio.create_subprocess_exec", delayed):
            async with group:
                pending = asyncio.create_task(
                    group.start(
                        "cancelled",
                        [sys.executable, "-c", "import time;time.sleep(60)"],
                    )
                )
                await asyncio.wait_for(spawned.wait(), 2)
                pending.cancel()
                await asyncio.sleep(0)
                pending.cancel()
                await asyncio.sleep(0)
                self.assertFalse(pending.done())
                released.set()
                with self.assertRaises(asyncio.CancelledError):
                    await pending
                actor = group.children[0]
        self.assertEqual(actor.process.returncode, -signal.SIGTERM)

    async def test_repeated_cancel_does_not_interrupt_shutdown(self) -> None:
        group = Actors(self.evidence, 1)
        await group.start("owned", [sys.executable, "-c", "import time;time.sleep(60)"])
        actor = group.children[0]
        entered = asyncio.Event()
        released = asyncio.Event()
        original = group.close

        async def delayed_close() -> None:
            entered.set()
            await released.wait()
            await original()

        with patch.object(group, "close", delayed_close):
            pending = asyncio.create_task(group.__aexit__())
            await entered.wait()
            pending.cancel()
            await asyncio.sleep(0)
            pending.cancel()
            await asyncio.sleep(0)
            self.assertFalse(pending.done())
            released.set()
            with self.assertRaises(asyncio.CancelledError):
                await pending
        self.assertIsNotNone(actor.process.returncode)
        self.assertEqual(group.children, [])

    async def test_capture_failure_still_drains_every_actor(self) -> None:
        group = Actors(self.evidence, 1)
        async with group:
            first = await group.start(
                "first", [sys.executable, "-c", "import time;time.sleep(60)"]
            )
            second = await group.start(
                "second", [sys.executable, "-c", "import time;time.sleep(60)"]
            )
            self.evidence.close()
            with self.assertRaises(ExceptionGroup):
                await group.close()
        self.assertIsNotNone(first.process.returncode)
        self.assertIsNotNone(second.process.returncode)
        self.assertEqual(group.children, [])

    async def test_duplicate_scenario_fields_are_rejected(self) -> None:
        path = self.directory / "duplicate.json"
        path.write_text('{"name":"one","name":"two"}')
        with self.assertRaisesRegex(ValueError, "duplicate"):
            Scenario.load(path)

    async def test_concurrent_launches_cannot_exceed_capacity(self) -> None:
        group = Actors(self.evidence, 1, maximum=1)
        async with group:
            results = await asyncio.gather(
                group.start(
                    "one", [sys.executable, "-c", "import time;time.sleep(60)"]
                ),
                group.start(
                    "two", [sys.executable, "-c", "import time;time.sleep(60)"]
                ),
                return_exceptions=True,
            )
            self.assertEqual(len(group.children), 1)
            self.assertEqual(
                sum(isinstance(result, ValueError) for result in results), 1
            )

    async def test_partial_stop_preserves_other_owned_children(self) -> None:
        group = Actors(self.evidence, 1)
        async with group:
            first = await group.start(
                "first", [sys.executable, "-c", "import time;time.sleep(60)"]
            )
            second = await group.start(
                "second", [sys.executable, "-c", "import time;time.sleep(60)"]
            )
            await group.stop([first])
            self.assertIsNotNone(first.process.returncode)
            self.assertIsNone(second.process.returncode)
            self.assertEqual(group.children, [second])
        self.assertIsNotNone(second.process.returncode)

    async def test_owned_input_and_observed_output(self) -> None:
        output = bytearray()
        async with Actors(self.evidence, 1) as group:
            actor = await group.start(
                "control",
                [
                    sys.executable,
                    "-c",
                    "import sys;print(sys.stdin.readline().strip())",
                ],
                input_pipe=True,
                observe=output.extend,
            )
            self.assertIsNotNone(actor.process.stdin)
            assert actor.process.stdin is not None
            actor.process.stdin.write(b"checkpoint 17\n")
            await actor.process.stdin.drain()
            await asyncio.wait_for(actor.process.wait(), 2)
            await actor.capture
            self.assertEqual(actor.process.returncode, 0)
        self.assertEqual(output, b"checkpoint 17\n")

    async def test_observer_failure_still_drains_stdout_and_other_children(
        self,
    ) -> None:
        def reject(_payload: bytes) -> None:
            raise RuntimeError("injected observer failure")

        group = Actors(self.evidence, 1)
        with self.assertRaises(ExceptionGroup) as caught:
            async with group:
                peer = await group.start(
                    "peer", [sys.executable, "-c", "import time;time.sleep(60)"]
                )
                writer = await group.start(
                    "writer",
                    [sys.executable, "-c", "import os;os.write(1,b'x'*500000)"],
                    observe=reject,
                )
                await asyncio.wait_for(writer.process.wait(), 2)
                self.assertEqual(writer.process.returncode, 0)
        self.assertIn("observer failure", str(caught.exception.exceptions[0]))
        self.assertIsNotNone(peer.process.returncode)
        self.assertEqual(group.children, [])

    async def test_evidence_is_bounded(self) -> None:
        for _ in range(100):
            self.evidence.record("actor_output", data="x" * 1000)
        self.assertLessEqual(self.evidence.used, 4096)
        self.assertGreater(self.evidence.dropped, 0)
        self.assertLessEqual((self.directory / "events.jsonl").stat().st_size, 4096)

    async def test_scenario_rejects_bool_and_unknown_fields(self) -> None:
        data: dict[str, object] = {
            "name": "smoke",
            "records": 1000,
            "producers": 16,
            "subscribers": 0,
            "durable_agents": 2,
            "duration": 60,
        }
        path = self.directory / "scenario.json"
        path.write_text(json.dumps(data))
        self.assertEqual(Scenario.load(path).records, 1000)
        data["records"] = True
        path.write_text(json.dumps(data))
        with self.assertRaises(ValueError):
            Scenario.load(path)
        data["records"] = 1000
        data["unknown"] = 1
        path.write_text(json.dumps(data))
        with self.assertRaises(ValueError):
            Scenario.load(path)


if __name__ == "__main__":
    unittest.main()
