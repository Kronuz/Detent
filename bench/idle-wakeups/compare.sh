#!/usr/bin/env bash
# usage, from the checkout root: compare.sh build|idle|paths|stop [BASELINE_CHECKOUT]
#   build (light, a job's --build): detent-authority and detent-relay (Release) here and, when given, at
#         BASELINE_CHECKOUT.
#   idle  (A1, strict): idle_loops.py, IDLE_ROUNDS (default 3) rounds alternating head and baseline,
#         IDLE_SECONDS (default 10) each, with the relay's sync interval IDLE_INTERVAL_MS (default
#         60000; the configuration's default is 250).
#   latency (relay-mirror-idle-wake A2, strict): relay_latency.py, LATENCY_ROUNDS (default 3) rounds
#         alternating head and baseline, LATENCY_SAMPLES (default 40) each, at LATENCY_INTERVAL_MS
#         (default 250); each build's own probe reads it, and `seal` goes only to a build that takes it.
#   paths (A3, strict): paths.py, five rounds alternating head and baseline.
#   stop  (F13, strict): stop_scale.py, three-voter stop times at STOP_KEYS (default 100000) keys,
#         STOP_ROUNDS (default 3) rounds alternating head and baseline; fails when head's slowest stop
#         exceeds twice main's plus 0.5 s.
set -eu
mode=${1:?build, idle, latency, paths or stop}
shift
here=bench/idle-wakeups
build() { # SOURCE BUILD
	cmake -S "$1" -B "$2" -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=/usr/bin/clang++ \
		-DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_OSX_SYSROOT= -DDETENT_BUILD_AUTHORITY=ON -DDETENT_BUILD_RELAY=ON \
		-DDETENT_BUILD_EXAMPLES=OFF -DDETENT_BUILD_BENCHMARKS=OFF -DDETENT_BUILD_TESTS=ON >/dev/null
	cmake --build "$2" -j4 --target detent-authority detent-relay "${@:3}" >/dev/null
}
# --agent-seal when SOURCE's W5 next takes `seal` (D163); a build before it refuses the field.
seal_flag() { grep -q '"seal"' "$1/src/detent_distribution_http/session_request.cc" && echo --agent-seal || true; }
case $mode in
build)
	mkdir -p .scratch
	build . .scratch/idle-head test_relay_probe
	/usr/bin/clang -O2 -o .scratch/idle-usage "$here/usage.c"
	if [ $# -ge 1 ]; then
		build "$1" .scratch/idle-base test_relay_probe
	fi
	exit 0 ;;
idle)
	rounds=${IDLE_ROUNDS:-3}
	run() { /usr/local/bin/python3 "$here/idle_loops.py" --build "$1" --usage .scratch/idle-usage --label "$2" \
		--interval-ms "${IDLE_INTERVAL_MS:-60000}" --seconds "${IDLE_SECONDS:-10}"; } ;;
latency)
	rounds=${LATENCY_ROUNDS:-3}
	run() { /usr/local/bin/python3 "$here/relay_latency.py" --build "$1" --label "$2" \
		--probe "$1/tests/detent_relay/test_relay_probe" --samples "${LATENCY_SAMPLES:-40}" \
		--interval-ms "${LATENCY_INTERVAL_MS:-250}" $(seal_flag "$3"); } ;;
paths)
	rounds=5
	run() { /usr/local/bin/python3 "$here/paths.py" --build "$1" --label "$2" $(seal_flag "$3"); } ;;
stop)
	rounds=${STOP_ROUNDS:-3}
	rm -rf .scratch/stop-scale
	run() { /usr/local/bin/python3 "$here/stop_scale.py" --build "$1" --label "$2" --out .scratch/stop-scale \
		--keys "${STOP_KEYS:-100000}"; } ;;
*)
	echo "unknown mode $mode" >&2
	exit 2 ;;
esac
for round in $(seq 1 "$rounds"); do
	run .scratch/idle-head "head-$round" .
	if [ $# -ge 1 ]; then
		run .scratch/idle-base "main-$round" "$1"
	fi
done
if [ "$mode" = stop ]; then
	/usr/local/bin/python3 "$here/stop_scale.py" --check --out .scratch/stop-scale
fi
