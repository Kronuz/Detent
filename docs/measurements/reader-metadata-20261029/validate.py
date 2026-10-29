from __future__ import annotations

import json
import os
import shlex
import subprocess
from pathlib import Path
from typing import Any

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
CASES = (
    "test_local_reader_metadata",
    "test_local_lazy_page_access",
    "test_local_managed_reader",
    "test_local_client_source",
    "test_local_pinned_source",
    "test_local_trusted_paths",
)


def main() -> None:
    rows: list[dict[str, Any]] = []
    report: dict[str, Any] = {"status": "incomplete", "samples": rows}
    try:
        for arm, folder in (("before", "baseline-build"), ("after", "reader-build")):
            build = ROOT / folder
            commands: list[dict[str, str]] = json.loads(
                (build / "compile_commands.json").read_text()
            )
            output = HERE / arm / "validation"
            output.mkdir(exist_ok=True)
            for case in CASES:
                target = build / f"tests/detent_local/CMakeFiles/{case}.dir"
                objects: dict[Path, Path] = {}
                selected = [
                    entry for entry in commands if f"/{case}.dir/" in entry["command"]
                ]
                if not selected:
                    raise ValueError(f"Missing target {case} in {arm}")
                for index, entry in enumerate(selected):
                    command = shlex.split(entry["command"])
                    position = command.index("-o") + 1
                    original = (Path(entry["directory"]) / command[position]).resolve()
                    obj = output / f"{case}-{index}.o"
                    objects[original] = obj
                    command[position] = str(obj)
                    subprocess.run(command, cwd=entry["directory"], check=True)
                link = shlex.split((target / "link.txt").read_text())
                directory = target.parent.parent
                binary = output / case
                for index, value in enumerate(link):
                    if value.endswith(".o"):
                        key = (directory / value).resolve()
                        if key not in objects:
                            raise ValueError(f"Unknown original object {key}")
                        link[index] = str(objects[key])
                    elif value.endswith("libdetent_local_view.a"):
                        link[index] = str(HERE / arm / "libdetent_local_view.a")
                    elif index and link[index - 1] == "-o":
                        link[index] = str(binary)
                subprocess.run(link, cwd=directory, check=True)
                outcomes: list[int] = []
                for observe in (False, True):
                    environment = dict(os.environ, METADATA_OBSERVER=str(int(observe)))
                    command = [str(binary)]
                    if case == "test_local_trusted_paths":
                        source = next(
                            row["file"]
                            for row in selected
                            if row["file"].endswith("test_trusted_paths.cc")
                        )
                        command = [
                            "sh",
                            str(Path(source).with_suffix(".sh")),
                            str(binary),
                            str(output / "trusted-fixtures"),
                        ]
                    result = subprocess.run(
                        command,
                        check=False,
                        env=environment,
                        cwd=output,
                        capture_output=True,
                        text=True,
                        timeout=90,
                    )
                    rows.append(
                        {
                            "arm": arm,
                            "case": case,
                            "observer": observe,
                            "returncode": result.returncode,
                            "stdout": result.stdout,
                            "stderr": result.stderr,
                        }
                    )
                    outcomes.append(result.returncode)
                    if result.returncode not in (0, 77):
                        raise RuntimeError(
                            f"Failed {arm}/{case}/{observe}: {result.stderr}"
                        )
                if outcomes[0] != outcomes[1]:
                    raise RuntimeError(f"Observer changes outcome: {arm}/{case}")
        report["status"] = "passed"
    except BaseException as error:
        report.update(status="failed", error=repr(error))
        raise
    finally:
        (HERE / "validation.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
