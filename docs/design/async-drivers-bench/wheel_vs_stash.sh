#!/usr/bin/env bash
# The rows of docs/design/wheel-evidence.md (wheel_vs_stash.cc):
#   wheel_vs_stash.sh test         every candidate against the reference model and the cross-thread
#                                  checks, under ASan+UBSan and under TSan, assertions on
#   wheel_vs_stash.sh smoke        every arm of every row once, small, with its configuration
#   wheel_vs_stash.sh single [R]   R interleaved rounds (default 5) of a a2 b c d at n = 1e3 and 1e6
#   wheel_vs_stash.sh cross [R]    R interleaved rounds of b b2 c d at P = 1, 4 and 14 producers
#   wheel_vs_stash.sh mem          bytes per entry and heap per arm (the counting build), twice
# stash comes from $JOB_REF_STASH (a job's --ref stash=...) or $STASH_DIR. Each row prints its
# runs and then the medians, min-max and A/A band (wheel_vs_stash_summary.py).
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
mode=${1:?mode}
rounds=${2:-5}
stash=${JOB_REF_STASH:-${STASH_DIR:?set STASH_DIR or pin --ref stash=...}}
build=${BUILD_DIR:-$here/build-wheel-vs-stash}
mkdir -p "$build"
echo "pin stash=$(git -C "$stash" rev-parse HEAD 2>/dev/null || echo "$stash")"
echo "host=$(uname -sm) cpu=\"$(sysctl -n machdep.cpu.brand_string 2>/dev/null || uname -p)\" compiler=\"$(c++ --version | head -1)\""
cxx() {  # out flags...
	local out=$1
	shift
	c++ -std=c++20 -pthread -I"$here" -I"$stash" "$@" "$here/wheel_vs_stash.cc" -o "$build/$out"
	file "$build/$out"
}
log="$build/$mode.log"
case $mode in
test)
	cxx test-asan -O1 -g -UNDEBUG -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all
	cxx test-tsan -O1 -g -UNDEBUG -fsanitize=thread
	echo "sanitize=address,undefined"
	"$build/test-asan" test
	echo "sanitize=thread"
	"$build/test-tsan" test
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
	for arm in a b c d; do
		run timing single $arm 1000
		run timing single $arm 1000000
	done
	for arm in b c d; do run timing cross $arm 4 200000; done
	cxx count -O2 -DNDEBUG -DCOUNT_HEAP
	for arm in a b c d; do run count mem $arm 1000; done
	;;
single)
	arms=(a a2 b c d)
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
	arms=(b b2 c d)
	for ((r = 0; r < rounds; ++r)); do
		for ((k = 0; k < ${#arms[@]}; ++k)); do
			arm=${arms[$(((k + r) % ${#arms[@]}))]}
			echo "round=$r"
			for p in 1 4 14; do run timing cross "$arm" "$p" 2000000; done
		done
	done
	;;
mem)
	for r in 0 1; do
		for arm in a b c d; do
			run count mem "$arm" 1000
			run count mem "$arm" 1000000
		done
	done
	;;
esac
echo "end=$(date -u +%FT%TZ)"
python3 "$here/wheel_vs_stash_summary.py" "$log"
