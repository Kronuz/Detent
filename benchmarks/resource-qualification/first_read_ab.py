"""Fresh-process resident-data first reads: A, B and A again (the A/A control), per key and round.

Each measured process gets its own warmup process just before it. The arm order rotates with the
key and round so no arm is always first. Output is one `unit` file per arm and round, for
`cpp-libraries/tools/ab_compare.py compare`, plus raw.json with every sample.
"""

from __future__ import annotations

import argparse
import json
import os
import statistics
import subprocess
from itertools import permutations
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
METRICS = {
    "first_live_ns": "first",
    "attach_ns": "attach",
    "lookup_ns": "lookup",
}
ARMS = ("a", "b", "aa")


def run(
    binary: Path, directory: Path, key: str, expected: int, env: dict[str, str]
) -> dict[str, float]:
    result = subprocess.run(
        [str(binary), "--read", str(directory), key, str(expected)],
        check=True,
        capture_output=True,
        text=True,
        env=env,
        timeout=30,
    )
    value: dict[str, float] = json.loads(result.stdout)
    if value["records"] != 5_000_000 or value["major_faults"]:
        raise RuntimeError(f"unexpected sample {value}")
    return value


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--a", type=Path, required=True)
    parser.add_argument("--b", type=Path, required=True)
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--rounds", type=int, default=10)
    parser.add_argument("--keys", type=int, default=40)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    manifest = json.loads(
        (REPO / "docs/measurements/prepared-managed-5m-20261026.json").read_text()
    )
    cases = [(str(row["key"]), int(row["expected_int"])) for row in manifest["cases"]][
        : args.keys
    ]
    env = {
        k: v
        for k, v in os.environ.items()
        if not k.startswith(("DETENT_", "DYLD_")) and k != "LD_PRELOAD"
    }
    env["LC_ALL"] = "C"
    binary = {"a": args.a, "b": args.b, "aa": args.a}
    orders = list(permutations(ARMS))
    samples: list[dict[str, object]] = []
    for arm in ARMS:
        (args.output / arm).mkdir()
    for round_index in range(args.rounds):
        rows: dict[str, list[str]] = {arm: [] for arm in ARMS}
        pooled: dict[str, dict[str, list[float]]] = {
            arm: {name: [] for name in (*METRICS.values(), "cpu")} for arm in ARMS
        }
        for index, (key, expected) in enumerate(cases):
            for arm in orders[(round_index + index) % len(orders)]:
                run(binary[arm], args.directory, key, expected, env)
                sample = run(binary[arm], args.directory, key, expected, env)
                samples.append(
                    dict(
                        sample,
                        arm=arm,
                        round=round_index,
                        key=key,
                        expected_int=expected,
                    )
                )
                for field, name in METRICS.items():
                    pooled[arm][name].append(sample[field] / 1e3)
                pooled[arm]["cpu"].append(sample["process_cpu_us"])
                rows[arm].append(f"{key} first {sample['first_live_ns'] / 1e3:.3f} us")
        for arm in ARMS:
            lines = [
                f"all {name} {statistics.median(values):.3f} us"
                for name, values in pooled[arm].items()
            ]
            (args.output / arm / f"round-{round_index:03d}.txt").write_text(
                "\n".join(lines + rows[arm]) + "\n"
            )
    below = {
        arm: sum(
            1
            for s in samples
            if s["arm"] == arm and float(str(s["first_live_ns"])) < 1e6
        )
        for arm in ARMS
    }
    total = {arm: sum(1 for s in samples if s["arm"] == arm) for arm in ARMS}
    (args.output / "raw.json").write_text(
        json.dumps(
            {
                "binaries": {k: str(v) for k, v in binary.items()},
                "below_1ms": below,
                "samples_per_arm": total,
                "samples": samples,
            },
            indent=1,
        )
        + "\n"
    )
    print(json.dumps({"below_1ms": below, "samples_per_arm": total}))


if __name__ == "__main__":
    main()
