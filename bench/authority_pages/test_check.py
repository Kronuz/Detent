"""Tests check.py's interval and its paced checks: python3 -I bench/authority_pages/test_check.py."""

from __future__ import annotations

import importlib.util
import io
import json
import pathlib
import unittest
from contextlib import redirect_stdout

_spec = importlib.util.spec_from_file_location(
    "check", pathlib.Path(__file__).with_name("check.py")
)
assert _spec is not None and _spec.loader is not None
check = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(check)


def block(
    rnd: int, slot: int, arm: str, p50: float, seals: int = 5
) -> dict[str, object]:
    builds = 0 if arm in ("none", "aa") else 10 * seals
    return {
        "step": "block",
        "round": rnd,
        "slot": slot,
        "arm": arm,
        "group_ms_p50": p50,
        "run_ms": 5000.0 * seals / 4.9,
        "tx_per_s": 470.0,
        "seals": seals,
        "builds": builds,
        "pages_written": builds,
    }


def rounds(
    others: list[str], count: int, p50: dict[str, float]
) -> list[dict[str, object]]:
    out = []
    for rnd in range(count):
        for slot in range(2 * len(others)):
            arm = "none" if slot % 2 == 0 else others[(slot // 2 + rnd) % len(others)]
            out.append(block(rnd, slot, arm, p50.get(arm, 100.0) + rnd))
    return out


class Interval(unittest.TestCase):
    def test_t_quantile_matches_the_table(self) -> None:
        self.assertEqual(check.t975(3), 3.182)
        self.assertEqual(check.t975(30), 2.042)
        self.assertAlmostEqual(check.t975(40), 2.021, places=3)
        self.assertAlmostEqual(check.t975(120), 1.980, places=3)

    def test_four_pairs_use_t_with_three_degrees_of_freedom(self) -> None:
        # mean 2.5, stdev 1.2910: half-width 3.182 * 1.2910 / 2 = 2.054 (z would give 1.265).
        self.assertEqual(check.stats([1.0, 2.0, 3.0, 4.0])["ci95"], [0.4, 4.6])

    def test_one_pair_has_no_interval(self) -> None:
        self.assertIsNone(check.stats([1.0])["ci95"])


class Paced(unittest.TestCase):
    def test_five_arms_rotate_and_pair(self) -> None:
        others = ["thread", "pool", "write", "pass4", "aa"]
        blocks = rounds(others, 4, {"write": 140.0})
        out = io.StringIO()
        with redirect_stdout(out):
            check.check_paced(blocks, 0.98)
        summary = json.loads(out.getvalue())
        self.assertEqual(summary["blocks"], 40)
        self.assertEqual(summary["pairs"]["write"]["median"], 40.0)
        self.assertEqual(summary["pairs"]["pass4"]["ci95"], [0.0, 0.0])

    def test_a_misplaced_arm_fails(self) -> None:
        blocks = rounds(["thread", "pool", "write", "pass4", "aa"], 2, {})
        blocks[13]["arm"], blocks[15]["arm"] = blocks[15]["arm"], blocks[13]["arm"]
        with self.assertRaises(SystemExit), redirect_stdout(io.StringIO()):
            check.check_paced(blocks, 0.98)

    def test_a_slow_posting_rate_fails(self) -> None:
        blocks = rounds(["thread", "aa"], 2, {})
        for b in blocks:
            b["run_ms"] = 2 * float(b["run_ms"])
        with self.assertRaises(SystemExit), redirect_stdout(io.StringIO()):
            check.check_paced(blocks, 0.98)


if __name__ == "__main__":
    unittest.main()
