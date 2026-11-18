#!/bin/bash
# A/B of the in-page walk cost: dense_walk_bench.cc at a baseline commit and at this tree.
#   dense_walk_ab.sh build OUT            light job with --ref BASELINE=<main>: Release builds of
#                                         the bench in both trees, each against its own sources.
#   dense_walk_ab.sh measure OUT [PAIRS]  exclusive job: PAIRS interleaved pairs (ABBA order), then
#                                         each metric's medians, B/A change and the A/A spread.
set -euo pipefail
rows=${DENSE_WALK_ROWS:-20000}
rounds=5

build() {
	local out=$1 here=$PWD
	mkdir -p "$out"
	for code in baseline candidate; do
		local src=$here
		[ "$code" = baseline ] && src=${JOB_REF_BASELINE:?--ref BASELINE}
		git -C "$src" rev-parse HEAD > "$out/$code.commit"
		[ "$src" = "$here" ] || cp "$here/tests/detent_local/dense_walk_bench.cc" "$src/tests/detent_local/"
		cat >> "$src/CMakeLists.txt" <<'EOF'

add_executable(dense_walk_bench tests/detent_local/dense_walk_bench.cc)
target_include_directories(dense_walk_bench PRIVATE "${PROJECT_SOURCE_DIR}/src/detent_local" "${PROJECT_SOURCE_DIR}/tests/detent_local")
target_link_libraries(dense_walk_bench PRIVATE detent::local_builder)
EOF
		cmake -S "$src" -B "$out/build-$code" -DCMAKE_BUILD_TYPE=Release \
			-DCMAKE_CXX_COMPILER=/usr/bin/clang++ -DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_OSX_SYSROOT= \
			-DDETENT_BUILD_AUTHORITY=OFF -DDETENT_BUILD_RELAY=OFF -DDETENT_BUILD_EXAMPLES=OFF \
			-DDETENT_BUILD_BENCHMARKS=OFF -DDETENT_BUILD_MANAGED_AGENT=OFF -DDETENT_BUILD_TESTS=ON \
			-DDETENT_READ_COUNTERS=OFF >/dev/null
		cmake --build "$out/build-$code" -j4 --target dense_walk_bench
		git -C "$src" checkout -- CMakeLists.txt
		[ "$src" = "$here" ] || rm "$src/tests/detent_local/dense_walk_bench.cc"
		cp "$out/build-$code/dense_walk_bench" "$out/$code"
		"$out/$code" 2000 1 >/dev/null
	done
}

measure() {
	local out=$1 pairs=${2:-10}
	: > "$out/runs.txt"
	for ((i = 0; i < pairs; ++i)); do
		local order="baseline candidate"
		((i % 2)) && order="candidate baseline"
		for code in $order; do
			"$out/$code" "$rows" "$rounds" 2>/dev/null | sed "s/^/code=$code pair=$i /" >> "$out/runs.txt"
		done
	done
	python3 -I - "$out/runs.txt" <<'EOF'
import statistics, sys
runs = {}
for line in open(sys.argv[1]):
    f = dict(kv.split("=") for kv in line.split())
    key = (f["code"], int(f["pair"]))
    for metric in ("lookup_ns", "verify_ns_per_page"):
        runs.setdefault(metric, {}).setdefault(key, []).append(float(f[metric]))
for metric, by_run in runs.items():
    per = {code: [statistics.median(v) for (c, _), v in sorted(by_run.items()) if c == code]
           for code in ("baseline", "candidate")}
    a, b = statistics.median(per["baseline"]), statistics.median(per["candidate"])
    # A/A: the baseline's own run-to-run spread, half its runs against the other half.
    half = len(per["baseline"]) // 2
    aa = statistics.median(per["baseline"][:half]) / statistics.median(per["baseline"][half:]) - 1
    print(f"{metric}: baseline {a:.2f} candidate {b:.2f} change {100 * (b / a - 1):+.2f}% "
          f"A/A {100 * aa:+.2f}% (runs {len(per['baseline'])}+{len(per['candidate'])})")
EOF
}

"$@"
