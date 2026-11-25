#!/bin/bash
# The pin wave's hot-path A/B (quest pin-wave, A6): old pins against new, interleaved with an A/A
# control (tools/w5/ab_rounds.py), reported by cpp-libraries' tools/ab_compare.py.
#   pin-wave-ab.sh build OUT          light, in the new tree's checkout with --ref OLD=<old commit>:
#       builds both trees (Release, /usr/bin/clang++) and copies the measured binaries and stores
#       to OUT/old and OUT/new.
#   pin-wave-ab.sh row KIND OUT ROUNDS   strict, at most ten minutes: one row, saved under
#       OUT/rows/KIND (or $JOB_OUT/KIND), its report printed.
# KIND: durable (daemon publish: detent_authority_durable_bench, real fsyncs), update (the authority
# state machine), get (detent_authority_read_response_bench), reader (reader_bench, warm lookups),
# first-read (first_read_core: detent_open and the first detent_get of a fresh process, 40 reads a run).
set -uo pipefail
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
ab=${AB_COMPARE:-$HOME/code/Kronuz/cpp-libraries/tools/ab_compare.py}

build() {
	local out src arm build
	mkdir -p "$1" && out=$(cd "$1" && pwd)
	cat > "$out/targets.cmake" <<EOF
add_executable(first_read_core "$root/tests/detent_system/first_read_phases.cc")
target_compile_definitions(first_read_core PRIVATE FIRST_READ_CORE=1)
target_link_libraries(first_read_core PRIVATE detent::reader)
add_executable(first_read_phases "$root/tests/detent_system/first_read_phases.cc")
target_include_directories(first_read_phases PRIVATE "\${PROJECT_SOURCE_DIR}/src/detent_local")
target_link_libraries(first_read_phases PRIVATE detent::local_view detent::testing_store)
EOF
	echo "cmake_language(DEFER CALL include \"$out/targets.cmake\")" > "$out/defer.cmake"
	for arm in old new; do
		if [ $arm = old ]; then src=${JOB_REF_OLD:?--ref OLD=<old commit>}; else src=$root; fi
		build=$src/.scratch/pin-wave-ab
		rm -rf "${out:?}/$arm" && mkdir -p "$out/$arm/benchmarks"
		git -C "$src" rev-parse HEAD > "$out/$arm/commit.txt"
		cmake -S "$src" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PROJECT_detent_INCLUDE="$out/defer.cmake" \
			-DCMAKE_CXX_COMPILER=/usr/bin/clang++ -DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_OSX_SYSROOT= \
			-DDETENT_BUILD_AUTHORITY=ON -DDETENT_BUILD_RELAY=ON -DDETENT_BUILD_MANAGED_AGENT=ON -DDETENT_BUILD_EXAMPLES=OFF \
			-DDETENT_BUILD_BENCHMARKS=ON -DDETENT_BUILD_TESTS=ON >"$out/$arm/configure.log" || { tail -30 "$out/$arm/configure.log"; return 1; }
		cmake --build "$build" -j4 --target detent_authority_durable_bench detent_authority_bench \
			detent_authority_read_response_bench reader_bench first_read_core first_read_phases || return 1
		for t in detent_authority_durable_bench detent_authority_bench detent_authority_read_response_bench reader_bench; do
			cp "$build/benchmarks/$t" "$out/$arm/benchmarks/$t" || return 1
		done
		cp "$build/first_read_core" "$build/first_read_phases" "$out/$arm/" || return 1
		"$out/$arm/benchmarks/reader_bench" --store "$out/$arm/reader-store" --generate 10000 || return 1
		"$out/$arm/first_read_phases" --generate "$out/$arm/first-read-store" 1 10000 > "$out/$arm/first-read-store.json" || return 1
		file "$out/$arm/benchmarks/"* "$out/$arm/first_read_core"
	done
}

reader_rows='
import json, statistics, sys
for line in sys.stdin:
    o = json.loads(line) if line.startswith("{") else {}
    for k in ("p50_ns", "p99_ns") if "case" in o else ():
        print(o["case"] + "." + k, statistics.median(t[k] for t in o["per_thread"]), "ns")'

# One run of one arm, as unit rows (`name value unit`).
run() {
	local arm=$1 kind=$2 out=$3 dir
	case "$kind" in
	durable) W5_SCRATCH=$out/scratch "$root/tools/w5/bench_rows.sh" "$out/$arm" authority-durable ;;
	update) W5_SCRATCH=$out/scratch "$root/tools/w5/bench_rows.sh" "$out/$arm" authority-update ;;
	get) W5_SCRATCH=$out/scratch "$root/tools/w5/bench_rows.sh" "$out/$arm" authority-read ;;
	reader)
		"$out/$arm/benchmarks/reader_bench" --store "$out/$arm/reader-store" --keys "$out/$arm/reader-store.keys" \
			--reader core --threads 1 --repetitions 5 |
			python3 -I -c "$reader_rows"
		;;
	first-read)
		python3 -I - "$out/$arm/first_read_core" "$out/$arm/first-read-store" "$out/scratch/$arm" <<'PY'
import json, shutil, statistics, subprocess, sys
from pathlib import Path
exe, store, work = sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3])
shutil.rmtree(work, ignore_errors=True)
subprocess.run(["cp", "-cR", store, work], check=True)
for path in work.rglob("*"):
    if path.is_file():
        path.read_bytes()
totals = []
for j in range(45):
    i = (j * 7919) % 10000
    r = subprocess.run([exe, "--core", work, f"n0:k{i}", str(3 * i + 1)], capture_output=True, text=True, timeout=10, check=True)
    if j >= 5:
        totals.append(json.loads(r.stdout.strip().splitlines()[-1])["total_ns"])
shutil.rmtree(work, ignore_errors=True)
totals.sort()
print(f"first_read.median {statistics.median(totals)} ns")
print(f"first_read.p90 {totals[int(len(totals) * 0.9) - 1]} ns")
PY
		;;
	*) echo "unknown kind $kind" >&2; return 2 ;;
	esac
}

row() {
	local kind=$1 out rounds=$3 dir
	out=$(cd "$2" && pwd)
	dir=${JOB_OUT:-$out/rows}/$kind
	mkdir -p "$out/scratch" "$dir"
	cat "$out/old/commit.txt" "$out/new/commit.txt"
	python3 -I "$root/tools/w5/ab_rounds.py" --a "$0 run old $kind $out" --b "$0 run new $kind $out" \
		--rounds "$rounds" --out "$dir" || return 2
	python3 -I "$ab" compare --format unit --a "$dir"/a/* --b "$dir"/b/* --aa "$dir"/aa/* --label-a old --label-b new | tee "$dir/report.txt"
}

case ${1:-} in
build) build "${2:?OUT}" ;;
row) row "${2:?KIND}" "${3:?OUT}" "${4:?ROUNDS}" ;;
run) run "${2:?ARM}" "${3:?KIND}" "${4:?OUT}" ;;
*) echo "usage: $0 build OUT | row KIND OUT ROUNDS" >&2; exit 2 ;;
esac
