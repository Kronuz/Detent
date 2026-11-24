#!/bin/bash
# A3: each mutant of core.h (tools/mutants.sh's and this quest's custom ones: pre-vote,
# a lone node raising the term, failover hand-off, time-keyed variants) built against the old
# (main's) and the new liveness harness, asserts-on -O2, each run as CI runs it (20 seeds).
# A mutant the old harness catches and the new one misses is a weakened oracle.
#   mutants.sh core|custom|baseline [FROM [TO]]
source "$(dirname "$0")/common.sh" || exit 1
list=$1; from=${2:-0}; to=${3:-999}; res="$OUT/mutants-$list-$from.txt"; work="$OUT/m"; mkdir -p "$work"
setup release || exit 1
apply() {  # apply FILE SEARCH REPLACE
	python3 -I - "$1" "$2" "$3" <<'PY'
import sys
p, a, b = sys.argv[1:4]
s = open(p).read()
if s.count(a) != 1: sys.exit("mutation point not found once: " + a[:80])
open(p, "w").write(s.replace(a, b, 1))
PY
}
names=(); searches=(); replaces=()
add() { names+=("$1"); searches+=("$2"); replaces+=("$3"); }
pv='prevote_ = hard_.term + 1; prevotes_ = {configuration_.local}; reset_election();'
ho='if (!transfer_ && witness_self()) {'
case $list in
core)
	eval "$(sed -n '/^mutants=(/,/^)/p' "$src/tools/mutants.sh")"
	for m in "${mutants[@]}"; do IFS=$'\t' read -r n a b <<<"$m"; add "$(echo "$n" | tr 'A-Z ' 'a-z-' | tr -cd 'a-z0-9-')" "$a" "$b"; done ;;
custom)
	add no-pre-vote 'if (quorum() == 1) { campaign(output); return; }' 'campaign(output); return;'
	add pre-vote-raises-own-term "$pv" "${pv/hard_.term + 1/++hard_.term + 1}"
	add pre-vote-ignores-leader-contact 'bool grant = request->term > hard_.term && !heard_from_leader() &&' 'bool grant = request->term > hard_.term &&'
	add pre-vote-counts-refusals 'if (!prevote_ || response.request_term != prevote_ || !response.granted ||' 'if (!prevote_ || response.request_term != prevote_ ||'
	add vote-ignores-leader-contact 'request && !request->transfer && heard_from_leader()) { return; }' 'request && false) { return; }'
	add lone-campaigns-after-15000ms "$pv" "if (now_ - leader_contact_ > 15000) { campaign(output); return; } $pv"
	add lone-campaigns-after-12-election-timeouts "$pv" "if (now_ - leader_contact_ > 12 * timing_.election_max) { campaign(output); return; } $pv"
	add witness-never-hands-off "$ho" 'if (false && witness_self()) {'
	add witness-hands-off-after-3-election-timeouts "$ho" 'if (!transfer_ && witness_self() && now_ - leadership_start_ >= 3 * timing_.election_max) {'
	add witness-hands-off-after-1500ms "$ho" 'if (!transfer_ && witness_self() && now_ - leadership_start_ >= 1500) {'
	add handoff-target-ignores-liveness 'const bool live = answer != answered_.end() && now_ - answer->second < timing_.election_max && answer->second > leadership_start_;' 'const bool live = true; (void)answer;'
	add no-check-quorum 'if (answering < quorum()) { follower(0, output); return; }' 'if (false) { follower(0, output); return; }' ;;
baseline) add none "" "" ;;
esac
one() {
	local i=$1 n=${names[$1]} d="$work/${names[$1]}"
	rm -rf "$d"; mkdir -p "$d"; cp -R "$src/include" "$d/include"
	[ -z "${searches[$i]}" ] || apply "$d/include/consensus/core.h" "${searches[$i]}" "${replaces[$i]}" || { echo "$n: does not apply"; return; }
	for v in old new; do cxx "$d/include" "$OUT/src/$v.cc" "$d/$v" > "$d/build-$v.log" 2>&1 || { echo "$n: build failed ($v)"; return; }; done
	local r=""
	for v in old new; do
		local t0=$SECONDS rc=0
		bounded 300 "$d/$v" 20 > "$d/$v.log" 2>&1 || rc=$?
		local first; first=$(grep -m1 -E 'FAIL|Assertion|terminate' "$d/$v.log" | cut -c1-140)
		r="$r | $v: $([ $rc = 0 ] && echo passes || echo "CAUGHT rc=$rc") $((SECONDS - t0)) s ${first:+[$first]}"
	done
	cmp -s <(sort "$d/old.log") <(sort "$d/new.log") && r="$r | same output lines" || r="$r | output lines DIFFER"
	echo "$n$r"
}
{
	for i in "${!names[@]}"; do
		[ "$i" -ge "$from" ] && [ "$i" -lt "$to" ] || continue
		one "$i" &
		while [ "$(jobs -rp | wc -l)" -ge 4 ]; do sleep 1; done
	done
	wait
} > "$res"
sort "$res" -o "$res"; cat "$res"
for n in $(command ls "$work"); do for v in old new; do [ -f "$work/$n/$v.log" ] && { echo "== $n $v"; grep -v '^ ' "$work/$n/$v.log" | grep -E 'FAIL|failures' | head -5; }; done; done
! grep -q 'old: CAUGHT.*new: passes' "$res"
