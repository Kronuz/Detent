from __future__ import annotations

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
