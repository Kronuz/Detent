#!/bin/bash
# A3: the new harness drives the cores exactly as the old one. equiv.sh [SEEDS]: builds both
# harnesses with trace.py's hash (asserts-on -O2), runs old, new, and new with LIVENESS_VERIFY=1
# (every held Tick stepped and checked), and fails unless the three outputs (stats and TRACE
# lines per profile) are identical and the verify run passes. Prints plain Release times too.
source "$(dirname "$0")/common.sh" || exit 1
seeds=${1:-20}; rc=0
setup release || exit 1
for v in old new; do
	python3 -I "$here/trace.py" "$OUT/src/$v.cc" "$OUT/src/$v-trace.cc" || exit 1
	cxx "$src/include" "$OUT/src/$v-trace.cc" "$OUT/$v-trace" || exit 1
	cxx "$src/include" "$OUT/src/$v.cc" "$OUT/$v" || exit 1
done
for v in old new; do
	t0=$SECONDS; "$OUT/$v" "$seeds" > "$OUT/$v.log" 2>&1 || rc=1; echo "PLAIN $v: $((SECONDS - t0)) s, $(tail -n 1 "$OUT/$v.log")"
done
"$OUT/old-trace" "$seeds" > "$OUT/old-trace.log" 2>&1 || rc=1
"$OUT/new-trace" "$seeds" > "$OUT/new-trace.log" 2>&1 || rc=1
LIVENESS_VERIFY=1 "$OUT/new-trace" "$seeds" > "$OUT/verify-trace.log" 2>&1 || { echo "verify run failed"; rc=1; }
grep TRACE "$OUT/old-trace.log"
diff "$OUT/old-trace.log" "$OUT/new-trace.log" && echo "old == new" || { echo "old != new"; rc=1; }
diff "$OUT/old-trace.log" "$OUT/verify-trace.log" && echo "old == verify" || { echo "old != verify"; rc=1; }
exit $rc
