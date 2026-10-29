from __future__ import annotations

import json
import shlex
import shutil
import subprocess
from pathlib import Path

HERE = Path(__file__).resolve().parent
WORK = HERE.parent
REPO = WORK.parents[1]


def main() -> None:
    source = WORK / "source"
    build = HERE / "build"
    deps = HERE / "deps"
    deps.mkdir(exist_ok=True)
    args = [
        "cmake",
        "-S",
        str(source),
        "-B",
        str(build),
        "-DCMAKE_CXX_COMPILER=/usr/local/opt/llvm/bin/clang++",
        "-DCMAKE_OSX_SYSROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk",
        "-DCMAKE_BUILD_TYPE=Release",
        "-DCMAKE_CXX_FLAGS_RELEASE=-O1 -g -UNDEBUG",
        "-DCMAKE_CXX_FLAGS=-fsanitize=fuzzer-no-link,address,undefined -fno-omit-frame-pointer",
        "-DDETENT_BUILD_TESTS=ON",
        "-DDETENT_BUILD_DISTRIBUTOR=OFF",
        "-DDETENT_BUILD_MANAGED_AGENT=OFF",
        "-DDETENT_BUILD_AUTHORITY=OFF",
        "-DDETENT_BUILD_RELAY=OFF",
        "-DDETENT_BUILD_EXAMPLES=OFF",
        "-DDETENT_BUILD_BENCHMARKS=OFF",
    ]
    pins: list[dict[str, str]] = json.loads(
        (source / "cmake/dependencies.json").read_text()
    )
    for pin in pins:
        original = (
            REPO
            / ".scratch/stash-adoption-20261029/build/_deps"
            / (pin["name"] + "-src")
        )
        if not original.exists():
            continue
        copied = deps / original.name
        if not copied.exists():
            shutil.copytree(original, copied)
        revision = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=copied, text=True
        ).strip()
        if revision != pin["revision"]:
            raise ValueError(f"Unexpected dependency revision for {pin['name']}")
        args.append(f"-DFETCHCONTENT_SOURCE_DIR_{pin['name'].upper()}={copied}")
    subprocess.run(args, check=True)
    subprocess.run(
        [
            "cmake",
            "--build",
            str(build),
            "-j4",
            "--target",
            "detent_local_view",
            "detent_local_builder",
        ],
        check=True,
    )
    target = build / "tests/detent_local/CMakeFiles/test_local_reader_capsule.dir"
    flags = dict(
        line.split(" = ", 1)
        for line in (target / "flags.make").read_text().splitlines()
        if " = " in line
    )
    command = ["/usr/local/opt/llvm/bin/clang++"]
    for key in ("CXX_DEFINES", "CXX_INCLUDES", "CXX_FLAGS"):
        command += shlex.split(flags[key])
    obj = HERE / "capsule_fuzz.o"
    subprocess.run(
        [*command, "-c", str(HERE / "capsule_fuzz.cc"), "-o", str(obj)], check=True
    )
    link = shlex.split((target / "link.txt").read_text())
    for index, value in enumerate(link):
        if value.endswith("test_reader_capsule.cc.o"):
            link[index] = str(obj)
        elif index and link[index - 1] == "-o":
            link[index] = str(HERE / "capsule_fuzz")
    link += ["-fsanitize=fuzzer,address,undefined"]
    subprocess.run(link, cwd=target.parent.parent, check=True)


if __name__ == "__main__":
    main()
