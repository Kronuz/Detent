#!/usr/bin/env bash
# The rows of docs/design/wheel-evidence.md (wheel_vs_stash.cc):
#   wheel_vs_stash.sh test         every candidate against the reference model and the cross-thread
#                                  checks, under ASan+UBSan and under TSan, assertions on
#   wheel_vs_stash.sh smoke        every arm of every row once, small, with its configuration
#   wheel_vs_stash.sh single [R]   R interleaved rounds (default 5) of a a2 b c d e f at n = 1e3 and 1e6
#   wheel_vs_stash.sh cross [R]    R interleaved rounds of b b2 c d e f at P = 1, 4 and 14 producers
#   wheel_vs_stash.sh mem          bytes per entry and heap per arm (the counting build), twice
# stash comes from $JOB_REF_STASH (a job's --ref stash=...) or $STASH_DIR. $CANDIDATES narrows the
# arms (default "a b c d e f"; a2 and b2 follow a and b) and $PRODUCERS the cross row's P (default
# "1 4 14"). Each row prints its runs and then the medians, min-max and A/A band
# (wheel_vs_stash_summary.py).
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
mode=${1:?mode}
rounds=${2:-5}
stash=${JOB_REF_STASH:-${STASH_DIR:?set STASH_DIR or pin --ref stash=...}}
build=${BUILD_DIR:-$here/build-wheel-vs-stash}
read -ra candidates <<<"${CANDIDATES:-a b c d e f}"
read -ra producers <<<"${PRODUCERS:-1 4 14}"
mkdir -p "$build"
echo "pin stash=$(git -C "$stash" rev-parse HEAD 2>/dev/null || echo "$stash")"
echo "host=$(uname -sm) cpu=\"$(sysctl -n machdep.cpu.brand_string 2>/dev/null || sed -n 's/^model name\t*: //p' /proc/cpuinfo | head -1)\" hw_threads=$(getconf _NPROCESSORS_ONLN) compiler=\"$(c++ --version | head -1)\""
cxx() {  # out flags...
	local out=$1
	shift
	c++ -std=c++20 -pthread -I"$here" -I"$stash" "$@" "$here/wheel_vs_stash.cc" -o "$build/$out"
	file "$build/$out" 2>/dev/null || true
}
log="$build/$mode.log"
case $mode in
test)
	cxx test-asan -O1 -g -UNDEBUG -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all
	cxx test-tsan -O1 -g -UNDEBUG -fsanitize=thread
	echo "sanitize=address,undefined"
	"$build/test-asan" test
	echo "sanitize=thread"
	# Linux 6.x maps above TSan's address layout under full ASLR; one process runs without it
	if [ "$(uname -s)" = Linux ]; then setarch "$(uname -m)" -R "$build/test-tsan" test; else "$build/test-tsan" test; fi
	exit 0
	;;
smoke | single | cross)
	cxx timing -O2 -DNDEBUG
	;;
mem)
	cxx count -O2 -DNDEBUG -DCOUNT_HEAP
	;;
*)
	echo "unknown mode $mode" >&2
	exit 2
	;;
esac
run() { "$build/$@" | tee -a "$log"; }
: >"$log"
echo "start=$(date -u +%FT%TZ)"
case $mode in
smoke)
	for arm in "${candidates[@]}"; do
		run timing single "$arm" 1000
		run timing single "$arm" 1000000
	done
	for arm in "${candidates[@]}"; do [ "$arm" = a ] || run timing cross "$arm" 4 200000; done
	cxx count -O2 -DNDEBUG -DCOUNT_HEAP
	for arm in "${candidates[@]}"; do run count mem "$arm" 1000; done
	;;
single)
	arms=()
	for arm in "${candidates[@]}"; do arms+=("$arm"); [ "$arm" != a ] || arms+=(a2); done
	for ((r = 0; r < rounds; ++r)); do
		for ((k = 0; k < ${#arms[@]}; ++k)); do
			arm=${arms[$(((k + r) % ${#arms[@]}))]}
			echo "round=$r"
			run timing single "$arm" 1000
			run timing single "$arm" 1000000
		done
	done
	;;
cross)
	arms=()
	for arm in "${candidates[@]}"; do [ "$arm" = a ] || arms+=("$arm"); [ "$arm" != b ] || arms+=(b2); done
	for ((r = 0; r < rounds; ++r)); do
		for ((k = 0; k < ${#arms[@]}; ++k)); do
			arm=${arms[$(((k + r) % ${#arms[@]}))]}
			echo "round=$r"
			for p in "${producers[@]}"; do run timing cross "$arm" "$p" 1000000; done
		done
	done
	;;
mem)
	for r in 0 1; do
		for arm in "${candidates[@]}"; do
			run count mem "$arm" 1000
			run count mem "$arm" 1000000
		done
	done
	;;
esac
echo "end=$(date -u +%FT%TZ)"
python3 "$here/wheel_vs_stash_summary.py" "$log"
