#!/usr/bin/env bash
# usage, from the checkout root: compare.sh build|idle|paths|stop [BASELINE_CHECKOUT]
#   build (light, a job's --build): detent-authority and detent-relay (Release) here and, when given, at
#         BASELINE_CHECKOUT.
#   idle  (A1, strict): idle_loops.py, three rounds alternating head and baseline, with the relay's
#         sync interval IDLE_INTERVAL_MS (default 60000; the configuration's default is 250).
#   paths (A3, strict): paths.py, five rounds alternating head and baseline.
#   stop  (F13, light): stop_scale.py, three-voter stop times at STOP_KEYS (default 100000) keys, three
#         rounds alternating head and baseline; fails when head's slowest stop exceeds 2x main's + 0.5 s.
set -eu
mode=${1:?build, idle or paths}
shift
here=bench/idle-wakeups
build() { # SOURCE BUILD
	cmake -S "$1" -B "$2" -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=/usr/bin/clang++ \
		-DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_OSX_SYSROOT= -DDETENT_BUILD_AUTHORITY=ON -DDETENT_BUILD_RELAY=ON \
		-DDETENT_BUILD_EXAMPLES=OFF -DDETENT_BUILD_BENCHMARKS=OFF -DDETENT_BUILD_TESTS=OFF >/dev/null
	cmake --build "$2" -j4 --target detent-authority detent-relay >/dev/null
}
case $mode in
build)
	mkdir -p .scratch
	build . .scratch/idle-head
	/usr/bin/clang -O2 -o .scratch/idle-usage "$here/usage.c"
	if [ $# -ge 1 ]; then
		build "$1" .scratch/idle-base
	fi
	exit 0 ;;
idle)
	rounds=3
	run() { /usr/local/bin/python3 "$here/idle_loops.py" --build "$1" --usage .scratch/idle-usage --label "$2" \
		--interval-ms "${IDLE_INTERVAL_MS:-60000}"; } ;;
paths)
	rounds=5
	run() { /usr/local/bin/python3 "$here/paths.py" --build "$1" --label "$2"; } ;;
stop)
	rounds=3
	rm -rf .scratch/stop-scale
	run() { /usr/local/bin/python3 "$here/stop_scale.py" --build "$1" --label "$2" --out .scratch/stop-scale \
		--keys "${STOP_KEYS:-100000}"; } ;;
*)
	echo "unknown mode $mode" >&2
	exit 2 ;;
esac
for round in $(seq 1 "$rounds"); do
	run .scratch/idle-head "head-$round"
	if [ $# -ge 1 ]; then
		run .scratch/idle-base "main-$round"
	fi
done
if [ "$mode" = stop ]; then
	/usr/local/bin/python3 "$here/stop_scale.py" --check --out .scratch/stop-scale
fi
