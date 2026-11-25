#!/bin/bash
# EX-1 qualification of this branch (allocators' accounting) against main (Detent's own copies). Each
# arm is the same program built from its own tree; main runs twice where timing is compared (A/A).
#   resource_qualification.sh prepare OUT    light, with --ref BASELINE=main: builds both trees (pinned
#       LLVM 23.1.2, Release, DETENT_READ_COUNTERS=OFF) and their probes; one store per namespace count.
#   resource_qualification.sh bytes OUT      light (A5, A14 bytes): captured wire payloads, the stores'
#       files and the authority's durable bytes must be identical across the trees.
#   resource_qualification.sh first-read OUT exclusive (A7): reader core and Client first reads, six
#       rounds of main, branch and main again (rotated), 200 reads each; the median paired difference
#       (branch minus main) must lie within the A/A band.
#   resource_qualification.sh costs OUT      exclusive (A13, A14 timing): accounting and retirement
#       benches, account footprint and the authority's durable acknowledgment, five rotated rounds.
#   resource_qualification.sh targets OUT    light (A12), with --ref BASELINE=main: configures both trees
#       at their defaults and with every component; allocators fetches nothing, keeps its tracked
#       global new/delete off, and no target compiles or links the legacy allocators library.
set -euo pipefail
llvm=/usr/local/opt/llvm/bin
sdk=$(sed -n 's/^-isysroot //p' "$("$llvm/clang++" --version | sed -n 's/^Configuration file: //p')")
knobs=10000
probes="first_read_phases first_read_core rq_wire_capture rq_ownership rq_retirement detent_authority_durable_bench"

prepare() {
	local branch=$PWD out
	mkdir -p "$1"
	out=$(cd "$1" && pwd)
	"$llvm/clang++" --version | tee "$out/toolchain.txt"
	grep -q 'clang version 23.1.2' "$out/toolchain.txt"
	for code in main branch; do
		local src build
		if [ "$code" = main ]; then src=${JOB_REF_BASELINE:?--ref BASELINE=main}; else src=$branch; fi
		mkdir -p "$out/$code"
		git -C "$src" rev-parse HEAD > "$out/$code/commit.txt"
		# The wire capture is each tree's own encoded-update fixture, writing every decoded result.
		/usr/local/bin/python3 -I - "$src/tests/detent_distribution/test_encoded_update.cc" "$out/$code/capture.cc" <<'PY'
import sys
text = open(sys.argv[1]).read()
marker = "\tdecoder.finish();\n\treturn result;\n}\n"
assert text.count(marker) == 1
capture = """\tdecoder.finish();
\tstatic unsigned sequence = 0;
\tstd::ofstream file(std::string(std::getenv("DETENT_CAPTURE_DIR")) + "/" + std::to_string(sequence++) + ".wire",
\t\t\t\t\t   std::ios::binary);
\tfile.write(result.data(), static_cast<std::streamsize>(result.size()));
\tif (!file)
\t\tthrow std::runtime_error("wire capture failed");
\treturn result;
}
"""
open(sys.argv[2], "w").write("#include <cstdlib>\n#include <fstream>\n" + text.replace(marker, capture))
PY
		cat > "$out/$code/targets.cmake" <<EOF
add_executable(first_read_phases "$branch/tests/detent_system/first_read_phases.cc")
target_include_directories(first_read_phases PRIVATE "\${PROJECT_SOURCE_DIR}/src/detent_local")
target_link_libraries(first_read_phases PRIVATE detent::local_client detent::testing_store)
add_executable(first_read_core "$branch/tests/detent_system/first_read_phases.cc")
target_compile_definitions(first_read_core PRIVATE FIRST_READ_CORE=1)
target_link_libraries(first_read_core PRIVATE detent::reader)
add_executable(rq_wire_capture "$out/$code/capture.cc")
target_include_directories(rq_wire_capture PRIVATE "\${PROJECT_SOURCE_DIR}/tests/detent_distribution")
target_link_libraries(rq_wire_capture PRIVATE detent::distribution)
add_executable(rq_ownership "$branch/benchmarks/resource_ownership_bench.cc")
target_link_libraries(rq_ownership PRIVATE detent::resources)
add_executable(rq_retirement "$branch/benchmarks/resource_retirement_bench.cc")
target_link_libraries(rq_retirement PRIVATE detent::resources)
EOF
		echo "cmake_language(DEFER CALL include \"$out/$code/targets.cmake\")" > "$out/$code/defer.cmake"
		build=$src/.scratch/resource-qualification-build
		cmake -S "$src" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PROJECT_detent_INCLUDE="$out/$code/defer.cmake" \
			-DCMAKE_CXX_COMPILER="$llvm/clang++" -DCMAKE_C_COMPILER="$llvm/clang" -DCMAKE_OSX_SYSROOT="$sdk" \
			-DDETENT_BUILD_AUTHORITY=ON -DDETENT_BUILD_RELAY=OFF -DDETENT_BUILD_EXAMPLES=OFF \
			-DDETENT_BUILD_BENCHMARKS=ON -DDETENT_BUILD_TESTS=ON -DDETENT_READ_COUNTERS=OFF >/dev/null
		grep -Eq '^CMAKE_BUILD_TYPE:[A-Z]+=Release$' "$build/CMakeCache.txt"
		grep -Eq '^DETENT_READ_COUNTERS:[A-Z]+=OFF$' "$build/CMakeCache.txt"
		# shellcheck disable=SC2086
		cmake --build "$build" -j4 --target $probes
		for probe in $probes; do cp "$(find "$build" -name "$probe" -type f -perm -u+x | head -1)" "$out/$code/"; done
	done
	# One store, written by main's writer, serves every first-read arm: the bytes read are identical.
	for namespaces in 1 1000; do
		rm -rf "$out/store-${namespaces}ns"
		"$out/main/first_read_phases" --generate "$out/store-${namespaces}ns" "$namespaces" "$knobs" >/dev/null
	done
}

python() { /usr/local/bin/python3 -I - "$@"; }

bytes() {
	python "$1" <<'PY'
import hashlib, json, shutil, subprocess, sys
from pathlib import Path

out = Path(sys.argv[1]).resolve()
work = out / "bytes"


def tree(root):
    return {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(root.rglob("*")) if p.is_file()}


def arm(code, label):
    base = work / label
    shutil.rmtree(base, ignore_errors=True)
    (base / "wire").mkdir(parents=True)
    subprocess.run([out / code / "rq_wire_capture"], check=True, capture_output=True,
                   env={"DETENT_CAPTURE_DIR": str(base / "wire")})
    result = {"wire": tree(base / "wire")}
    for namespaces in (1, 1000):
        store = base / f"store-{namespaces}ns"
        subprocess.run([out / code / "first_read_phases", "--generate", store, str(namespaces), "10000"],
                       check=True, capture_output=True)
        result[f"store-{namespaces}ns"] = tree(store)
    durable = json.loads(subprocess.run([out / code / "detent_authority_durable_bench", base / "authority", "20000"],
                                        check=True, capture_output=True, text=True).stdout.strip().splitlines()[-1])
    result["authority"] = {k: v for k, v in durable.items() if "bytes" in k and k != "peak_rss_bytes"}
    return result


main, again, branch = arm("main", "main"), arm("main", "again"), arm("branch", "branch")
shutil.rmtree(work, ignore_errors=True)
ok = True
for part in main:
    stable = main[part] == again[part]
    same = main[part] == branch[part]
    ok &= same or not stable
    print("A5 " + json.dumps({"part": part, "entries": len(main[part]), "main_repeatable": stable,
                              "branch_equals_main": same,
                              **({"main": main[part], "branch": branch[part]} if part == "authority" else {})}))
print(f"A5 {'PASS' if ok else 'FAIL'}: every repeatable byte set of the branch equals main's")
sys.exit(0 if ok else 1)
PY
}

first_read() {
	python "$1" <<'PY'
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
        passed = paired <= band
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

costs() {
	python "$1" <<'PY'
import json, re, shutil, statistics, subprocess, sys
from pathlib import Path

out = Path(sys.argv[1]).resolve()
ROUNDS = 5
ORDERS = [("main", "branch", "again"), ("branch", "again", "main"), ("again", "main", "branch")]


def measure(code, index):
    binary = out / ("main" if code == "again" else code)
    rows = {}
    text = subprocess.run([binary / "rq_ownership"], check=True, capture_output=True, text=True).stdout
    for line in text.splitlines():
        if m := re.match(r"(\S+) operations=(\d+) seconds=([\d.e-]+) cpu_seconds", line):
            rows[f"{m[1]} ns"] = float(m[3]) / int(m[2]) * 1e9
        elif line.startswith(("account_bytes=", "account-heap")):
            for k, v in re.findall(r"(\w+)=(-?\d+)", line):
                rows[k] = float(v)
    text = subprocess.run([binary / "rq_retirement"], check=True, capture_output=True, text=True).stdout
    for k, v in re.findall(r"(\w+)=([\d.]+)", text):
        rows[f"retirement {k}"] = float(v)
    directory = out / "costs" / f"{code}-{index}"
    shutil.rmtree(directory, ignore_errors=True)
    directory.parent.mkdir(exist_ok=True)
    durable = json.loads(subprocess.run([binary / "detent_authority_durable_bench", directory, "20000"],
                                        check=True, capture_output=True, text=True).stdout.strip().splitlines()[-1])
    shutil.rmtree(directory, ignore_errors=True)
    for k in ("durable_ack_ms_per_transaction", "durable_cpu_ms_per_transaction", "load_ms", "checkpoint_ms",
              "peak_rss_bytes"):
        rows[f"authority {k}"] = float(durable[k])
    return rows


samples = {}
for index in range(ROUNDS):
    for code in ORDERS[index % 3]:
        for row, value in measure(code, index).items():
            samples.setdefault(row, {}).setdefault(code, []).append(value)
shutil.rmtree(out / "costs", ignore_errors=True)
for row, arms in samples.items():
    m, b, a = (statistics.median(arms[c]) for c in ("main", "branch", "again"))
    print("COST " + json.dumps({"row": row, "main": m, "branch": b, "again": a,
                                "branch_over_main": round(b / m, 4) if m else None,
                                "again_over_main": round(a / m, 4) if m else None}))
PY
}

targets() {
	local branch=$PWD out ok=1
	mkdir -p "$1"
	out=$(cd "$1" && pwd)
	for profile in default all; do
		local flags=()
		[ "$profile" = all ] && flags=(-DDETENT_BUILD_AUTHORITY=ON -DDETENT_BUILD_RELAY=ON -DDETENT_BUILD_MANAGED_AGENT=ON
			-DDETENT_BUILD_TESTS=ON -DDETENT_BUILD_BENCHMARKS=ON)
		for code in main branch; do
			local src build
			if [ "$code" = main ]; then src=${JOB_REF_BASELINE:?--ref BASELINE=main}; else src=$branch; fi
			build=$out/targets-$code-$profile
			rm -rf "$build"
			cmake -S "$src" -B "$build" -G Ninja "${flags[@]}" >/dev/null
			(cd "$build/_deps" && command ls -d -- *-src | sort) > "$build.deps"
			echo "$profile $code fetches: $(tr '\n' ' ' < "$build.deps")"
		done
		local added
		added=$(comm -13 "$out/targets-main-$profile.deps" "$out/targets-branch-$profile.deps" | grep -vx allocators-src || true)
		[ -z "$added" ] || { echo "A12 $profile: unrelated fetches: $added"; ok=0; }
		build=$out/targets-branch-$profile
		grep -Eq '^ALLOCATORS_TRACKED_MEM:BOOL=OFF$' "$build/CMakeCache.txt" || { echo "A12 $profile: tracked memory on"; ok=0; }
		! grep -rqi 'FetchContent_Declare' "$build/_deps/allocators-src/CMakeLists.txt" || { echo "A12: allocators fetches"; ok=0; }
		if ninja -C "$build" -t commands all | grep -Eq 'allocators\.cc|liballocators(_tracked)?\.a'; then
			echo "A12 $profile: the legacy allocators library is built or linked"; ok=0
		fi
		echo "A12 $profile: $(ninja -C "$build" -t commands all | grep -c 'allocators-src/retirement.cc') compile of allocators' retirement.cc"
	done
	[ $ok = 1 ] && echo "A12 PASS" || { echo "A12 FAIL"; return 1; }
}

case ${1:-} in
targets) targets "${2:?OUT}" ;;
prepare) prepare "${2:?OUT}" ;;
bytes) bytes "${2:?OUT}" ;;
first-read) first_read "${2:?OUT}" ;;
costs) costs "${2:?OUT}" ;;
*) echo "usage: $0 prepare|bytes|first-read|costs|targets OUT" >&2; exit 2 ;;
esac
