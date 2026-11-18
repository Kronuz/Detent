#!/usr/bin/env bash
# The stash-niche rows of docs/perf/wheel-vs-stash.md (stash_niche.cc): fire-and-forget keys over a
# horizon, Kronuz/wheel (w) against stash (b, its A/A twin b2).
#   stash_niche.sh test                  every arm against the checks, under ASan+UBSan and TSan
#   stash_niche.sh smoke                 every arm at both horizons, small, with its configuration
#   stash_niche.sh row HORIZON_MS N [R]  R interleaved rounds (default 40) of b b2 w at each P in
#                                        $PRODUCERS (default "1 4 14"), then the summary;
#                                        $ROUND_START (default 0) numbers the first round, so a row
#                                        split over jobs rotates its arm order as one
#   stash_niche.sh mem HORIZON_MS N      the structure's heap per key, each arm twice, at P = 1 and 14
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/wheel_vs_stash_common.sh"
mode=${1:?mode}
read -ra producers <<<"${PRODUCERS:-1 4 14}"
arms=(b b2 w)
host
if [ "$mode" = test ]; then
	sanitized_tests stash_niche nichetest
	exit 0
fi
case $mode in
smoke) build timing "-O2 -DNDEBUG" stash_niche && build count "-O2 -DNDEBUG -DCOUNT_HEAP" stash_niche ;;
row) build timing "-O2 -DNDEBUG" stash_niche ;;
mem) build count "-O2 -DNDEBUG -DCOUNT_HEAP" stash_niche ;;
*)
	echo "unknown mode $mode" >&2
	exit 2
	;;
esac
timing=$(bin timing stash_niche)
count=$(bin count stash_niche)
log=$build_root/niche-$mode.log
: >"$log"
run() { "$@" | tee -a "$log"; }
echo "start=$(date -u +%FT%TZ)"
case $mode in
smoke)
	for h in 1000 86400000; do
		for arm in "${arms[@]}"; do run "$timing" niche "$arm" 4 100000 "$h"; done
	done
	for arm in b w; do run "$count" nichemem "$arm" 4 100000 86400000; done
	;;
row)
	h=${2:?horizon ms}
	n=${3:?keys}
	rounds=${4:-40}
	start=${ROUND_START:-0}
	for ((r = start; r < start + rounds; ++r)); do
		for ((k = 0; k < ${#arms[@]}; ++k)); do
			arm=${arms[$(((k + r) % ${#arms[@]}))]}
			round "$r" | tee -a "$log"
			for p in "${producers[@]}"; do run "$timing" niche "$arm" "$p" "$n" "$h"; done
		done
	done
	;;
mem)
	h=${2:?horizon ms}
	n=${3:?keys}
	for rep in 0 1; do
		for p in 1 14; do
			for arm in b w; do run "$count" nichemem "$arm" "$p" "$n" "$h"; done
		done
	done
	;;
esac
echo "end=$(date -u +%FT%TZ)"
python3 "$here/stash_niche_summary.py" "$(summary_module)" "$log"
