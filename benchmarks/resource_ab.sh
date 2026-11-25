#!/bin/bash
# EX-1 Decisions 3 (D162): allocators at two revisions, A (base) and B (candidate), through
# allocators' own benches (bench.cc, pool_bench.cc, resources_bench.cc and tracked_bench.cc, the
# tracked global new/delete from several threads) and Detent's accounting hot paths
# (resource_ownership_bench.cc, resource_retirement_bench.cc), with A again as the A/A control.
#   resource_ab.sh prepare OUT   light, with --ref ALLOC_A=allocators@A --ref ALLOC_B=allocators@B:
#       builds every bench per revision (Release, this checkout's Detent for its benches).
#   resource_ab.sh measure OUT   strict: rounds of A, B and A again (rotated); per row the median
#       B/A and A/A ratios; a row regresses when B/A - 1 exceeds the largest A/A deviation.
set -euo pipefail

prepare() {
	local detent=$PWD out
	mkdir -p "$1"
	out=$(cd "$1" && pwd)
	for arm in a b; do
		local src build
		if [ "$arm" = a ]; then src=${JOB_REF_ALLOC_A:?--ref ALLOC_A}; else src=${JOB_REF_ALLOC_B:?--ref ALLOC_B}; fi
		rm -rf "${out:?}/$arm"
		mkdir -p "$out/$arm"
		git -C "$src" rev-parse HEAD > "$out/$arm/allocators.txt"
		build=$out/$arm/allocators-build
		cmake -S "$src" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null
		cmake --build "$build" -j4 --target allocators_bench allocators_pool_bench allocators_resources_bench \
			allocators_tracked_bench
		build=$out/$arm/detent-build
		cmake -S "$detent" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DFETCHCONTENT_SOURCE_DIR_ALLOCATORS="$src" \
			-DDETENT_BUILD_AUTHORITY=OFF -DDETENT_BUILD_RELAY=OFF -DDETENT_BUILD_MANAGED_AGENT=OFF \
			-DDETENT_BUILD_EXAMPLES=OFF -DDETENT_BUILD_TESTS=OFF -DDETENT_BUILD_BENCHMARKS=ON >/dev/null
		cmake --build "$build" -j4 --target detent_resource_ownership_bench detent_resource_retirement_bench
		for b in allocators-build/allocators_bench allocators-build/allocators_pool_bench \
			allocators-build/allocators_resources_bench allocators-build/allocators_tracked_bench \
			detent-build/benchmarks/detent_resource_ownership_bench detent-build/benchmarks/detent_resource_retirement_bench; do
			cp "$out/$arm/$b" "$out/$arm/"
		done
	done
}

measure() {
	/usr/local/bin/python3 -I - "$@" <<'PY'
import json, re, statistics, subprocess, sys
from pathlib import Path

out = Path(sys.argv[1]).resolve()
ROUNDS = 5
ORDERS = [("a", "b", "again"), ("b", "again", "a"), ("again", "a", "b")]
BENCHES = ["allocators_bench", "allocators_pool_bench", "allocators_resources_bench", "allocators_tracked_bench",
           "detent_resource_ownership_bench", "detent_resource_retirement_bench"]


def rows(bench, text):
    found = {}
    for line in text.splitlines():
        if m := re.match(r"\s*(.+?)\s+n=(\d+)\s+allocators\s+([\d.]+) ns", line):
            found[f"{bench}: {m[1]} n={m[2]}"] = float(m[3])
        elif m := re.match(r"(.+?) ([\d.]+) ns$", line):
            found[f"{bench}: {m[1]}"] = float(m[2])
        elif m := re.match(r"(\S+) operations=(\d+) seconds=([\d.e-]+) cpu_seconds", line):
            found[f"{bench}: {m[1]}"] = float(m[3]) / int(m[2]) * 1e9
        elif line.startswith("owners="):
            fields = dict(x.split("=") for x in line.split())
            for phase in ("creation", "release", "drain"):
                found[f"{bench}: retirement {phase} per owner"] = float(fields[f"{phase}_seconds"]) / int(fields["owners"]) * 1e9
    return found


samples = {}
for index in range(ROUNDS):
    for arm in ORDERS[index % 3]:
        for bench in BENCHES:
            binary = out / ("a" if arm == "again" else arm) / bench
            text = subprocess.run([binary], check=True, capture_output=True, text=True, timeout=300).stdout
            for row, value in rows(bench, text).items():
                samples.setdefault(row, {}).setdefault(arm, []).append(value)
ok, table = True, []
for row, arms in samples.items():
    ba = [b / a for a, b in zip(arms["a"], arms["b"])]
    aa = [x / a for a, x in zip(arms["a"], arms["again"])]
    noise = max(abs(r - 1) for r in aa)
    ratio = statistics.median(ba)
    regressed = ratio - 1 > noise
    ok &= not regressed
    entry = {"row": row, "a_ns": round(statistics.median(arms["a"]), 3), "b_ns": round(statistics.median(arms["b"]), 3),
             "b_over_a": round(ratio, 4), "aa_median": round(statistics.median(aa), 4), "aa_noise": round(noise, 4),
             "regressed": regressed}
    table.append(entry)
    print("AB row " + json.dumps(entry), flush=True)
print(f"AB {'PASS' if ok else 'FAIL'}: {sum(e['regressed'] for e in table)} of {len(table)} rows regress beyond the A/A noise")
sys.exit(0 if ok else 1)
PY
}

case ${1:-} in
prepare) prepare "${2:?OUT}" ;;
measure) measure "${2:?OUT}" ;;
*) echo "usage: $0 prepare OUT | measure OUT" >&2; exit 2 ;;
esac
