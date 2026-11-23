#!/bin/bash
# First-read gate (reader-s6 A16, reader.md T2 and D49): fresh-process first reads, 200 per run, through
# the branch's reader core (`detent_open`, then the first `detent_get`) against main's pre-S6 Client
# (runtime, attach, refresh thread, first lookup), over 1- and 1,000-namespace stores, publisher stopped
# and running, interleaved. Both arms come from tests/detent_system/first_read_phases.cc.
#   first-read-gate.sh prepare OUT   light, in the branch checkout with --ref BASELINE=main: builds
#       both trees (pinned LLVM 23.1.2, Release, DETENT_READ_COUNTERS=OFF) and each tree's own store
#       of 10,000 int knobs in 1 and in 1,000 namespaces, under OUT (the job's own path depth).
#   first-read-gate.sh measure OUT [stopped|running]   exclusive: six main/branch pairs per cell;
#       fails unless the branch's paired difference and pooled p99 stay within main's plus their floors.
set -euo pipefail
llvm=/usr/local/opt/llvm/bin
# CMake detects implicit includes under the SDK LLVM's config file names; any other sysroot leaves
# zlib's SDK include dir as an -isystem ahead of libc++.
sdk=$(sed -n 's/^-isysroot //p' "$("$llvm/clang++" --version | sed -n 's/^Configuration file: //p')")
knobs=10000

# A -D cache entry keeps the type it was given (STRING or UNINITIALIZED), never FILEPATH; the
# detected version comes from CMake's compiler record.
check() {
	local cache=$1/CMakeCache.txt
	grep -Eq '^CMAKE_CXX_COMPILER:[A-Z]+=/usr/local/opt/llvm/bin/clang\+\+$' "$cache" || { echo "check: compiler not $llvm/clang++" >&2; return 1; }
	grep -Eq 'set\(CMAKE_CXX_COMPILER_VERSION "23\.1\.2[".]' "$1"/CMakeFiles/*/CMakeCXXCompiler.cmake || { echo "check: compiler version not 23.1.2" >&2; return 1; }
	grep -Eq "^CMAKE_OSX_SYSROOT:[A-Z]+=$sdk\$" "$cache" || { echo "check: sysroot not $sdk" >&2; return 1; }
	grep -Eq '^CMAKE_BUILD_TYPE:[A-Z]+=Release$' "$cache" || { echo "check: build type not Release" >&2; return 1; }
	grep -Eq '^DETENT_READ_COUNTERS:[A-Z]+=OFF$' "$cache" || { echo "check: DETENT_READ_COUNTERS not OFF" >&2; return 1; }
}

prepare() {
	local branch=$PWD out
	mkdir -p "$1"
	out=$(cd "$1" && pwd)
	[ -d "$sdk" ] || { echo "prepare: no SDK from the LLVM config file" >&2; return 1; }
	"$llvm/clang++" --version | tee "$out/toolchain.txt"
	grep -q 'clang version 23.1.2' "$out/toolchain.txt"
	# The gate's targets join each tree after its top-level CMakeLists.txt, which stays untouched.
	cat > "$out/targets.cmake" <<EOF
add_executable(first_read_phases "$branch/tests/detent_system/first_read_phases.cc")
target_include_directories(first_read_phases PRIVATE "\${PROJECT_SOURCE_DIR}/src/detent_local")
target_link_libraries(first_read_phases PRIVATE detent::local_client detent::testing_store)
if(TARGET detent::reader)
  add_executable(first_read_core "$branch/tests/detent_system/first_read_phases.cc")
  target_compile_definitions(first_read_core PRIVATE FIRST_READ_CORE=1)
  target_link_libraries(first_read_core PRIVATE detent::reader)
endif()
EOF
	echo "cmake_language(DEFER CALL include \"$out/targets.cmake\")" > "$out/defer.cmake"
	for code in main branch; do
		local src build targets=first_read_phases
		if [ "$code" = main ]; then src=${JOB_REF_BASELINE:?--ref BASELINE=main}; else src=$branch; targets="$targets first_read_core"; fi
		build=$src/.scratch/first-read-build
		rm -rf "${out:?}/$code"
		mkdir -p "$out/$code"
		git -C "$src" rev-parse HEAD > "$out/$code/commit.txt"
		cmake -S "$src" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PROJECT_detent_INCLUDE="$out/defer.cmake" \
			-DCMAKE_CXX_COMPILER="$llvm/clang++" -DCMAKE_C_COMPILER="$llvm/clang" -DCMAKE_OSX_SYSROOT="$sdk" \
			-DDETENT_BUILD_AUTHORITY=OFF -DDETENT_BUILD_RELAY=OFF -DDETENT_BUILD_EXAMPLES=OFF \
			-DDETENT_BUILD_BENCHMARKS=OFF -DDETENT_BUILD_TESTS=ON -DDETENT_READ_COUNTERS=OFF >/dev/null
		check "$build"
		# shellcheck disable=SC2086
		cmake --build "$build" -j4 --target $targets
		for target in $targets; do cp "$build/$target" "$out/$code/$target"; done
		for namespaces in 1 1000; do
			"$out/$code/first_read_phases" --generate "$out/$code/store-${namespaces}ns" "$namespaces" "$knobs" |
				tee "$out/$code/store-${namespaces}ns.json"
		done
	done
	file "$out"/*/first_read_*
}

measure() {
	/usr/local/bin/python3 -I - "$@" <<'PY'
import json, shutil, statistics, subprocess, sys
from pathlib import Path

# mode: "measure" gates D140's bound (exclusive, D142); "count" gates only that the branch never fails
# and counts main's refusals, which do not depend on load (light).
mode, prepared, states = sys.argv[1], Path(sys.argv[2]).resolve(), sys.argv[3:] or ["stopped", "running"]
KNOBS, READS, PAIRS, WARMUP = 10000, 200, 6, 5
KEYS = [(j * 7919) % KNOBS for j in range(40)]
runs = prepared / "runs"


def command(code, store, namespaces, i):
    ns, key, value = f"n{i % namespaces}", f"k{i}", str(3 * i + 1)
    if code == "branch":
        return [prepared / code / "first_read_core", "--core", store, f"{ns}:{key}", value]
    return [prepared / code / "first_read_phases", "--client", store, ns, key, value]


def sample(code, store, namespaces, i):
    result = subprocess.run(command(code, store, namespaces, i), capture_output=True, text=True, timeout=10)
    if result.returncode:
        # main's Client refuses a generation file collected under it; the branch must never fail.
        if code == "main":
            return {"failed": result.stderr.strip()[-200:]}
        raise RuntimeError(f"{code} exit {result.returncode}: {result.stderr[-400:]}")
    return json.loads(result.stdout.strip().splitlines()[-1])


def run(code, namespaces, publisher, index):
    store = runs / f"{code}-{namespaces}ns-{publisher}-{index}"
    shutil.rmtree(store, ignore_errors=True)
    subprocess.run(["cp", "-cR", prepared / code / f"store-{namespaces}ns", store], check=True)
    agent = None
    try:
        if publisher == "running":
            agent = subprocess.Popen([prepared / code / "first_read_phases", "--publish", store, str(namespaces)],
                                     stdout=subprocess.PIPE, text=True)
            if agent.stdout.readline().strip() != "publishing":
                raise RuntimeError(f"{code} publisher did not start")
        for path in store.rglob("*"):  # resident data
            if path.is_file():
                path.read_bytes()
        for i in KEYS[:WARMUP]:
            sample(code, store, namespaces, i)
        samples = [sample(code, store, namespaces, KEYS[r % len(KEYS)]) for r in range(READS)]
    finally:
        if agent:
            agent.kill()
            agent.wait()
        shutil.rmtree(store, ignore_errors=True)
    failed = [s["failed"] for s in samples if "failed" in s]
    samples = [s for s in samples if "failed" not in s]
    totals = sorted(s["total_ns"] for s in samples)
    # Each read at or over 1 ms with its phases' wall, CPU and faults, against the run's medians.
    slow = [{k: v for k, v in s.items() if k != "components"} for s in samples if s["total_ns"] >= 1_000_000]
    return {
        "code": code, "namespaces": namespaces, "publisher": publisher, "index": index,
        "components": samples[0]["components"], "below_1ms": sum(t < 1_000_000 for t in totals),
        "failed": len(failed), "failures": sorted(set(failed)),
        "median_ns": int(statistics.median(totals)), "p99_ns": totals[int(len(totals) * 0.99) - 1],
        "max_ns": totals[-1],
        "phase_medians": {k: int(statistics.median(s[k] for s in samples)) for k in samples[0]
                          if k.endswith(("_ns", "_us", "_faults")) and k != "total_ns"},
        "slow": slow, "totals": totals,
    }


def spread(medians):
    return statistics.median(abs(a - b) for a, b in zip(medians, medians[1:]))


runs.mkdir(exist_ok=True)
cells, ok = [], True
for publisher in states:
    for namespaces in (1, 1000):
        flip = len(cells) % 2  # main first in even cells, branch first in odd ones
        results = []
        for pair in range(PAIRS):
            for code in (("branch", "main") if flip else ("main", "branch")):
                results.append(run(code, namespaces, publisher, pair))
                print(json.dumps({k: v for k, v in results[-1].items() if k != "totals"}), flush=True)
        arm = {code: [r for r in results if r["code"] == code] for code in ("main", "branch")}
        medians = {code: [r["median_ns"] for r in arm[code]] for code in arm}
        diffs = [b - m for b, m in zip(medians["branch"], medians["main"])]
        pooled = {code: sorted(t for r in arm[code] for t in r["totals"]) for code in arm}
        p99 = {code: pooled[code][int(len(pooled[code]) * 0.99) - 1] for code in arm}
        diff = int(statistics.median(diffs))
        floor = int(max(spread(medians["main"]), spread(medians["branch"])))
        main_p99s = [r["p99_ns"] for r in arm["main"]]
        p99_floor = max(main_p99s) - min(main_p99s)
        # D140, D142: the median paired difference within the medians' floor, and the pooled branch
        # p99 within main's plus main's run-to-run p99 spread; 1 ms and 200/200 are reported only.
        passed = mode == "count" or (diff <= floor and p99["branch"] <= p99["main"] + p99_floor)
        ok &= passed
        cell = {"publisher": publisher, "namespaces": namespaces, "pass": passed,
                "branch_below_1ms": [r["below_1ms"] for r in arm["branch"]],
                "main_below_1ms": [r["below_1ms"] for r in arm["main"]],
                "main_failed": [r["failed"] for r in arm["main"]],
                "branch_medians_ns": medians["branch"], "main_medians_ns": medians["main"],
                "branch_p99_ns": [r["p99_ns"] for r in arm["branch"]], "main_p99_ns": [r["p99_ns"] for r in arm["main"]],
                "paired_diff_median_ns": diff, "noise_floor_ns": floor, "p99_floor_ns": p99_floor,
                "branch_pooled_p99_ns": p99["branch"], "main_pooled_p99_ns": p99["main"]}
        cells.append(cell)
        print("A16 cell " + json.dumps(cell), flush=True)
shutil.rmtree(runs, ignore_errors=True)
if mode == "count":
    print(f"A16 count PASS: no branch failure; main refused {sum(sum(c['main_failed']) for c in cells)} reads in {len(cells)} cells")
else:
    print(f"A16 {'PASS' if ok else 'FAIL'}: paired difference and pooled branch p99 within main's plus their floors in {len(cells)} cells")
sys.exit(0 if ok else 1)
PY
}

case ${1:-} in
prepare) prepare "${2:?OUT}" ;;
measure | count) measure "$1" "${2:?OUT}" "${@:3}" ;;
*) echo "usage: $0 prepare OUT | measure OUT [stopped|running] | count OUT [stopped|running]" >&2; exit 2 ;;
esac
