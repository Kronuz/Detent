from __future__ import annotations

import json
import shlex
import shutil
import subprocess
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent / "resource-adoption-20261027"
PROTOTYPE = HERE / "source/src/detent_local"


def main() -> None:
    status = subprocess.check_output(
        ["git", "status", "--porcelain"], cwd=HERE / "source", text=True
    )
    if status.strip():
        raise ValueError("Commit prototype before building the frozen screen")
    for arm, folder, capsule in (
        ("private-current", "baseline-build", False),
        ("private-capsule", "baseline-build", True),
        ("shared-current", "reader-build", False),
        ("shared-capsule", "reader-build", True),
    ):
        build = ROOT / folder
        entries: list[dict[str, str]] = json.loads(
            (build / "compile_commands.json").read_text()
        )
        entry = next(
            row for row in entries if row["file"].endswith("/prepared_reader.cc")
        )
        original = Path(entry["file"])
        output = HERE / arm
        output.mkdir(exist_ok=True)
        source = output / "prepared_reader.cc"
        source.write_text(
            (PROTOTYPE / "prepared_reader.cc").read_text()
            if capsule
            else original.read_text()
        )
        if capsule:
            shutil.copyfile(
                PROTOTYPE / "reader_capsule.hh", output / "reader_capsule.hh"
            )
        objects: list[Path] = []
        for name in (
            ("prepared_reader.cc", "reader_capsule.cc")
            if capsule
            else ("prepared_reader.cc",)
        ):
            if name == "reader_capsule.cc":
                shutil.copyfile(PROTOTYPE / name, output / name)
            obj = output / (name + ".o")
            command = shlex.split(entry["command"])
            command[command.index("-o") + 1] = str(obj)
            command[command.index("-c") + 1] = str(output / name)
            command += ["-I", str(original.parent)]
            subprocess.run(command, cwd=entry["directory"], check=True)
            objects.append(obj)
        archive = output / "libdetent_local_view.a"
        shutil.copyfile(build / "src/detent_local/libdetent_local_view.a", archive)
        subprocess.run(
            ["/usr/local/opt/llvm/bin/llvm-ar", "r", str(archive), *map(str, objects)],
            check=True,
        )
        probe_source = output / "probe.cc"
        shutil.copyfile(ROOT / "diagnostic/probe.cc", probe_source)
        target = (
            build
            / "tests/detent_system/CMakeFiles/detent_system_prepared_client_probe.dir"
        )
        flags = dict(
            line.split(" = ", 1)
            for line in (target / "flags.make").read_text().splitlines()
            if " = " in line
        )
        command = ["/usr/local/opt/llvm/bin/clang++"]
        for key in ("CXX_DEFINES", "CXX_INCLUDES", "CXX_FLAGS"):
            command += shlex.split(flags[key])
        probe_obj = output / "probe.o"
        subprocess.run(
            [*command, "-c", str(probe_source), "-o", str(probe_obj)], check=True
        )
        link = shlex.split((target / "link.txt").read_text())
        for index, value in enumerate(link):
            if value.endswith("prepared_client_probe.cc.o"):
                link[index] = str(probe_obj)
            elif value.endswith("libdetent_local_view.a"):
                link[index] = str(archive)
            elif index and link[index - 1] == "-o":
                link[index] = str(output / "probe")
        subprocess.run(link, cwd=target.parent.parent, check=True)
    (HERE / "screen-source-commit.txt").write_text(
        subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=HERE / "source", text=True
        )
    )


if __name__ == "__main__":
    main()
