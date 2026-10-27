from __future__ import annotations

import shlex
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main() -> None:
    for label, folder in (("before", "baseline-build"), ("after", "reader-build")):
        build = ROOT / folder
        directory = build / "tests/detent_system"
        target = directory / "CMakeFiles/detent_system_prepared_client_probe.dir"
        flags = dict(
            line.split(" = ", 1)
            for line in (target / "flags.make").read_text().splitlines()
            if " = " in line
        )
        obj = ROOT / f"diagnostic/{label}.o"
        command = ["/usr/local/opt/llvm/bin/clang++"]
        for key in ("CXX_DEFINES", "CXX_INCLUDES", "CXX_FLAGS"):
            command.extend(shlex.split(flags[key]))
        command.extend(["-c", str(ROOT / "diagnostic/probe.cc"), "-o", str(obj)])
        subprocess.run(command, check=True)
        link = shlex.split((target / "link.txt").read_text())
        for index, arg in enumerate(link):
            if arg.endswith("prepared_client_probe.cc.o"):
                link[index] = str(obj)
            if index and link[index - 1] == "-o":
                link[index] = str(ROOT / f"diagnostic/{label}")
        subprocess.run(link, cwd=directory, check=True)
    (ROOT / "diagnostic/.built").touch()


if __name__ == "__main__":
    main()
