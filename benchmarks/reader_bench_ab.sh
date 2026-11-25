#!/bin/bash
# Interleaved reader_bench A/B (quest first-read-1ms): main ($JOB_REF_BASE) against this checkout's
# warm core lookups, int, hot and scattered, 1 and 8 threads.
#   reader_bench_ab.sh build OUT          light: Release reader_bench in both trees, each tree's store
#                                         (--generate 50000: 100K knobs over 64 namespaces), under OUT
#   reader_bench_ab.sh run OUT [PAIRS]    strict: PAIRS base/head pairs per thread count, alternating
#                                         which goes first; prints each case's median p50 and p99 in ns
#   reader_bench_ab.sh find OUT [PAIRS]   strict: table_find_bench (built with reader_bench; main's tree
#                                         gets the target for the build only), PAIRS alternating pairs;
#                                         prints main's decoded find against the head's paged find
set -euo pipefail

build() {
	local out code src
	mkdir -p "$1"
	out=$(cd "$1" && pwd)
	for code in base head; do
		if [ "$code" = base ]; then src=${JOB_REF_BASE:?--ref base=main}; else src=$PWD; fi
		cmake -S "$src" -B "$src/.scratch/reader-bench-build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
			-DDETENT_BUILD_AUTHORITY=OFF -DDETENT_BUILD_RELAY=OFF -DDETENT_BUILD_EXAMPLES=OFF \
			-DDETENT_BUILD_BENCHMARKS=ON -DDETENT_BUILD_TESTS=ON -DDETENT_READ_COUNTERS=OFF >/dev/null
		cmake --build "$src/.scratch/reader-bench-build" -j4 --target reader_bench
		if [ "$code" = base ]; then
			cp "$src/benchmarks/CMakeLists.txt" "$out/base-CMakeLists.txt"
			cp "$PWD/benchmarks/table_find_bench.cc" "$src/benchmarks/"
			printf 'add_executable(table_find_bench table_find_bench.cc)\ntarget_link_libraries(table_find_bench PRIVATE detent::local_view)\n' >> "$src/benchmarks/CMakeLists.txt"
		fi
		cmake --build "$src/.scratch/reader-bench-build" -j4 --target table_find_bench
		if [ "$code" = base ]; then
			cp "$out/base-CMakeLists.txt" "$src/benchmarks/CMakeLists.txt"
			rm "$src/benchmarks/table_find_bench.cc"
		fi
		rm -rf "${out:?}/$code"
		mkdir -p "$out/$code"
		cp "$src/.scratch/reader-bench-build/benchmarks/reader_bench" \
			"$src/.scratch/reader-bench-build/benchmarks/table_find_bench" "$out/$code/"
		git -C "$src" rev-parse HEAD > "$out/$code/commit.txt"
		"$out/$code/reader_bench" --store "$out/$code/store" --generate 50000
	done
	file "$out"/*/reader_bench
}

run() {
	/usr/local/bin/python3 -I - "$@" <<'PY'
import json, statistics, subprocess, sys
from pathlib import Path
out, pairs = Path(sys.argv[1]).resolve(), int(sys.argv[2]) if len(sys.argv) > 2 else 6
results = {}
for threads in (1, 8):
    for pair in range(pairs):
        for code in (("head", "base") if pair % 2 else ("base", "head")):
            store = out / code / "store"
            lines = subprocess.run([out / code / "reader_bench", "--store", store, "--keys", f"{store}.keys",
                                    "--operation", "int", "--reader", "core", "--threads", str(threads),
                                    "--calls", "1000000", "--repetitions", "1"],
                                   check=True, capture_output=True, text=True).stdout.splitlines()
            for line in lines:
                row = json.loads(line)
                if "case" not in row:
                    continue
                case = row["case"].rsplit("/rep", 1)[0]
                per = row["per_thread"]
                key = (case, threads)
                results.setdefault(key, {}).setdefault(code, []).append(
                    (statistics.median(t["p50_ns"] for t in per), statistics.median(t["p99_ns"] for t in per)))
                print(json.dumps({"code": code, "pair": pair, "case": case, "threads": threads,
                                  "p50_ns": results[key][code][-1][0], "p99_ns": results[key][code][-1][1]}), flush=True)
for (case, threads), arms in sorted(results.items()):
    summary = {code: [statistics.median(r[0] for r in rows), statistics.median(r[1] for r in rows)]
               for code, rows in arms.items()}
    diffs = [h[0] - b[0] for h, b in zip(arms["head"], arms["base"])]
    print("reader_bench " + json.dumps({"case": case, "threads": threads, "base_p50_p99": summary["base"],
                                        "head_p50_p99": summary["head"], "paired_p50_diff": statistics.median(diffs)}))
PY
}

find_cost() {
	/usr/local/bin/python3 -I - "$@" <<'PY'
import json, statistics, subprocess, sys
from pathlib import Path
out, pairs = Path(sys.argv[1]).resolve(), int(sys.argv[2]) if len(sys.argv) > 2 else 10
rows = {}
for pair in range(pairs):
    for code in (("head", "base") if pair % 2 else ("base", "head")):
        for line in subprocess.run([out / code / "table_find_bench"], check=True, capture_output=True,
                                   text=True).stdout.splitlines():
            row = json.loads(line)
            rows.setdefault((code, row["case"]), []).append((row["present_ns"], row["absent_ns"]))
            print(json.dumps({"code": code, "pair": pair, **row}), flush=True)
def median(key):
    return [statistics.median(r[0] for r in rows[key]), statistics.median(r[1] for r in rows[key])]
for width in (16, 40):
    main, paged = median(("base", f"decoded/{width}")), median(("head", f"paged/{width}"))
    print("table_find " + json.dumps({"width": width, "main_decoded_present_absent": main,
                                      "head_decoded_present_absent": median(("head", f"decoded/{width}")),
                                      "head_paged_present_absent": paged,
                                      "paged_over_main": [round(p / m, 2) for p, m in zip(paged, main)]}))
PY
}

case ${1:-} in
build) build "${2:?OUT}" ;;
run) run "${2:?OUT}" "${@:3}" ;;
find) find_cost "${2:?OUT}" "${@:3}" ;;
*) echo "usage: $0 build OUT | run OUT [PAIRS] | find OUT [PAIRS]" >&2; exit 2 ;;
esac
