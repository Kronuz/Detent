"""Capture production logical bytes from a configured Unix Makefiles test build.
Build test_distribution_encoded_update first, then pass SOURCE BUILD OUTPUT.
Requires quiet and an OUTPUT without .wire files. manifest.json hashes logical
bytes after raw/zstd frame decoding; existing fixture checks still run.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shlex
import subprocess
from pathlib import Path


def run(command: list[str], cwd: Path, env: dict[str, str] | None = None) -> None:
    result = subprocess.run(["quiet", "check"], check=False)
    if result.returncode not in (0, 2):
        raise SystemExit("Strict host hold: capture step deferred")
    subprocess.run(["nice", "-n", "19", *command], check=True, cwd=cwd, env=env)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("build", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--key-bytes", type=int, choices=(34, 200), default=34)
    args = parser.parse_args()
    source, build, output = (
        args.source.resolve(),
        args.build.resolve(),
        args.output.resolve(),
    )
    output.mkdir(parents=True, exist_ok=True)
    if any(output.glob("*.wire")):
        raise SystemExit("Use an output directory without existing .wire captures")
    fixture = (source / "tests/detent_distribution/test_encoded_update.cc").read_text()
    if args.key_bytes == 200:
        replacements = {
            "char key[35]": "char key[201]",
            "app:%030u": "app:%0196u",
            "3000 * 34 + 3": "3000 * 200 + 3",
        }
        for old, new in replacements.items():
            assert fixture.count(old) == 1
            fixture = fixture.replace(old, new)
    marker = "\tdecoder.finish();\n\treturn result;\n}\nd::RetirementHandle"
    assert fixture.count(marker) == 1
    capture = """\tdecoder.finish();
    static unsigned sequence = 0;
    const auto *directory = std::getenv("DETENT_CAPTURE_DIR");
    if (!directory) throw std::runtime_error("missing wire capture directory");
    const auto path = std::string(directory) + "/" + std::to_string(sequence++) + ".wire";
    std::ofstream file(path, std::ios::binary);
    file.write(result.data(), static_cast<std::streamsize>(result.size()));
    if (!file) throw std::runtime_error("wire capture failed");
\treturn result;
}
d::RetirementHandle"""
    fixture = "#include <fstream>\n#include <cstdlib>\n" + fixture.replace(
        marker, capture
    )
    capture_source = output / "capture.cc"
    capture_source.write_text(fixture)
    target = (
        build
        / "tests/detent_distribution/CMakeFiles/test_distribution_encoded_update.dir"
    )
    flags: dict[str, list[str]] = {}
    for line in (target / "flags.make").read_text().splitlines():
        if line.startswith("CXX_") and " = " in line:
            key, value = line.split(" = ", 1)
            flags[key] = shlex.split(value)
    compiler = shlex.split((target / "link.txt").read_text())[0]
    obj = output / "capture.o"
    run(
        [
            compiler,
            *flags["CXX_DEFINES"],
            *flags["CXX_INCLUDES"],
            *flags["CXX_FLAGS"],
            "-c",
            str(capture_source),
            "-o",
            str(obj),
        ],
        build,
    )
    command = shlex.split((target / "link.txt").read_text())
    command[command.index("-o") + 1] = str(output / "capture")
    objects = [
        i for i, word in enumerate(command) if word.endswith("test_encoded_update.cc.o")
    ]
    assert len(objects) == 1
    command[objects[0]] = str(obj)
    run(command, build / "tests/detent_distribution")
    env = os.environ.copy()
    env["DETENT_CAPTURE_DIR"] = str(output)
    run([str(output / "capture")], build, env)
    captures = sorted(output.glob("*.wire"))
    if len(captures) != 7:
        raise SystemExit(f"Expected seven captures; found {len(captures)}")
    manifest = {
        "key_bytes": args.key_bytes,
        "files": {
            path.name: {
                "bytes": path.stat().st_size,
                "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
            }
            for path in captures
        },
    }
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")


if __name__ == "__main__":
    main()
