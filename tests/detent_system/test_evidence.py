from __future__ import annotations

import hashlib
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from evidence import archive_sha256, command, repository


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

    def test_archive_directory_symlinks_are_fingerprinted_without_following(
        self,
    ) -> None:
        scratch = Path.cwd() / ".scratch"
        scratch.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=scratch) as temporary:
            path = Path(temporary)
            source = path / "source"
            source.mkdir()
            target = path / "target"
            target.mkdir()
            (target / "external.cc").write_text("one")
            link = source / "include"
            link.symlink_to(target, target_is_directory=True)
            before = archive_sha256(source)
            (target / "external.cc").write_text("two")
            self.assertEqual(before, archive_sha256(source))
            other = path / "other-target"
            other.mkdir()
            link.unlink()
            link.symlink_to(other, target_is_directory=True)
            self.assertNotEqual(before, archive_sha256(source))

    def test_archive_rejects_fifo_without_opening(self) -> None:
        scratch = Path.cwd() / ".scratch"
        scratch.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=scratch) as temporary:
            path = Path(temporary)
            os.mkfifo(path / "pipe")
            with self.assertRaisesRegex(ValueError, "nonregular"):
                archive_sha256(path)

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
