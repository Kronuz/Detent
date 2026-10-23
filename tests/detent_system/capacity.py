"""Explicit native capacity settings shared by the qualification participants."""

from __future__ import annotations

import json
from dataclasses import dataclass, field
from pathlib import Path


@dataclass(frozen=True)
class CapacityProfile:
    application: dict[str, int]
    delivery: dict[str, int]
    image: dict[str, int]
    heap_bytes: int
    mapping_bytes: int
    disk_bytes: int
    journal: dict[str, int] = field(default_factory=dict)

    @classmethod
    def load(cls, path: Path) -> CapacityProfile:
        if path.stat().st_size > 65536:
            raise ValueError("capacity profile exceeds 64 KiB")

        def unique(pairs: list[tuple[str, object]]) -> dict[str, object]:
            result: dict[str, object] = {}
            for key, value in pairs:
                if key in result:
                    raise ValueError(f"duplicate capacity field: {key}")
                result[key] = value
            return result

        value: object = json.loads(path.read_text(), object_pairs_hook=unique)
        required = {
            "application",
            "delivery",
            "image",
            "heap_bytes",
            "mapping_bytes",
            "disk_bytes",
        }
        if (
            not isinstance(value, dict)
            or not required <= value.keys()
            or value.keys() - required - {"journal"}
        ):
            raise ValueError("invalid capacity profile fields")
        tables: dict[str, dict[str, int]] = {}
        for key in ("application", "delivery", "image", "journal"):
            table = value.get(key, {})
            if not isinstance(table, dict) or len(table) > 16:
                raise ValueError(f"invalid capacity table: {key}")
            normalized: dict[str, int] = {}
            for name, number in table.items():
                if (
                    not isinstance(name, str)
                    or type(number) is not int
                    or not 0 <= number < 2**64
                ):
                    raise ValueError(f"invalid capacity integer: {key}")
                normalized[name] = number
            tables[key] = normalized
        budgets: dict[str, int] = {}
        for key in ("heap_bytes", "mapping_bytes", "disk_bytes"):
            number = value[key]
            if type(number) is not int or not 1 <= number < 2**63:
                raise ValueError(f"invalid capacity budget: {key}")
            budgets[key] = number
        if not tables["application"].get("keys") or not tables["image"].get("records"):
            raise ValueError(
                "capacity profile requires authority and image record bounds"
            )
        if (
            not 1 <= tables["application"]["keys"] < 2**32
            or not 1 <= tables["image"]["records"] < 2**32
        ):
            raise ValueError("capacity record bounds exceed the format")
        return cls(
            application=tables["application"],
            journal=tables["journal"],
            delivery=tables["delivery"],
            image=tables["image"],
            heap_bytes=budgets["heap_bytes"],
            mapping_bytes=budgets["mapping_bytes"],
            disk_bytes=budgets["disk_bytes"],
        )

    def managed(self) -> dict[str, object]:
        return {
            "image_limits": self.image,
            "heap_bytes": self.heap_bytes,
            "mapping_bytes": self.mapping_bytes,
            "disk_bytes": self.disk_bytes,
        }

    def oracle_arguments(self) -> list[str]:
        return [
            str(value)
            for value in (
                self.image["records"],
                self.image.get("image_bytes", 0),
                self.image.get("string_bytes", 0),
                self.image.get("pages", 0),
                self.image.get("segments", 0),
                self.heap_bytes,
                self.mapping_bytes,
            )
        ]
