from __future__ import annotations

import asyncio
import copy
import io
import json
import random
import ssl
import struct
import tempfile
import unittest
from pathlib import Path
from typing import Any
from unittest.mock import patch

from authority import CONTEXT, Authority, credential
from mixed import MAXIMUM_GENERATION, UNSEEDED, MixedWriters, definition
from runtime import Actors, Evidence
from writers import Writers


class ModelAuthority:
    def __init__(self) -> None:
        self.leader_index = 0
        self.revision = 0
        self.namespaces: dict[str, int] = {}
        self.knobs: dict[str, Any] = {}
        self.revisions: dict[str, int] = {}
        self.receipts: dict[tuple[int, int], dict[str, Any]] = {}
        self.commands: list[tuple[int, str, dict[str, Any]]] = []
        self.capacity = False
        self.unknown = False
        self.delays: dict[int, asyncio.Event] = {}
        self.committed = asyncio.Event()
        self.corrupt: str | None = None

    async def leader(self) -> int:
        return self.leader_index

    async def request(
        self,
        index: int,
        path: str,
        fields: dict[str, Any] | None = None,
        producer: int = 0,
        *,
        admission_deadline: float | None = None,
    ) -> tuple[int, dict[str, Any]]:
        if fields is None:
            raise ValueError("model authority requires request fields")
        self.commands.append((producer, path, copy.deepcopy(fields)))
        if path == "/v2/open":
            return 200, {"generation": 1}
        if path == "/v2/read":
            keys = fields["keys"]
            knobs = {
                key: copy.deepcopy(self.knobs[key]) for key in keys if key in self.knobs
            }
            if self.corrupt in knobs:
                knobs[self.corrupt]["default"] = 1
            return 200, {
                "authority": CONTEXT,
                "schema_version": 1,
                "version": self.revision,
                "namespace_revision": self.namespaces.get(fields["namespace"], 0),
                "key_revisions": {
                    key: self.revisions[key] for key in keys if key in self.knobs
                },
                "knobs": knobs,
            }
        sequence = int(fields["sequence"])
        identity = (producer, sequence)
        if path == "/v2/result":
            receipt = self.receipts[identity]
        elif path == "/v2/transactions":
            if identity in self.receipts:
                receipt = self.receipts[identity]
            else:
                if sequence != sum(owner == producer for owner, _ in self.receipts) + 1:
                    raise RuntimeError("model producer sequence gap")
                operations = fields["operations"]
                if (
                    not operations
                    or len(operations) > 128
                    or len({op["key"] for op in operations}) != len(operations)
                ):
                    raise RuntimeError("model command admission exceeded")
                namespace = fields["namespace"]
                watermark = self.namespaces.get(namespace, 0)
                conflict = (
                    "expected_namespace" in fields
                    and fields["expected_namespace"] != watermark
                )
                for op in operations:
                    if "expected_revision" in op:
                        expected = op["expected_revision"]
                        if expected == 0:
                            conflict |= (
                                op["key"] in self.knobs
                                or op["absent_namespace_revision"] != watermark
                            )
                        else:
                            conflict |= self.revisions.get(op["key"]) != expected
                changed = any(
                    self.knobs.get(op["key"]) != op["value"] for op in operations
                )
                status = (
                    "conflict"
                    if conflict
                    else "capacity"
                    if self.capacity
                    else "applied"
                    if changed
                    else "no_change"
                )
                self.capacity = False
                if status == "applied":
                    self.revision += 1
                    self.namespaces[namespace] = self.revision
                    for op in operations:
                        key, value = op["key"], op["value"]
                        if value is None:
                            self.knobs.pop(key, None)
                            self.revisions.pop(key, None)
                        elif self.knobs.get(key) != value:
                            self.knobs[key] = copy.deepcopy(value)
                            self.revisions[key] = self.revision
                receipt = {
                    "status": status,
                    "generation": 1,
                    "sequence": sequence,
                    "revision": self.revision,
                    "namespace_revision": self.namespaces.get(namespace, 0),
                }
                self.receipts[identity] = receipt
                self.committed.set()
                if self.unknown:
                    self.unknown = False
                    raise OSError("reply lost after accepted commit")
                if producer in self.delays:
                    await self.delays[producer].wait()
        else:
            raise ValueError("unknown model authority route")
        status = receipt.get("status")
        if not isinstance(status, str):
            raise TypeError("model receipt status")
        code = {"conflict": 409, "capacity": 429}.get(status, 200)
        return code, copy.deepcopy(receipt)


class MixedTests(unittest.IsolatedAsyncioTestCase):
    async def test_commit_revision_orders_overlapping_late_acknowledgments(
        self,
    ) -> None:
        authority = ModelAuthority()
        writers = MixedWriters(authority, 2, 2, 17, shared_namespace="shared")
        await writers.phase("prepare")
        authority.committed.clear()
        release = asyncio.Event()
        authority.delays[0] = release
        earlier = asyncio.create_task(writers.submit(0, [(0, 42)]))
        await authority.committed.wait()
        with self.assertRaisesRegex(RuntimeError, "outstanding"):
            await writers.submit(0, [(0, 43)])
        later = await writers.submit(1, [(0, 84)])
        release.set()
        await earlier
        self.assertEqual(writers.values[0], 84)
        self.assertEqual(writers.key_revisions[0], later["revision"])
        self.assertEqual(writers.counts["late_oracle_observations"], 1)
        await writers.phase("verify")

    async def test_late_no_change_cannot_resurrect_previous_definition(self) -> None:
        authority = ModelAuthority()
        writers = MixedWriters(authority, 2, 2, 17, shared_namespace="shared")
        await writers.phase("prepare")
        authority.committed.clear()
        release = asyncio.Event()
        authority.delays[0] = release
        earlier = asyncio.create_task(writers.submit(0, [(0, 0)]))
        await authority.committed.wait()
        await writers.submit(1, [(0, 17)])
        release.set()
        receipt = await earlier
        self.assertEqual(receipt["status"], "no_change")
        self.assertEqual(writers.values[0], 17)
        await writers.phase("verify")

    async def test_terminal_conflict_capacity_and_exact_unknown_retry(self) -> None:
        authority = ModelAuthority()
        writers = MixedWriters(authority, 1, 1, 17)
        await writers.phase("prepare")
        authority.unknown = True
        receipt = await writers.submit(0, [(0, 8)], force_conflict=True)
        self.assertEqual(receipt["status"], "conflict")
        self.assertEqual(authority.commands[-1], authority.commands[-2])
        self.assertEqual(writers.values[0], 0)
        self.assertEqual(writers.sequences, [2])
        authority.capacity = True
        receipt = await writers.submit(0, [(0, 9)])
        self.assertEqual(receipt["status"], "capacity")
        self.assertEqual(writers.values[0], 0)
        await writers.phase("verify")
        authority.unknown = True
        receipt = await writers.submit(0, [(0, 10)])
        self.assertEqual(receipt["sequence"], 4)
        self.assertEqual(authority.commands[-1], authority.commands[-2])
        self.assertEqual(writers.values[0], 10)
        self.assertEqual(writers.counts["terminal_cas_conflicts"], 1)
        self.assertEqual(writers.counts["terminal_capacity_rejections"], 1)
        self.assertEqual(writers.counts["transport_unknown"], 2)
        await writers.phase("verify")

    async def test_thousand_operation_workload_uses_truthful_bounded_commands(
        self,
    ) -> None:
        authority = ModelAuthority()
        writers = MixedWriters(authority, 1200, 1, 17)
        await writers.phase("prepare")
        before = len(authority.commands)
        await writers.workload(0, 1000, "upsert", random.Random(17))
        commands = [
            fields
            for _, path, fields in authority.commands[before:]
            if path == "/v2/transactions"
        ]
        self.assertEqual(
            [len(fields["operations"]) for fields in commands], [128] * 7 + [104]
        )
        self.assertEqual(sum(len(fields["operations"]) for fields in commands), 1000)
        self.assertEqual(writers.counts["requested_workloads_1000"], 1)
        self.assertEqual(writers.counts["atomic_subbatches"], 8)
        await writers.phase("verify")

    async def test_deletion_reinsertion_repeat_and_real_cas_witness(self) -> None:
        authority = ModelAuthority()
        writers = MixedWriters(authority, 32, 2, 17, shared_namespace="shared")
        await writers.phase("prepare")
        rng = random.Random(17)
        await writers.workload(0, 10, "delete", rng)
        deleted = [ordinal for ordinal, value in enumerate(writers.values) if value < 0]
        self.assertEqual(len(deleted), 10)
        await writers.phase("verify")
        await writers.workload(0, 10, "reinsert", rng)
        self.assertTrue(all(writers.values[ordinal] >= 0 for ordinal in deleted))
        await writers.workload(0, 1, "repeat", rng)
        await writers.workload(0, 1, "noop", rng)
        await writers.workload(0, 1, "cas", rng)
        self.assertGreaterEqual(writers.counts["no_change"], 2)
        commands = [
            fields
            for _, path, fields in authority.commands
            if path == "/v2/transactions"
        ]
        self.assertIn("expected_revision", commands[-1]["operations"][0])
        await writers.phase("verify")

    async def test_full_definition_comparison_distinguishes_bool_and_int_scalars(
        self,
    ) -> None:
        authority = ModelAuthority()
        writers = MixedWriters(authority, 16, 2, 17)
        await writers.phase("prepare")
        authority.corrupt = "load0:key2"
        self.assertIs(authority.knobs[authority.corrupt]["default"], True)
        with self.assertRaisesRegex(RuntimeError, "definition oracle"):
            await writers.verify(0)

    async def test_no_cross_namespace_or_oversized_admission(self) -> None:
        authority = ModelAuthority()
        writers = MixedWriters(authority, 130, 2, 17)
        with self.assertRaisesRegex(ValueError, "namespace"):
            await writers.submit(0, [(1, 0)])
        with self.assertRaisesRegex(ValueError, "bound"):
            await writers.submit(0, [(0, 0)] * 2)
        with self.assertRaisesRegex(ValueError, "bound"):
            await writers.submit(0, [(ordinal, 0) for ordinal in range(129)])
        self.assertFalse(authority.commands)

    async def test_compact_expected_file_keeps_legacy_oracle_unchanged(self) -> None:
        authority = ModelAuthority()
        legacy = Writers(authority, 2, 1, 17)
        legacy.values[1] = -7
        output = io.BytesIO()
        legacy.write_expected(output, 12)
        self.assertEqual(output.getvalue(), struct.pack("<QQQqq", 2, 1, 12, 0, -7))
        writers = MixedWriters(authority, 2, 1, 17, shared_namespace="shared")
        await writers.phase("prepare")
        await writers.submit(0, [(1, -18)])
        output = io.BytesIO()
        writers.write_expected(output, authority.revision)
        expected = b"DTMIXED1" + struct.pack("<QQQQ", 2, 1, authority.revision, 6)
        expected += b"shared" + struct.pack("<qq", 0, -18)
        self.assertEqual(output.getvalue(), expected)
        unseeded = MixedWriters(authority, 1, 1, 17)
        with self.assertRaises(ValueError):
            unseeded.write_expected(io.BytesIO(), 0)


class MixedConfigurationTests(unittest.TestCase):
    def test_generation_vectors_and_tombstone_bounds(self) -> None:
        self.assertEqual(
            definition(0, 0),
            {
                "type": "bool",
                "default": False,
                "rollout_pct": None,
                "overrides": {
                    "force-off": False,
                    "force-on": True,
                    "opaque\0group": True,
                    "ramp": {"rollout_pct": 0},
                },
            },
        )
        self.assertEqual(definition(1, 0), {"type": "int", "default": 17})
        self.assertEqual(definition(0, 3), {"type": "int", "default": -314187})
        self.assertIsNone(definition(2, -1))
        self.assertIsNone(definition(2, -MAXIMUM_GENERATION - 1))
        for invalid in (UNSEEDED, MAXIMUM_GENERATION + 1):
            with self.assertRaises(ValueError):
                definition(0, invalid)

    def test_shared_scope_credentials_remain_producer_specific(self) -> None:
        scratch = Path.cwd() / ".scratch"
        scratch.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=scratch) as temporary:
            directory = Path(temporary)
            for name in ("one", "two", "three"):
                (directory / f"{name}.pem").write_text(
                    "-----BEGIN CERTIFICATE-----\nYQ==\n-----END CERTIFICATE-----\n"
                )
            evidence = Evidence(directory / "evidence.jsonl", 4096)
            try:
                with patch(
                    "authority.ssl.create_default_context",
                    return_value=ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT),
                ):
                    authority = Authority(
                        Path("detentd"),
                        directory,
                        directory,
                        Actors(evidence, 1),
                        3,
                        shared_namespace="hot",
                    )
                try:
                    self.assertEqual(authority.namespaces, ("hot",))
                    config = json.loads((directory / "authority-0.json").read_text())
                    publishers = config["credentials"][:3]
                    self.assertEqual(
                        [entry["producer"] for entry in publishers],
                        ["load0", "load1", "load2"],
                    )
                    self.assertEqual(
                        [entry["namespaces"] for entry in publishers], [["hot"]] * 3
                    )
                    self.assertEqual(config["credentials"][-1]["namespaces"], ["hot"])
                    self.assertEqual(len({entry["token"] for entry in publishers}), 3)
                    self.assertEqual(credential(2)["namespaces"], ["load2"])
                finally:
                    authority.release_ports()
            finally:
                evidence.close()

    def test_invalid_shared_namespace_refuses_before_network_admission(self) -> None:
        for namespace in ("", "bad:name", "é", "x" * 199):
            with self.assertRaisesRegex(ValueError, "namespace"):
                MixedWriters(ModelAuthority(), 1, 1, 17, shared_namespace=namespace)


if __name__ == "__main__":
    unittest.main()
