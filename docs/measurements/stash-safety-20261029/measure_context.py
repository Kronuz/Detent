from __future__ import annotations

import json
import subprocess
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]


def main() -> None:
    source = HERE / "context.cc"
    source.write_text(
        "#include <stash.h>\n#include <iostream>\n"
        'int main() { std::cout << sizeof(StashContext) << " " << alignof(StashContext) << "\\n"; }\n'
    )
    result: dict[str, object] = {}
    for name, directory in (
        ("before", REPO / ".scratch/lru-erase-20261029/consumer-build/_deps/stash-src"),
        ("after", HERE / "build/_deps/stash-src"),
    ):
        revision = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=directory, text=True
        ).strip()
        binary = HERE / ("context-" + name)
        subprocess.run(
            [
                "/usr/local/opt/llvm/bin/clang++",
                "-std=c++20",
                "-O2",
                "-isysroot",
                "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk",
                "-I",
                str(directory),
                str(source),
                "-o",
                str(binary),
            ],
            check=True,
        )
        size, alignment = map(
            int, subprocess.check_output([str(binary)], text=True).split()
        )
        result[name] = {
            "repository": "stash",
            "revision": revision,
            "sizeof_StashContext": size,
            "alignment": alignment,
        }
    (HERE / "context-footprint.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
