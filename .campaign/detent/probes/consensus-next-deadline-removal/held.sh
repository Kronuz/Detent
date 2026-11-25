#!/bin/bash
# A3: with every node's Tick holdable (a D3 removal, a witness leader and a non-voter included),
# the liveness harness drives the cores exactly as stepping every Tick. held.sh [SEEDS], in a
# consensus checkout: builds the harness stepped and held (variant.py), each with trace.py's hash
# (asserts-on -O2); runs stepped, held, and held with LIVENESS_VERIFY=1 (each held Tick stepped
# and checked: no action, the same deadline, nothing retired); fails unless the three outputs
# (stats and TRACE lines per profile) are identical, the verify run passes, and the held run held
# Ticks in every class in every profile.
mine="$(cd "$(dirname "$0")" && pwd)"
source "$mine/../consensus-liveness-asan-timeout/common.sh" || exit 1
seeds=${1:-20}; rc=0
setup release || exit 1
python3 -I "$here/trace.py" "$src/test/liveness_test.cc" "$OUT/src/traced.cc" || exit 1
for m in stepped held; do
	python3 -I "$mine/variant.py" "$OUT/src/traced.cc" "$OUT/src/$m.cc" "$m" || exit 1
	cxx "$src/include" "$OUT/src/$m.cc" "$OUT/$m" || exit 1
done
t0=$SECONDS; "$OUT/stepped" "$seeds" > "$OUT/stepped.log" 2>&1 || { echo "stepped run failed"; rc=1; }; echo "stepped: $((SECONDS - t0)) s"
t0=$SECONDS; "$OUT/held" "$seeds" > "$OUT/held.log" 2>&1 || { echo "held run failed"; rc=1; }; echo "held: $((SECONDS - t0)) s"
LIVENESS_VERIFY=1 "$OUT/held" "$seeds" > "$OUT/verify.log" 2>&1 || { echo "verify run failed"; grep FAIL "$OUT/verify.log" | sort | uniq -c | head; rc=1; }
grep -E '^(TRACE|HELD)' "$OUT/held.log"
for v in held verify; do
	if diff <(grep -v '^HELD' "$OUT/stepped.log") <(grep -v '^HELD' "$OUT/$v.log") > "$OUT/diff-$v.txt"; then echo "stepped == $v"
	else echo "stepped != $v"; head -20 "$OUT/diff-$v.txt"; rc=1; fi
done
while read -r _ _ leaving _ witness _ nonvoter name; do
	if [ "$leaving" = 0 ] || [ "$witness" = 0 ] || [ "$nonvoter" = 0 ]; then echo "$name: a class held no Tick"; rc=1; fi
done < <(grep '^HELD' "$OUT/held.log")
exit $rc
