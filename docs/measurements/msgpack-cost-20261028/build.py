from __future__ import annotations

import shlex
import subprocess
from pathlib import Path

root = Path(__file__).resolve().parent.parent
for arm in ("before", "after"):
    directory = (
        Path(
            "/Users/kronuz/code/Detent/.scratch/resource-adoption-20261027/baseline-build"
        )
        if arm == "before"
        else root / "build"
    ) / "tests/detent_distribution"
    metadata = directory / "CMakeFiles/test_distribution_encoded_update.dir"
    flags = {}
    for line in (metadata / "flags.make").read_text().splitlines():
        if line.startswith("CXX_"):
            key, value = line.split(" = ", 1)
            flags[key] = shlex.split(value)
    link = shlex.split((metadata / "link.txt").read_text())
    output = root / f"cost/production-{arm}"
    object_path = output.with_suffix(".o")
    subprocess.run(
        [
            link[0],
            *flags["CXX_DEFINES"],
            *flags["CXX_INCLUDES"],
            *flags["CXX_FLAGS"],
            "-c",
            str(root / "cost/production.cc"),
            "-o",
            str(object_path),
        ],
        check=True,
    )
    object_index = next(i for i, value in enumerate(link) if value.endswith(".cc.o"))
    link[object_index] = str(object_path)
    link[link.index("-o") + 1] = str(output)
    subprocess.run(link, cwd=directory, check=True)
