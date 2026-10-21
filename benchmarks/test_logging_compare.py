"""Regression checks for measurement interference detection."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import logging_compare


class InterferenceTests(unittest.TestCase):
    def test_rejects_competing_jobs_and_preserves_snapshot(self) -> None:
        scratch = Path(__file__).resolve().parents[1] / ".scratch"
        scratch.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=scratch) as folder:
            destination = Path(folder) / "processes.txt"
            for executable in (
                "./format14_test",
                "/usr/bin/clang++",
                "./test_publication_atomic",
                "./mutation_bench",
            ):
                snapshot = f"PID PPID %CPU COMM\n44417 44411 100.0 {executable}\n"
                with (
                    self.subTest(executable=executable),
                    patch.object(
                        logging_compare.subprocess,
                        "check_output",
                        return_value=snapshot,
                    ),
                    self.assertRaisesRegex(RuntimeError, "timings rejected"),
                ):
                    logging_compare.check_interference(destination)
                self.assertEqual(destination.read_text(), snapshot)

    def test_accepts_snapshot_without_competing_jobs(self) -> None:
        scratch = Path(__file__).resolve().parents[1] / ".scratch"
        scratch.mkdir(exist_ok=True)
        snapshot = "PID PPID %CPU COMM\n1 0 0.0 /sbin/launchd\n22 1 0.0 ps\n"
        with tempfile.TemporaryDirectory(dir=scratch) as folder:
            destination = Path(folder) / "processes.txt"
            with patch.object(
                logging_compare.subprocess, "check_output", return_value=snapshot
            ):
                logging_compare.check_interference(destination)
            self.assertEqual(destination.read_text(), snapshot)


if __name__ == "__main__":
    unittest.main()
