#!/bin/bash
# A1/A2: per-phase ASan time of the liveness test. phases.sh old|new|both [SEEDS]: builds the
# instrumented (instrument.py: wall time and events per profile and phase) and the plain ASan
# test of each harness, runs the plain one (as CI: SEEDS, default 20) and then the instrumented
# one, and prints wall seconds and the TIME table. Fails when a run fails, or with MAX_RATIO=N
# when a profile runs more than N times the default profile's events (the slow phase: idle
# windows written in default-profile milliseconds, run at a derived profile's 1-4 ms tick).
# BOUND=S fails the new (checkout's) plain run if longer than S seconds (half the ctest TIMEOUT: 90).
# PLAIN and INST name the harnesses to run plain and instrumented (default: the argument's).
source "$(dirname "$0")/common.sh" || exit 1
which=${1:-both}; seeds=${2:-20}; rc=0; alarm=400
setup asan || exit 1
vs=(old new); [ "$which" = both ] || vs=("$which")
for v in "${vs[@]}"; do
	python3 -I "$here/instrument.py" "$OUT/src/$v.cc" "$OUT/src/$v-inst.cc"
	cxx "$src/include" "$OUT/src/$v.cc" "$OUT/$v" || exit 1
	cxx "$src/include" "$OUT/src/$v-inst.cc" "$OUT/$v-inst" || exit 1
done
for v in ${PLAIN-${vs[*]}}; do
	read -r wall cpu prc < <(python3 -I -c '
import resource, subprocess, sys, time
t = time.time()
with open(sys.argv[2], "w") as out: rc = subprocess.run(["perl", "-e", "alarm shift; exec @ARGV", sys.argv[4], sys.argv[1], sys.argv[3]], stdout=out, stderr=subprocess.STDOUT).returncode
rc = 142 if rc in (-14, 142) else rc
r = resource.getrusage(resource.RUSAGE_CHILDREN)
print(f"{time.time() - t:.1f} {r.ru_utime + r.ru_stime:.1f} {rc}")' "$OUT/$v" "$OUT/$v.log" "$seeds" "$alarm")
	if [ "$prc" = 142 ]; then
		# The alarm killing the old (baseline) harness is a result; the new one must finish.
		echo "PLAIN $v $seeds seeds: over the alarm ($alarm s)"
		[ "$v" = old ] || { echo "$v: plain run over the alarm"; rc=1; }
	else
		[ "$prc" = 0 ] || { echo "$v: plain run failed rc=$prc"; rc=1; }
		echo "PLAIN $v $seeds seeds: $wall s wall, $cpu s cpu"
	fi
	tail -n 1 "$OUT/$v.log"
	if [ -n "${BOUND:-}" ] && [ "$v" = new ] && python3 -c "import sys; sys.exit(0 if $wall > $BOUND else 1)"; then echo "$v: above BOUND=$BOUND s"; rc=1; fi
done
for v in ${INST-${vs[*]}}; do
	bounded "$alarm" "$OUT/$v-inst" "$seeds" > "$OUT/$v-inst.log" 2>&1; irc=$?
	echo "== $v instrumented"; cat "$OUT/$v-inst.log"
	if [ "$irc" = 142 ]; then
		echo "$v instrumented: over the alarm ($alarm s), no per-phase table"
		[ "$v" = old ] || { echo "$v: instrumented run over the alarm"; rc=1; }
		continue
	fi
	[ "$irc" = 0 ] || { echo "$v: instrumented run failed rc=$irc"; rc=1; }
	python3 -I - "$OUT/$v-inst.log" "${MAX_RATIO:-0}" <<'PY' || rc=1
import collections, re, sys
ev = collections.Counter()
for line in open(sys.argv[1]):
    m = re.match(r"TIME (.*?) \| (.*?)\s+[0-9.]+ s\s+(\d+) (?:events|steps)", line)
    if m and m[2] != "TOTAL seeds": ev[m[1]] += int(m[3])
if not ev: sys.exit("no per-phase rows in the instrumented log")
ref = next((v for k, v in ev.items() if k.startswith("default")), 0) or 1
worst = max(v / ref for v in ev.values())
for k, v in ev.items(): print(f"EVENTS {k:28} {v:10d}  {v / ref:5.2f}x default")
limit = float(sys.argv[2])
if limit and worst > limit: sys.exit(f"a profile runs {worst:.2f}x the default profile's events, above {limit}")
PY
done
exit $rc
