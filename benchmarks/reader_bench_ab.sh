#!/bin/bash
# Interleaved reader_bench A/B (quest first-read-1ms): main ($JOB_REF_BASE) against this checkout's
# warm core lookups, int, hot and scattered, 1 and 8 threads.
#   reader_bench_ab.sh build OUT          light: Release reader_bench in both trees, each tree's store
#                                         (--generate 50000: 100K knobs over 64 namespaces), under OUT;
#                                         table_find_bench in each tree, and in $JOB_REF_BEFORE's when given
#   reader_bench_ab.sh run OUT [PAIRS]    strict: PAIRS base/head pairs per thread count, alternating
#                                         which goes first; prints each case's median p50 and p99 in ns
#   reader_bench_ab.sh find OUT [PAIRS]   strict: table_find_bench, PAIRS rounds of every arm (main, the
#                                         earlier commit when built, the head, and the head again as an
#                                         A/A), rotating which goes first; per width and case, each arm's
#                                         medians and the head's paired differences against each arm
set -euo pipefail

build() {
	local out code src
	mkdir -p "$1"
	out=$(cd "$1" && pwd)
	for code in base before head; do
		case $code in
		base) src=${JOB_REF_BASE:?--ref base=main} ;;
		before) src=${JOB_REF_BEFORE:-}; [ -n "$src" ] || continue ;;
		head) src=$PWD ;;
		esac
		cmake -S "$src" -B "$src/.scratch/reader-bench-build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
			-DDETENT_BUILD_AUTHORITY=OFF -DDETENT_BUILD_RELAY=OFF -DDETENT_BUILD_EXAMPLES=OFF \
			-DDETENT_BUILD_BENCHMARKS=ON -DDETENT_BUILD_TESTS=ON -DDETENT_READ_COUNTERS=OFF >/dev/null
		[ "$code" = before ] || cmake --build "$src/.scratch/reader-bench-build" -j4 --target reader_bench
		if [ "$code" != head ]; then
			# The head's bench source in each other tree, paged where the tree has PagedTable; restored after.
			cp "$src/benchmarks/CMakeLists.txt" "$out/$code-CMakeLists.txt"
			[ ! -f "$src/benchmarks/table_find_bench.cc" ] || cp "$src/benchmarks/table_find_bench.cc" "$out/$code-table_find_bench.cc"
			cp "$PWD/benchmarks/table_find_bench.cc" "$src/benchmarks/"
			if ! grep -q table_find_bench "$src/benchmarks/CMakeLists.txt"; then
				printf 'add_executable(table_find_bench table_find_bench.cc)\ntarget_link_libraries(table_find_bench PRIVATE detent::local_view)\n' >> "$src/benchmarks/CMakeLists.txt"
				! grep -q 'class PagedTable' "$src/include/detent_local/store_table.hh" ||
					printf 'target_compile_definitions(table_find_bench PRIVATE DETENT_PAGED_TABLE)\n' >> "$src/benchmarks/CMakeLists.txt"
			fi
		fi
		cmake --build "$src/.scratch/reader-bench-build" -j4 --target table_find_bench
		if [ "$code" != head ]; then
			cp "$out/$code-CMakeLists.txt" "$src/benchmarks/CMakeLists.txt"
			if [ -f "$out/$code-table_find_bench.cc" ]; then
				cp "$out/$code-table_find_bench.cc" "$src/benchmarks/table_find_bench.cc"
			else
				rm "$src/benchmarks/table_find_bench.cc"
			fi
		fi
		rm -rf "${out:?}/$code"
		mkdir -p "$out/$code"
		cp "$src/.scratch/reader-bench-build/benchmarks/table_find_bench" "$out/$code/"
		git -C "$src" rev-parse HEAD > "$out/$code/commit.txt"
		[ "$code" = before ] && continue
		cp "$src/.scratch/reader-bench-build/benchmarks/reader_bench" "$out/$code/"
		"$out/$code/reader_bench" --store "$out/$code/store" --generate 50000
	done
	file "$out"/*/table_find_bench
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
import itertools, json, statistics, subprocess, sys
from pathlib import Path
out, pairs = Path(sys.argv[1]).resolve(), int(sys.argv[2]) if len(sys.argv) > 2 else 10
# The head runs twice a round, as itself and as its own A/A arm: the floor of a difference.
arms = [code for code in ("base", "before", "head") if (out / code / "table_find_bench").exists()] + ["head-aa"]
rows = {}
for pair in range(pairs):
    for code in arms[pair % len(arms):] + arms[:pair % len(arms)]:
        binary = out / ("head" if code == "head-aa" else code) / "table_find_bench"
        for line in subprocess.run([binary], check=True, capture_output=True, text=True).stdout.splitlines():
            row = json.loads(line)
            for field in ("present_ns", "absent_ns"):
                rows.setdefault((code, row["case"], field), []).append(row[field])
            print(json.dumps({"code": code, "pair": pair, **row}), flush=True)


def spread(values):
    return statistics.median(abs(a - b) for a, b in itertools.pairwise(values))


# Each case of the head against each arm's same case and main's decoded find (main has no paged table).
for (code, case, field), head in sorted(rows.items()):
    if code != "head":
        continue
    kind, width = case.split("/")
    out_row = {"case": case, "name": field.removesuffix("_ns"), "head": round(statistics.median(head), 1)}
    for arm in arms:
        if arm == "head":
            continue
        other = rows.get((arm, case if arm != "base" else f"decoded/{width}", field))
        if not other:
            continue
        diff = statistics.median(h - o for h, o in zip(head, other))
        out_row[arm] = {"median": round(statistics.median(other), 1), "diff": round(diff, 1),
                        "floor": round(max(spread(head), spread(other)), 1)}
    print("table_find " + json.dumps(out_row))
PY
}

case ${1:-} in
build) build "${2:?OUT}" ;;
run) run "${2:?OUT}" "${@:3}" ;;
find) find_cost "${2:?OUT}" "${@:3}" ;;
*) echo "usage: $0 build OUT | run OUT [PAIRS] | find OUT [PAIRS]" >&2; exit 2 ;;
esac
