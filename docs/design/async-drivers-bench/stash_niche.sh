#!/usr/bin/env bash
# The rows of docs/design/stash-niche.md (stash_niche.cc):
#   stash_niche.sh test                  both arms against the checks, under ASan+UBSan and TSan
#   stash_niche.sh smoke                 every arm at both horizons, small, with its configuration
#   stash_niche.sh row HORIZON_MS N [PAIRS]   PAIRS (default 6) interleaved b/w pairs at each
#                                        P in $PRODUCERS (default "1 4 14"), then the summary;
#                                        $PAIR_START (default 0) numbers the first pair, so a
#                                        row split over jobs rotates its arm order as one
#   stash_niche.sh mem HORIZON_MS N      the structure's heap per key, each arm twice, at P = 1 and 14
# stash comes from $JOB_REF_STASH (a job's --ref stash=...) or $STASH_DIR. $BUILD_DIR holds the
# binaries and logs.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
mode=${1:?mode}
stash=${JOB_REF_STASH:-${STASH_DIR:?set STASH_DIR or pin --ref stash=...}}
build=${BUILD_DIR:-$here/build-stash-niche}
read -ra producers <<<"${PRODUCERS:-1 4 14}"
mkdir -p "$build"
echo "pin stash=$(git -C "$stash" rev-parse HEAD 2>/dev/null || echo "$stash")"
echo "host=$(uname -sm) cpu=\"$(sysctl -n machdep.cpu.brand_string 2>/dev/null || sed -n 's/^model name\t*: //p' /proc/cpuinfo | head -1)\" hw_threads=$(getconf _NPROCESSORS_ONLN) compiler=\"$(c++ --version | head -1)\""
cxx() {  # out flags...
	local out=$1
	shift
	c++ -std=c++20 -pthread -I"$here" -I"$stash" "$@" "$here/stash_niche.cc" -o "$build/$out"
	file "$build/$out" 2>/dev/null || true
}
case $mode in
test)
	cxx test-asan -O1 -g -UNDEBUG -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all
	cxx test-tsan -O1 -g -UNDEBUG -fsanitize=thread
	echo "sanitize=address,undefined"
	"$build/test-asan" nichetest
	echo "sanitize=thread"
	if [ "$(uname -s)" = Linux ]; then setarch "$(uname -m)" -R "$build/test-tsan" nichetest; else "$build/test-tsan" nichetest; fi
	exit 0
	;;
smoke | row)
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
log="$build/$mode.log"
: >"$log"
run() { "$build/$@" | tee -a "$log"; }
echo "start=$(date -u +%FT%TZ)"
case $mode in
smoke)
	for h in 1000 86400000; do
		for arm in b w; do run timing niche "$arm" 4 100000 "$h"; done
	done
	cxx count -O2 -DNDEBUG -DCOUNT_HEAP
	for arm in b w; do run count nichemem "$arm" 4 100000 86400000; done
	;;
row)
	h=${2:?horizon ms}
	n=${3:?keys}
	pairs=${4:-6}
	start=${PAIR_START:-0}
	for ((r = start; r < start + pairs; ++r)); do
		for ((k = 0; k < ${#producers[@]}; ++k)); do
			p=${producers[$k]}
			echo "pair=$r"
			if (((r + ${producers[$k]}) % 2 == 0)); then order="b w"; else order="w b"; fi
			for arm in $order; do run timing niche "$arm" "$p" "$n" "$h"; done
		done
	done
	;;
mem)
	h=${2:?horizon ms}
	n=${3:?keys}
	for rep in 0 1; do
		for p in 1 14; do
			for arm in b w; do run count nichemem "$arm" "$p" "$n" "$h"; done
		done
	done
	;;
esac
echo "end=$(date -u +%FT%TZ)"
python3 "$here/stash_niche_summary.py" "$log"
