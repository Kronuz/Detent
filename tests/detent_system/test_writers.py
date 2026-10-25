from __future__ import annotations

import copy
import tempfile
import unittest
from collections import deque
from collections.abc import Sequence
from pathlib import Path
from typing import Any
from unittest.mock import patch

from authority import Authority
from http_activity import GeneratorAdmissionRefused
from run_writers import drained
from runtime import Evidence
from writers import Writers


class Responses:
    def __init__(
        self, replies: Sequence[tuple[int, dict[str, Any]] | Exception]
    ) -> None:
        self.leader_index = 0
        self.replies = deque(replies)
        self.commands: list[dict[str, Any] | None] = []

    async def request(
        self,
        index: int,
        path: str,
        fields: dict[str, Any] | None = None,
        producer: int = 0,
        *,
        admission_deadline: float | None = None,
    ) -> tuple[int, dict[str, Any]]:
        self.commands.append(copy.deepcopy(fields))
        reply = self.replies.popleft()
        if isinstance(reply, Exception):
            raise reply
        return reply

    async def leader(self) -> int:
        return self.leader_index


class WriterTests(unittest.IsolatedAsyncioTestCase):
    async def test_teardown_waits_for_restarted_follower(self) -> None:
        authority = object.__new__(Authority)
        authority.leader_index = 0
        follower_attempts = 0

        async def health(index: int, path: str) -> tuple[int, dict[str, int | bool]]:
            nonlocal follower_attempts
            self.assertEqual(path, "/v1/health")
            if index == 2:
                follower_attempts += 1
                if follower_attempts == 1:
                    raise ConnectionRefusedError("follower is recovering")
            return 200, {
                "ready": True,
                "fenced": False,
                "requests": 0,
                "reserved_body_bytes": 0,
                "reserved_reply_bytes": 0,
                "connections_active": 0,
                "retired_log_bytes": 0,
                "retired_log_entries": 0,
            }

        scratch = Path.cwd() / ".scratch"
        scratch.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=scratch) as directory:
            evidence = Evidence(Path(directory) / "events.jsonl", 4096)
            try:
                with patch.object(authority, "request", side_effect=health):
                    await drained(authority, evidence)
            finally:
                evidence.close()
        self.assertGreaterEqual(follower_attempts, 2)

    async def test_capacity_is_terminal_and_advances_sequence(self) -> None:
        capacity = {"status": "capacity", "generation": 1, "sequence": 1}
        applied = {"status": "applied", "generation": 1, "sequence": 2}
        authority = Responses([(429, capacity), (200, applied)])
        writers = Writers(authority, 1, 1, 17)
        self.assertFalse(await writers.transaction(0, []))
        self.assertTrue(await writers.transaction(0, []))
        self.assertEqual(writers.sequences, [2])
        self.assertEqual(writers.counts["terminal_capacity_rejections"], 1)
        self.assertEqual(writers.counts["acknowledged"], 1)
        self.assertEqual(writers.values[0], 0)

    async def test_generator_refusal_preserves_sequence_and_unknown_state(self) -> None:
        for replies in (
            [GeneratorAdmissionRefused("unsent")],
            [OSError("ACK lost"), GeneratorAdmissionRefused("retry unsent")],
        ):
            authority = Responses(replies)
            writers = Writers(authority, 1, 1, 17)
            with self.assertRaises(GeneratorAdmissionRefused):
                await writers.transaction(0, [])
            self.assertEqual(writers.sequences, [0])
            self.assertEqual(writers.receipts, [None])
            self.assertEqual(writers.values[0], 0)
            self.assertEqual(writers.counts["generator_http_admission_refusals"], 1)
            if len(authority.commands) == 2:
                self.assertEqual(authority.commands[0], authority.commands[1])
                self.assertEqual(writers.counts["unresolved_at_generator_deadline"], 1)
                self.assertEqual(writers.counts["transport_unknown"], 1)

    async def test_retry_preserves_exact_command(self) -> None:
        applied = {"status": "applied", "generation": 1, "sequence": 1}
        authority = Responses([(503, {"error": "outcome_unknown"}), (200, applied)])
        writers = Writers(authority, 1, 1, 17)
        self.assertTrue(
            await writers.transaction(
                0, [{"key": "load0:key0", "value": {"type": "int", "default": 7}}]
            )
        )
        self.assertEqual(authority.commands[0], authority.commands[1])
        self.assertEqual(writers.counts["logical_commands"], 1)
        self.assertEqual(writers.counts["offered_attempts"], 2)
        self.assertEqual(writers.sequences, [1])


if __name__ == "__main__":
    unittest.main()
