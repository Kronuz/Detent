from __future__ import annotations

import subprocess
from pathlib import Path

HERE = Path(__file__).resolve().parent


def main() -> None:
    subprocess.run(
        [
            "cmake",
            "-S",
            str(HERE / "source"),
            "-B",
            str(HERE / "build"),
            "-DCMAKE_CXX_COMPILER=/usr/local/opt/llvm/bin/clang++",
            "-DCMAKE_OSX_SYSROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk",
            "-DCMAKE_BUILD_TYPE=Release",
            "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
            "-DDETENT_BUILD_TESTS=ON",
            "-DDETENT_BUILD_AUTHORITY=OFF",
            "-DDETENT_BUILD_RELAY=OFF",
            "-DDETENT_BUILD_DISTRIBUTOR=OFF",
            "-DDETENT_BUILD_MANAGED_AGENT=OFF",
            "-DDETENT_BUILD_EXAMPLES=OFF",
            "-DDETENT_BUILD_BENCHMARKS=OFF",
        ],
        check=True,
    )
    subprocess.run(
        [
            "cmake",
            "--build",
            str(HERE / "build"),
            "-j4",
            "--target",
            "detent-agent",
            "detent-reader",
            "test_logging",
            "test_agent_basic",
            "test_snapshot_canonical",
            "test_shm_concurrency_stress",
            "test_reader_capacity",
        ],
        check=True,
    )
    subprocess.run(
        [
            "ctest",
            "--test-dir",
            str(HERE / "build"),
            "--output-on-failure",
            "-j1",
            "-R",
            "^test_(logging|agent_basic|snapshot_canonical|shm_concurrency_stress|reader_capacity|agent_cli|system_agent)$",
        ],
        check=True,
    )


if __name__ == "__main__":
    main()
