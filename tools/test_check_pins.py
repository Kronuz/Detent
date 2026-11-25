"""Tests for check_pins.parse_declares and repo_key."""

from __future__ import annotations

import unittest

from check_pins import parse_declares, repo_key


class ParseTest(unittest.TestCase):
    def test_declares_resolve_variables_and_skip_comments(self) -> None:
        text = (
            "set(REV abc)\n"
            "FetchContent_Declare(a GIT_REPOSITORY https://github.com/Kronuz/a GIT_TAG ${REV})\n"
            "# FetchContent_Declare(b GIT_REPOSITORY https://x/b.git GIT_TAG 1)\n"
            'FetchContent_Declare(c\n  GIT_REPOSITORY "https://x/c"\n  GIT_TAG v1)\n'
            "FetchContent_Declare(d URL https://x/d.zip)\n"
        )
        got = [(d.name, d.repository, d.tag) for d in parse_declares("lib", "CMakeLists.txt", text)]
        self.assertEqual(
            got, [("a", "https://github.com/Kronuz/a", "abc"), ("c", "https://x/c", "v1")]
        )

    def test_repo_key_normalizes(self) -> None:
        self.assertEqual(repo_key("https://github.com/Kronuz/A.git"), "github.com/kronuz/a")
        self.assertEqual(repo_key("git@github.com:Kronuz/a"), "github.com/kronuz/a")


if __name__ == "__main__":
    unittest.main()
