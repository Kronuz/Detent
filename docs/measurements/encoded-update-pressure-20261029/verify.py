from __future__ import annotations

import json
import shlex
import subprocess
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SOURCE = ROOT / "tests/detent_distribution/test_encoded_update.cc"
BUILD = ROOT / ".scratch/stash-adoption-20261029/build"


def compile_case(source: Path, name: str) -> Path:
    entries: list[dict[str, str]] = json.loads(
        (BUILD / "compile_commands.json").read_text()
    )
    entry = next(
        row for row in entries if row["file"].endswith("/test_encoded_update.cc")
    )
    command = shlex.split(entry["command"])
    obj = HERE / (name + ".o")
    command[command.index("-o") + 1] = str(obj)
    command[command.index("-c") + 1] = str(source)
    command += ["-I", str(SOURCE.parent)]
    subprocess.run(command, cwd=entry["directory"], check=True)
    target = (
        BUILD
        / "tests/detent_distribution/CMakeFiles/test_distribution_encoded_update.dir"
    )
    link = shlex.split((target / "link.txt").read_text())
    binary = HERE / name
    for index, value in enumerate(link):
        if value.endswith("test_encoded_update.cc.o"):
            link[index] = str(obj)
        elif index and link[index - 1] == "-o":
            link[index] = str(binary)
    subprocess.run(link, cwd=target.parent.parent, check=True)
    return binary


def main() -> None:
    global BUILD
    current = SOURCE.read_text()
    site = "auto case_account = std::make_shared<d::StorageLedger>(publication, publication->limit());"
    assert current.count(site) == 1
    mutant = HERE / "shared-account-mutant.cc"
    mutant.write_text(current.replace(site, "auto case_account = publication;"))
    for name, build in [
        ("private", ".scratch/stash-adoption-20261029/build"),
        ("canonical", ".scratch/reader-capsule-20261029/canonical-build"),
    ]:
        BUILD = ROOT / build
        good = compile_case(SOURCE, name + "-isolated")
        bad = compile_case(mutant, name + "-mutant")
        result = subprocess.run(
            [str(bad)],
            cwd=HERE,
            capture_output=True,
            text=True,
            timeout=30,
            check=False,
        )
        (HERE / (name + "-mutant.log")).write_text(result.stdout + result.stderr)
        if result.returncode == 0 or "encoded update invariant" not in result.stderr:
            raise RuntimeError("mutant did not fail expected invariant")
        for _ in range(30):
            subprocess.run([str(good)], cwd=HERE, check=True, timeout=30)
        print(name + ": 30 isolated runs pass; shared-account mutant fails", flush=True)


if __name__ == "__main__":
    main()
