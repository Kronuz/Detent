from __future__ import annotations

import hashlib
import json
import os
import shlex
import subprocess
from pathlib import Path

root = Path(__file__).resolve().parent
baseline = root.parent / "resource-adoption-20261027/baseline-build"
manifests: dict[str, dict[str, str]] = {}
for arm, build in (("before", baseline), ("after", root / "build")):
    output = root / f"wire-{arm}"
    output.mkdir(exist_ok=True)
    for name, relative, target in (
        ("export", "tests/detent_authority", "test_authority_export_encoder"),
        ("checkpoint", "benchmarks", "detent_authority_bench"),
    ):
        directory = build / relative
        metadata = directory / f"CMakeFiles/{target}.dir"
        flags: dict[str, list[str]] = {}
        for line in (metadata / "flags.make").read_text().splitlines():
            if line.startswith("CXX_"):
                key, value = line.split(" = ", 1)
                flags[key] = shlex.split(value)
        link = shlex.split((metadata / "link.txt").read_text())
        binary = output / name
        object_path = binary.with_suffix(".o")
        subprocess.run(
            [
                link[0],
                *flags["CXX_DEFINES"],
                *flags["CXX_INCLUDES"],
                *flags["CXX_FLAGS"],
                "-c",
                str(root / f"capture-{name}.cc"),
                "-o",
                str(object_path),
            ],
            check=True,
        )
        object_index = next(
            i for i, value in enumerate(link) if value.endswith(".cc.o")
        )
        link[object_index] = str(object_path)
        link[link.index("-o") + 1] = str(binary)
        subprocess.run(link, cwd=directory, check=True)
        environment = dict(os.environ, TREE_CAPTURE_DIR=str(output))
        command = [str(binary)]
        if name == "checkpoint":
            command += ["1000", "uniform", "2"]
        subprocess.run(command, env=environment, check=True, timeout=120)
    manifests[arm] = {
        path.name: hashlib.sha256(path.read_bytes()).hexdigest()
        for path in sorted(output.glob("*.wire"))
    }
if not manifests["before"] or manifests["before"] != manifests["after"]:
    raise RuntimeError(f"Wire captures differ: {manifests}")
(root / "wire-identity.json").write_text(json.dumps(manifests, indent=2) + "\n")
