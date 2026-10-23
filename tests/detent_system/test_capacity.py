"""Keep explicit capacity profiles bounded and preserve small-test defaults."""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path
from typing import Any

from capacity import CapacityProfile
from runtime import Scenario


class CapacityTests(unittest.TestCase):
    def setUp(self) -> None:
        parent = Path(__file__).resolve().parents[2] / ".scratch"
        parent.mkdir(exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(
            dir=parent, prefix="capacity-profile-"
        )
        self.addCleanup(self.temporary.cleanup)
        self.path = Path(self.temporary.name) / "profile.json"
        self.data: dict[str, Any] = json.loads(
            (Path(__file__).with_name("presets") / "capacity-1300000.json").read_text()
        )

    def write(self) -> None:
        self.path.write_text(json.dumps(self.data))

    def test_explicit_profile_reaches_native_participants(self) -> None:
        self.write()
        profile = CapacityProfile.load(self.path)
        self.assertEqual(profile.application["keys"], 1300000)
        self.assertEqual(profile.managed()["image_limits"], profile.image)
        self.assertEqual(profile.oracle_arguments()[0], "1300000")
        self.assertEqual(profile.journal, {})

    def test_booleans_and_unfunded_numeric_budgets_are_rejected(self) -> None:
        self.data["image"]["records"] = True
        self.write()
        with self.assertRaises(ValueError):
            CapacityProfile.load(self.path)
        self.data["image"]["records"] = 1300000
        self.data["heap_bytes"] = 0
        self.write()
        with self.assertRaises(ValueError):
            CapacityProfile.load(self.path)

    def test_duplicate_nested_fields_are_rejected(self) -> None:
        self.path.write_text('{"application":{"keys":1,"keys":2}}')
        with self.assertRaisesRegex(ValueError, "duplicate"):
            CapacityProfile.load(self.path)

    def test_scenario_default_requires_explicit_larger_envelope(self) -> None:
        self.path.write_text(
            json.dumps(
                {
                    "name": "capacity",
                    "records": 1300000,
                    "producers": 4,
                    "subscribers": 16,
                    "durable_agents": 2,
                    "duration": 3,
                }
            )
        )
        with self.assertRaises(ValueError):
            Scenario.load(self.path)
        self.assertEqual(Scenario.load(self.path, 1300000).records, 1300000)
        with self.assertRaises(ValueError):
            Scenario.load(self.path, 2**32)


if __name__ == "__main__":
    unittest.main()
