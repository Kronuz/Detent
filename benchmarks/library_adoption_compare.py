"""Compare frozen macOS benchmark arms in alternating order."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path

from library_adoption_measure import workloads


def linkage(binary: Path) -> tuple[str, ...]:
    return tuple(
        subprocess.check_output(["otool", "-L", str(binary)], text=True).splitlines()[
            1:
        ]
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--before", required=True, type=Path)
    parser.add_argument("--after", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--samples", type=int)
    parser.add_argument("--case", action="append", dest="selected_cases")
    args = parser.parse_args()
    before: Path = args.before.resolve()
    after: Path = args.after.resolve()
    output: Path = args.output.resolve()
    count: int | None = args.samples
    if count is not None and count < 1:
        raise ValueError("sample count must be positive")
    cases = workloads()
    selected: list[str] | None = args.selected_cases
    if selected is not None:
        unknown = set(selected) - {case.name for case in cases}
        if unknown:
            raise ValueError(f"unknown workloads: {sorted(unknown)}")
        cases = [case for case in cases if case.name in selected]
    output.mkdir(parents=True, exist_ok=False)
    skipped: list[str] = []
    runner = Path(__file__).with_name("library_adoption_measure.py")
    with (output / "order.jsonl").open("x") as records:
        for case in cases:
            old = before / "bin" / case.program
            new = after / "bin" / case.program
            if hashlib.sha256(old.read_bytes()).digest() == hashlib.sha256(
                new.read_bytes()
            ).digest() and linkage(old) == linkage(new):
                skipped.append(case.name)
                continue
            for sample in range(count if count is not None else case.samples):
                arms = (("before", before), ("after", after))
                if sample % 2:
                    arms = arms[::-1]
                for label, arm in arms:
                    destination = output / f"{case.name}-{sample}-{label}"
                    command = [
                        sys.executable,
                        str(runner),
                        "--arm",
                        str(arm),
                        "--output",
                        str(destination),
                        "--samples",
                        "1",
                        "--case",
                        case.name,
                    ]
                    records.write(
                        json.dumps(
                            {
                                "case": case.name,
                                "pair": sample,
                                "arm": label,
                                "command": command,
                            }
                        )
                        + "\n"
                    )
                    records.flush()
                    subprocess.run(command, check=True)
    (output / "identical-skipped.json").write_text(json.dumps(skipped, indent=2) + "\n")


if __name__ == "__main__":
    main()
