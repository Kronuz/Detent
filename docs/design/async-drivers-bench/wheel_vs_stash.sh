#!/usr/bin/env bash
# The rows of docs/perf/wheel-vs-stash.md (wheel_vs_stash.cc): Kronuz/wheel and stash, one build.
#   wheel_vs_stash.sh test         both arms against the reference model and the cross-thread
#                                  checks, under ASan+UBSan and under TSan, assertions on
#   wheel_vs_stash.sh smoke        every arm of every row once, small, with its configuration
#   wheel_vs_stash.sh single [R]   R interleaved rounds (default 40) of b b2 w at n = $SIZES
#                                  (default "1000 1000000")
#   wheel_vs_stash.sh cross [R]    R interleaved rounds (default 40) of b b2 w at P = $PRODUCERS
#                                  (default "1 4 14")
#   wheel_vs_stash.sh mem          bytes per entry and heap per arm (the counting build), twice
# Each round runs every arm once, in its own process and a rotated order. A row prints its runs
# and then the medians, the paired w/b ratios and the A/A band (wheel_vs_stash_summary.py).
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/wheel_vs_stash_common.sh"
mode=${1:?mode}
rounds=${2:-40}
read -ra sizes <<<"${SIZES:-1000 1000000}"
read -ra producers <<<"${PRODUCERS:-1 4 14}"
arms=(b b2 w)
host
log=$build_root/$mode.log
if [ "$mode" = test ]; then
	sanitized_tests wheel_vs_stash test
	exit 0
fi
case $mode in
smoke) build timing "-O2 -DNDEBUG" wheel_vs_stash && build count "-O2 -DNDEBUG -DCOUNT_HEAP" wheel_vs_stash ;;
single | cross) build timing "-O2 -DNDEBUG" wheel_vs_stash ;;
mem) build count "-O2 -DNDEBUG -DCOUNT_HEAP" wheel_vs_stash ;;
*)
	echo "unknown mode $mode" >&2
	exit 2
	;;
esac
timing=$(bin timing wheel_vs_stash)
count=$(bin count wheel_vs_stash)
: >"$log"
run() { "$@" | tee -a "$log"; }
echo "start=$(date -u +%FT%TZ)"
case $mode in
smoke)
	for arm in "${arms[@]}"; do
		run "$timing" single "$arm" 1000
		run "$timing" single "$arm" 1000000
		run "$timing" cross "$arm" 4 200000
	done
	for arm in b w; do run "$count" mem "$arm" 1000; done
	;;
single)
	for ((r = 0; r < rounds; ++r)); do
		for ((k = 0; k < ${#arms[@]}; ++k)); do
			arm=${arms[$(((k + r) % ${#arms[@]}))]}
			round "$r" | tee -a "$log"
			for n in "${sizes[@]}"; do run "$timing" single "$arm" "$n"; done
		done
	done
	;;
cross)
	for ((r = 0; r < rounds; ++r)); do
		for ((k = 0; k < ${#arms[@]}; ++k)); do
			arm=${arms[$(((k + r) % ${#arms[@]}))]}
			round "$r" | tee -a "$log"
			for p in "${producers[@]}"; do run "$timing" cross "$arm" "$p" 1000000; done
		done
	done
	;;
mem)
	for r in 0 1; do
		for arm in b w; do
			for n in "${sizes[@]}"; do run "$count" mem "$arm" "$n"; done
		done
	done
	;;
esac
echo "end=$(date -u +%FT%TZ)"
python3 "$here/wheel_vs_stash_summary.py" "$(summary_module)" "$log"
