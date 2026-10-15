from __future__ import annotations

import hashlib
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from evidence import command, repository


class EvidenceTests(unittest.TestCase):
    def test_nested_archive_does_not_claim_parent_revision(self) -> None:
        scratch = Path.cwd() / ".scratch"
        scratch.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=scratch) as temporary:
            path = Path(temporary)
            source = path / "source.cc"
            source.write_text("one")
            before = repository(path)
            self.assertEqual(before["revision"], "archive")
            source.write_text("two")
            after = repository(path)
            self.assertNotEqual(
                before["source_tree_sha256"], after["source_tree_sha256"]
            )

    def test_large_tracked_diff_retains_exact_fingerprint(self) -> None:
        scratch = Path.cwd() / ".scratch"
        scratch.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=scratch) as temporary:
            path = Path(temporary)
            subprocess.run(["git", "init", "-q", str(path)], check=True)
            source = path / "source.cc"
            source.write_text("before\n")
            subprocess.run(["git", "-C", str(path), "add", "source.cc"], check=True)
            subprocess.run(
                [
                    "git",
                    "-C",
                    str(path),
                    "-c",
                    "user.name=Test",
                    "-c",
                    "user.email=test@example.invalid",
                    "commit",
                    "-qm",
                    "Seed",
                ],
                check=True,
            )
            source.write_text("after\n" * 12000)
            patch = (
                subprocess.check_output(["git", "diff", "--binary", "HEAD"], cwd=path)
                .decode()
                .strip()
            )
            self.assertGreater(len(patch), 65536)
            self.assertEqual(
                repository(path)["tracked_changes_sha256"],
                hashlib.sha256(patch.encode()).hexdigest(),
            )

    def test_command_output_is_bounded(self) -> None:
        with self.assertRaisesRegex(ValueError, "64 KiB"):
            command([sys.executable, "-c", "import os;os.write(1,b'x'*100000)"])

    def test_command_failure_is_reported(self) -> None:
        with self.assertRaisesRegex(ValueError, "command failed"):
            command([sys.executable, "-c", "raise SystemExit(7)"])

    def test_command_captures_success(self) -> None:
        self.assertEqual(command([sys.executable, "-c", "print('hello')"]), "hello")


if __name__ == "__main__":
    unittest.main()
