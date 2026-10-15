from __future__ import annotations

import sys
import unittest

from evidence import command


class EvidenceTests(unittest.TestCase):
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
