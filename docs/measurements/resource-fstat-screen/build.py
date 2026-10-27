from __future__ import annotations

import json
import shlex
import shutil
import subprocess
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent


def main() -> None:
    for arm, folder in (("private", "baseline-build"), ("shared", "reader-build")):
        build = ROOT / folder
        entries: list[dict[str, str]] = json.loads(
            (build / "compile_commands.json").read_text()
        )
        entry = next(x for x in entries if x["file"].endswith("/lazy_page_access.cc"))
        original = Path(entry["file"])
        source = original.read_text()
        redundant = "\t\tcheck_generation_file(payload.fd, publisher, length);\n"
        if source.count(redundant) != (1 if arm == "private" else 0):
            raise ValueError("Unexpected source")
        source = source.replace(redundant, "")
        output = HERE / arm
        output.mkdir(exist_ok=True)
        copied = output / "lazy_page_access.cc"
        copied.write_text(source)
        obj = output / "lazy_page_access.cc.o"
        command = shlex.split(entry["command"])
        command[command.index("-o") + 1] = str(obj)
        command[command.index("-c") + 1] = str(copied)
        command.extend(["-I", str(original.parent)])
        subprocess.run(command, cwd=entry["directory"], check=True)
        archive = output / "libdetent_local_view.a"
        shutil.copyfile(build / "src/detent_local/libdetent_local_view.a", archive)
        subprocess.run(
            ["/usr/local/opt/llvm/bin/llvm-ar", "r", str(archive), str(obj)], check=True
        )
        target = (
            build
            / "tests/detent_system/CMakeFiles/detent_system_prepared_client_probe.dir"
        )
        link = shlex.split((target / "link.txt").read_text())
        for i, arg in enumerate(link):
            if arg.endswith("libdetent_local_view.a"):
                link[i] = str(archive)
            elif i and link[i - 1] == "-o":
                link[i] = str(output / "probe")
        subprocess.run(link, cwd=target.parent.parent, check=True)
        if arm != "shared":
            continue
        test = ROOT / "migration-build/tests/detent_local/CMakeFiles/test_local_lazy_page_access.dir"
        test_link = shlex.split((test / "link.txt").read_text())
        for i, arg in enumerate(test_link):
            if arg.endswith("libdetent_local_view.a"):
                test_link[i] = str(archive)
            elif i and test_link[i - 1] == "-o":
                test_link[i] = str(output / "test_lazy")
        subprocess.run(test_link, cwd=test.parent.parent, check=True)
        subprocess.run([str(output / "test_lazy")], check=True, timeout=90)


if __name__ == "__main__":
    main()
