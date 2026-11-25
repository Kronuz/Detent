#!/bin/bash
# EX-1 A7: fresh-process resident-data first reads of this branch (allocators' accounting) against
# main (Detent's own copies), with main again as the A/A control. Each arm is the same program built
# from its tree: the reader core (`detent_open`, first `detent_get`) and the Client (retirement
# runtime, attach, refresh thread, first `try_get`), both from tests/detent_system/first_read_phases.cc.
#   resource_first_read.sh prepare OUT   light, with --ref BASELINE=main: builds both trees (pinned
#       LLVM 23.1.2, Release, DETENT_READ_COUNTERS=OFF) and one store of 10,000 int knobs per tree.
#   resource_first_read.sh measure OUT   exclusive: per program and namespace count, six rounds of
#       main, branch and main again (rotated), 200 reads each after five warmups; fails unless the
#       median paired difference (branch minus main) is within the A/A band.
set -euo pipefail
llvm=/usr/local/opt/llvm/bin
sdk=$(sed -n 's/^-isysroot //p' "$("$llvm/clang++" --version | sed -n 's/^Configuration file: //p')")
knobs=10000

prepare() {
	local branch=$PWD out
	mkdir -p "$1"
	out=$(cd "$1" && pwd)
	"$llvm/clang++" --version | tee "$out/toolchain.txt"
	grep -q 'clang version 23.1.2' "$out/toolchain.txt"
	cat > "$out/targets.cmake" <<EOF
add_executable(first_read_phases "$branch/tests/detent_system/first_read_phases.cc")
target_include_directories(first_read_phases PRIVATE "\${PROJECT_SOURCE_DIR}/src/detent_local")
target_link_libraries(first_read_phases PRIVATE detent::local_client detent::testing_store)
add_executable(first_read_core "$branch/tests/detent_system/first_read_phases.cc")
target_compile_definitions(first_read_core PRIVATE FIRST_READ_CORE=1)
target_link_libraries(first_read_core PRIVATE detent::reader)
EOF
	echo "cmake_language(DEFER CALL include \"$out/targets.cmake\")" > "$out/defer.cmake"
	for code in main branch; do
		local src build
		if [ "$code" = main ]; then src=${JOB_REF_BASELINE:?--ref BASELINE=main}; else src=$branch; fi
		build=$src/.scratch/resource-first-read-build
		rm -rf "${out:?}/$code"
		mkdir -p "$out/$code"
		git -C "$src" rev-parse HEAD > "$out/$code/commit.txt"
		cmake -S "$src" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PROJECT_detent_INCLUDE="$out/defer.cmake" \
			-DCMAKE_CXX_COMPILER="$llvm/clang++" -DCMAKE_C_COMPILER="$llvm/clang" -DCMAKE_OSX_SYSROOT="$sdk" \
			-DDETENT_BUILD_AUTHORITY=OFF -DDETENT_BUILD_RELAY=OFF -DDETENT_BUILD_EXAMPLES=OFF \
			-DDETENT_BUILD_BENCHMARKS=OFF -DDETENT_BUILD_TESTS=ON -DDETENT_READ_COUNTERS=OFF >/dev/null
		grep -Eq '^CMAKE_BUILD_TYPE:[A-Z]+=Release$' "$build/CMakeCache.txt"
		grep -Eq '^DETENT_READ_COUNTERS:[A-Z]+=OFF$' "$build/CMakeCache.txt"
		cmake --build "$build" -j4 --target first_read_phases first_read_core
		cp "$build/first_read_phases" "$build/first_read_core" "$out/$code/"
	done
	# One store, written by main's writer, serves every arm: the bytes read are identical.
	for namespaces in 1 1000; do
		"$out/main/first_read_phases" --generate "$out/store-${namespaces}ns" "$namespaces" "$knobs" |
			tee "$out/store-${namespaces}ns.json"
	done
	file "$out"/*/first_read_*
}

measure() {
	/usr/local/bin/python3 -I - "$@" <<'PY'
import json, shutil, statistics, subprocess, sys
from pathlib import Path

prepared = Path(sys.argv[1]).resolve()
KNOBS, READS, ROUNDS, WARMUP = 10000, 200, 6, 5
KEYS = [(j * 7919) % KNOBS for j in range(40)]
ORDERS = [("main", "branch", "again"), ("branch", "again", "main"), ("again", "main", "branch")]


def command(program, code, store, namespaces, i):
    binary = prepared / ("main" if code == "again" else code) / program
    ns, key, value = f"n{i % namespaces}", f"k{i}", str(3 * i + 1)
    if program == "first_read_core":
        return [binary, "--core", store, f"{ns}:{key}", value]
    return [binary, "--client", store, ns, key, value]


def sample(program, code, store, namespaces, i):
    result = subprocess.run(command(program, code, store, namespaces, i), capture_output=True, text=True, timeout=10)
    if result.returncode:
        raise RuntimeError(f"{code} {program} exit {result.returncode}: {result.stderr[-400:]}")
    return json.loads(result.stdout.strip().splitlines()[-1])["total_ns"]


def run(program, code, namespaces, index):
    store = prepared / "runs" / f"{code}-{namespaces}ns-{index}"
    shutil.rmtree(store, ignore_errors=True)
    subprocess.run(["cp", "-cR", prepared / f"store-{namespaces}ns", store], check=True)
    try:
        for path in store.rglob("*"):  # resident data
            if path.is_file():
                path.read_bytes()
        for i in KEYS[:WARMUP]:
            sample(program, code, store, namespaces, i)
        return sorted(sample(program, code, store, namespaces, KEYS[r % len(KEYS)]) for r in range(READS))
    finally:
        shutil.rmtree(store, ignore_errors=True)


(prepared / "runs").mkdir(exist_ok=True)
ok = True
for program in ("first_read_core", "first_read_phases"):
    for namespaces in (1, 1000):
        totals = {code: [] for code in ("main", "branch", "again")}
        for index in range(ROUNDS):
            for code in ORDERS[index % 3]:
                totals[code].append(run(program, code, namespaces, index))
        medians = {code: [statistics.median(r) for r in runs] for code, runs in totals.items()}
        paired = statistics.median(b - m for b, m in zip(medians["branch"], medians["main"]))
        aa = [a - m for a, m in zip(medians["again"], medians["main"])]
        band = max(abs(x) for x in aa)
        pooled = {code: sorted(t for r in runs for t in r) for code, runs in totals.items()}
        passed = abs(paired) <= band or paired <= 0
        ok &= passed
        cell = {"program": program, "namespaces": namespaces, "pass": passed, "samples_per_arm": ROUNDS * READS,
                "median_ns": {c: int(statistics.median(p)) for c, p in pooled.items()},
                "p99_ns": {c: p[int(len(p) * 0.99) - 1] for c, p in pooled.items()},
                "below_1ms": {c: sum(t < 1_000_000 for t in p) for c, p in pooled.items()},
                "paired_branch_minus_main_ns": int(paired), "aa_paired_ns": [int(x) for x in aa],
                "aa_band_ns": int(band), "run_medians_ns": {c: [int(x) for x in m] for c, m in medians.items()}}
        print("A7 cell " + json.dumps(cell), flush=True)
shutil.rmtree(prepared / "runs", ignore_errors=True)
print(f"A7 {'PASS' if ok else 'FAIL'}: branch first reads within the A/A band of main's")
sys.exit(0 if ok else 1)
PY
}

case ${1:-} in
prepare) prepare "${2:?OUT}" ;;
measure) measure "${2:?OUT}" ;;
*) echo "usage: $0 prepare OUT | measure OUT" >&2; exit 2 ;;
esac
