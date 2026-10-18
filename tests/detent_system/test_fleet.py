from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from fleet import FleetOutput, admission, load_result, parse_cpu_time


class FleetTests(unittest.TestCase):
    def test_process_cpu_time_formats(self) -> None:
        for text, expected in (
            ("0:00.02", 0.02),
            ("61:02.50", 3662.5),
            ("01:02:03", 3723.0),
            ("2-03:04:05", 183845.0),
        ):
            with self.subTest(text=text):
                self.assertEqual(parse_cpu_time(text), expected)
        for text in ("1", "-1:00", "1:60", "1:60:00", "1:nan", "1:inf"):
            with self.subTest(text=text), self.assertRaises(ValueError):
                parse_cpu_time(text)

    def test_fragmented_events_and_exact_counter_types(self) -> None:
        output = FleetOutput()
        for byte in b'{"event":"ready","sessions":500,"warm":false}\n':
            output.feed(bytes([byte]))
        self.assertTrue(output.ready.is_set())
        output.feed(b'{"event":"checkpoint","revision":42}\n')
        self.assertEqual(output.checkpoint_revision, 42)
        for payload in (
            b'{"event":"ready","sessions":true}\n',
            b'{"event":"sample","offers":-1}\n',
            b'{"event":"checkpoint","revision":0}\n',
            b'{"event":"unexpected"}\n',
        ):
            broken = FleetOutput()
            with self.assertRaises(ValueError):
                broken.feed(payload)
            self.assertTrue(broken.ready.is_set())
            self.assertIsNotNone(broken.failure)

    def test_expected_fault_diagnostic_remains_bounded(self) -> None:
        output = FleetOutput(allow_failure=True)
        output.feed(b"fleet: injected failure\n")
        self.assertEqual(output.diagnostic, "fleet: injected failure")
        self.assertIsNone(output.failure)
        with self.assertRaises(ValueError):
            output.feed(b"fleet: duplicate failure\n")
        with self.assertRaisesRegex(ValueError, "4 KiB"):
            FleetOutput(allow_failure=True).feed(b"fleet: " + b"x" * 4096)

    def test_measurement_acknowledgments_require_ordered_transitions(self) -> None:
        output = FleetOutput()
        output.feed(b'{"event":"measurement","running":1}\n')
        self.assertTrue(output.measurement_started.is_set())
        output.feed(b'{"event":"measurement","running":0}\n')
        self.assertTrue(output.measurement_stopped.is_set())
        for events in (
            [0],
            [1, 1],
            [1, 0, 0],
            [2],
            [True],
        ):
            output = FleetOutput()
            with self.assertRaises(ValueError):
                for running in events:
                    output.feed(
                        json.dumps(
                            {"event": "measurement", "running": running}
                        ).encode()
                        + b"\n"
                    )

    def test_output_bound_applies_before_and_after_newline(self) -> None:
        for suffix in (b"", b"\n"):
            with self.assertRaisesRegex(ValueError, "4 KiB"):
                FleetOutput().feed(b" " * 4097 + suffix)

    def test_admission_refuses_instead_of_shrinking_population(self) -> None:
        with (
            patch("fleet.physical_memory", return_value=32 * 1024**3),
            patch("fleet.available_memory", return_value=24 * 1024**3),
        ):
            admitted = admission(10000, 128, 16)
            self.assertLess(admitted["required_workload_bytes"], 24 * 1024**3)
            wide = admission(10000, 128, 256)
            self.assertGreater(
                wide["probe_heap_bytes"] - admitted["probe_heap_bytes"],
                2 * 1024**3,
            )
            with self.assertRaisesRegex(ValueError, "refused"):
                admission(10000, 10000, 4096)
        with (
            patch("fleet.physical_memory", return_value=32 * 1024**3),
            patch("fleet.available_memory", return_value=1),
            self.assertRaisesRegex(ValueError, "refused"),
        ):
            admission(500, 128, 16)

    def test_result_requires_exact_conservation(self) -> None:
        result = dict.fromkeys(
            ("transport_probes", "accepted", "completed", "opened", "ready", "checked"),
            500,
        )
        result.update(
            success=True,
            durable_stores=0,
            checkpoint_revision=42,
            premature_ready_polls=0,
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "result.json"
            path.write_text(json.dumps(result))
            self.assertEqual(load_result(path, 500, 42), result)
            for key, value in (("completed", 499), ("durable_stores", False)):
                broken = result | {key: value}
                path.write_text(json.dumps(broken))
                with self.assertRaisesRegex(ValueError, "counter mismatch"):
                    load_result(path, 500, 42)
