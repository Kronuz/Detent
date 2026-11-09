#!/bin/bash
# The counting-cost benchmark of docs/design/observability.md D2, run as campaign jobs from the checkout
# root on one slot, with the reader S6 commit as a ref:
#   observability-bench.sh build                      (light: builds observability_bench against $JOB_REF_S6)
#   observability-bench.sh run smoke|screen|confirm   (strict or exclusive: runs it and summarizes)
set -euo pipefail
here=$(pwd)
work=$here/.scratch/obs
bin=$work/build/benchmarks/observability_bench
s6=${JOB_REF_S6:?--ref s6=COMMIT is required}
s6_commit=$(git -C "$s6" rev-parse HEAD)
source_digest=$(shasum -a 256 docs/design/observability_bench.cc | cut -d' ' -f1)
case "${1:-}" in
build)
	mkdir -p "$work"
	rsync -a --delete --exclude .git --exclude .scratch "$s6"/ "$work/s6-src/"
	cp docs/design/observability_bench.cc "$work/s6-src/benchmarks/"
	cat >>"$work/s6-src/benchmarks/CMakeLists.txt" <<'CMAKE'
# docs/design/observability.md D2; appended by observability-bench.sh, not part of the S6 tree.
if(NOT TARGET metrics::metrics)
  FetchContent_MakeAvailable(metrics)
endif()
add_executable(observability_bench observability_bench.cc)
target_link_libraries(observability_bench PRIVATE detent::reader detent::testing_store metrics::metrics)
CMAKE
	cmake -S "$work/s6-src" -B "$work/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_CXX_COMPILER=/usr/bin/clang++ -DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_OSX_SYSROOT= \
		-DDETENT_BUILD_AUTHORITY=OFF -DDETENT_BUILD_RELAY=OFF -DDETENT_BUILD_EXAMPLES=OFF \
		-DDETENT_BUILD_TESTS=OFF -DDETENT_BUILD_BENCHMARKS=ON
	cmake --build "$work/build" --target observability_bench
	file "$bin"
	printf '%s %s\n' "$s6_commit" "$source_digest" >"$work/build/STAMP"
	cat "$work/build/STAMP"
	;;
run)
	mode=${2:?smoke, screen or confirm}
	[ -x "$bin" ] || { echo "no binary at $bin: run the build job on this slot first" >&2; exit 1; }
	[ "$(cat "$work/build/STAMP")" = "$s6_commit $source_digest" ] ||
		{ echo "stale binary: $(cat "$work/build/STAMP") is not $s6_commit $source_digest" >&2; exit 1; }
	file "$bin"
	case "$mode" in
	smoke) args=(--knobs 20000 --reps 1 --calls 16384 --scattered 256 --soak 2) ;;
	screen) args=(--knobs 100000 --reps 3 --calls 131072 --scattered 1024 --soak 10) ;;
	confirm) args=(--knobs 100000 --reps 7 --calls 262144 --scattered 1024 --soak 60) ;;
	*) echo "unknown mode $mode" >&2; exit 2 ;;
	esac
	out=${JOB_OUT:-$work/out}
	mkdir -p "$out"
	store=$work/store-$mode map=$work/map-$mode
	rm -rf "$store" "$map"
	mkdir -p "$map"
	echo "s6 $s6_commit; bench $source_digest; host $(sysctl -n machdep.cpu.brand_string 2>/dev/null || uname -m); $(date -u +%FT%TZ)"
	"$bin" --store "$store" --map-dir "$map" "${args[@]}" | tee "$out/$mode.jsonl"
	"${PYTHON:-python3}" docs/design/observability-summary.py "$out/$mode.jsonl" | tee "$out/$mode.md"
	rm -rf "$store" "$map"
	;;
*)
	echo "usage: $0 build | run smoke|screen|confirm" >&2
	exit 2
	;;
esac
