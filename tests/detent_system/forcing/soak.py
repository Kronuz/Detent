"""Run one ctest entry RUNS times, JOBS copies at once, each copy in its own working directory.

Usage: soak.py BUILD TARGETS RUNS JOBS TEST. Builds TARGETS (comma-separated), runs the test's fixture
setup once, then the test's own command; prints failures' tails and exits nonzero if any run failed.
"""

import json
import os
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

build, targets, runs, jobs, name = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4]), sys.argv[5]
build_dir = Path(build).resolve()
if not (build_dir / "CMakeCache.txt").exists():
    subprocess.run(
        ["cmake", "-S", ".", "-B", str(build_dir), "-DCMAKE_BUILD_TYPE=RelWithDebInfo",
         "-DDETENT_BUILD_AUTHORITY=ON", "-DDETENT_BUILD_RELAY=ON", "-DDETENT_BUILD_MANAGED_AGENT=ON",
         "-DDETENT_BUILD_BENCHMARKS=OFF", "-DDETENT_BUILD_EXAMPLES=OFF", "-DDETENT_BUILD_TESTS=ON"],
        check=True, stdout=subprocess.DEVNULL)
built = subprocess.run(["cmake", "--build", str(build_dir), "-j4", "--target", *targets.split(",")],
                       capture_output=True, text=True)
if built.returncode:
    print(built.stdout[-4000:], built.stderr[-4000:])
    sys.exit(1)
listing = json.loads(subprocess.run(["ctest", "--test-dir", str(build_dir), "--show-only=json-v1"],
                                    capture_output=True, text=True, check=True).stdout)
tests = {t["name"]: t for t in listing["tests"]}
test = tests[name]
props = {p["name"]: p["value"] for p in test.get("properties", [])}
for fixture in props.get("FIXTURES_REQUIRED", []):
    for setup in (t for t in listing["tests"] if fixture in
                  {p["name"]: p["value"] for p in t.get("properties", [])}.get("FIXTURES_SETUP", [])):
        subprocess.run(setup["command"], cwd=next((p["value"] for p in setup.get("properties", [])
                                                  if p["name"] == "WORKING_DIRECTORY"), build_dir), check=True)
base = Path(props.get("WORKING_DIRECTORY", build_dir))
timeout = float(props.get("TIMEOUT", 600))
root = build_dir / f"soak-{name}"
subprocess.run(["rm", "-rf", str(root)])

def one(index: int) -> tuple[int, str, float]:
    cwd = root / str(index)
    cwd.mkdir(parents=True)
    began = time.monotonic()
    try:
        done = subprocess.run(test["command"], cwd=cwd if "WORKING_DIRECTORY" not in props else base,
                              capture_output=True, text=True, timeout=timeout)
        code, out = done.returncode, done.stdout + done.stderr
    except subprocess.TimeoutExpired as error:
        code, out = 124, f"timeout after {timeout} s\n{error.stdout or ''}{error.stderr or ''}"
    if code == 0:
        subprocess.run(["rm", "-rf", str(cwd)])
    return code, out, time.monotonic() - began

failed = 0
slowest = 0.0
with ThreadPoolExecutor(jobs) as pool:
    for index, (code, out, elapsed) in enumerate(pool.map(one, range(runs))):
        slowest = max(slowest, elapsed)
        if code:
            failed += 1
            if failed <= 3:
                print(f"--- run {index} exit {code} after {elapsed:.1f} s (kept in {root / str(index)})")
                print(out[-6000:])
print(f"RESULT {name}: {failed} of {runs} failed ({jobs} at once, slowest {slowest:.1f} s)")
sys.exit(1 if failed else 0)
