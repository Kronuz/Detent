#!/usr/bin/env bash
# usage, from the checkout root: spin_probe.sh MODE build, then spin_probe.sh MODE run in a
# strict hold (its own cores: CPU time under a light hold's contention measures the load, not the loop).
# MODE is bounded or unbounded. The build wedges the authority's stop loop for 2 s per stop (it keeps
# visiting a node with nothing in flight); unbounded also lifts the spin_visits bound. The run executes
# test_authority_restart_wake's four stops and fails when the process spends at least half the 8 s of
# wedged wall time on CPU.
set -u
mode=${1:?mode}
step=${2:-}
build=.scratch/loop-idle-waits-spin-probe-$mode
if [ "$step" = run ]; then
	cd "$build/tests/detent_authority" || exit 126
	exec /usr/local/bin/python3 -I - <<'PY'
import resource, subprocess, sys, time
began = time.monotonic()
subprocess.run(["./test_authority_restart_wake"])  # its 200 ms stop bound fails by design here
usage = resource.getrusage(resource.RUSAGE_CHILDREN)
cpu = usage.ru_utime + usage.ru_stime
print(f"spin probe: wall {time.monotonic() - began:.2f} s, cpu {cpu:.2f} s, voluntary switches {usage.ru_nvcsw}"
      " over 8 s of wedged stops")
sys.exit(0 if cpu < 4.0 else 1)
PY
fi
[ "$step" = build ] || { echo "usage: spin_probe.sh bounded|unbounded build|run" >&2; exit 126; }
mutate() { # FILE OLD NEW: replace exactly one occurrence, or exit 126
	/usr/local/bin/python3 -I - "$@" <<'PY' || exit 126
import sys
path, old, new = sys.argv[1:4]
text = open(path).read()
if text.count(old) != 1:
    sys.exit(f"{path}: {text.count(old)} matches")
open(path, "w").write(text.replace(old, new))
PY
}
mutate src/detent_authority/service.cc 'while (active || history_active || (storage_flight' \
	'const auto wedge_until = Clock::now() + std::chrono::seconds(2);
	while (Clock::now() < wedge_until || active || history_active || (storage_flight'
case $mode in
bounded) ;;
unbounded) mutate src/detent_authority/service.cc 'constexpr unsigned spin_visits = 64;' 'constexpr unsigned spin_visits = ~0u;' ;;
*) echo "unknown mode $mode" >&2; exit 126 ;;
esac
cmake -S . -B "$build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=/usr/bin/clang++ -DCMAKE_C_COMPILER=/usr/bin/clang \
	-DCMAKE_OSX_SYSROOT= -DDETENT_BUILD_AUTHORITY=ON -DDETENT_BUILD_RELAY=OFF -DDETENT_BUILD_MANAGED_AGENT=OFF \
	-DDETENT_BUILD_EXAMPLES=OFF -DDETENT_BUILD_BENCHMARKS=OFF -DDETENT_BUILD_TESTS=ON >/dev/null || exit 126
cmake --build "$build" -j4 --target test_authority_restart_wake >/dev/null || exit 126
