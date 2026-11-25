#!/usr/bin/env bash
# T9 (namespace-measure) runner. Rules: docs/perf/namespace-measure.md.
#
#   namespace-measure-t9.sh build                 release build of the harness (light)
#   namespace-measure-t9.sh prepare               the prepared 5M store, once (light)
#   namespace-measure-t9.sh round N ARM ...       one run of each ARM in order (exclusive); an ARM
#                                                 is `full` or a fraction (0.05, 0.25, 0.5, 1)
#
# $T9_BUDGET_S (optional) time-boxes a round: an ARM is skipped, and says so, when the seconds
# elapsed plus its estimate (1.15 x the slowest per-shape cost seen so far, shaped by the arm's
# cost weight) would pass the budget. Skipped arms are not run later.
# $CORPUS names the DTCORP01 corpus and $CORPUS_SHA256 its digest. Stores live under
# $T9_ROOT (default .scratch/namespace-measure-t9/<corpus name> in this checkout); every delta run clones
# the prepared store first (cp -c on APFS, cp -a elsewhere), outside the timed span.
set -euo pipefail
mode=$1
shift
build=${T9_BUILD:-$PWD/.scratch/namespace-measure-t9/build}
root=${T9_ROOT:-$PWD/.scratch/namespace-measure-t9/$(basename "${CORPUS:-corpus}" .bin)}
exe=$build/namespace_measure_t9
corpus=${CORPUS:?CORPUS names the corpus}
check_corpus() {
	local got
	got=$(shasum -a 256 "$corpus" | cut -d' ' -f1)
	[[ $got == "${CORPUS_SHA256:?}" ]] || { echo "corpus digest $got != $CORPUS_SHA256" >&2; exit 1; }
	echo "corpus $corpus sha256 $got"
}
clone() {
	if [[ $(uname) == Darwin ]]; then cp -c -R "$1" "$2"; else cp -a "$1" "$2"; fi
}
memory() {
	if [[ $(uname) == Darwin ]]; then
		echo "memory: $(vm_stat | awk '/Pages (free|inactive)/ {printf "%s %d MiB; ", $2, $NF * 4096 / 1048576}')$(sysctl -n vm.swapusage)"
	else
		free -m | sed -n 2p
	fi
}
# Relative cost of an arm (the full copy ~0.8, a delta 0.4 + 0.6 f), after the 100K smoke.
weight() {
	if [[ $1 == full ]]; then echo 0.8; else awk -v f="$1" 'BEGIN { print 0.4 + 0.6 * f }'; fi
}
echo "detent $(git rev-parse HEAD) $(uname -sm)"
case $mode in
build)
	mkdir -p "$(dirname "$build")"
	cmake -S . -B "$build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PROJECT_detent_INCLUDE="$PWD/docs/perf/namespace-measure-t9.cmake" \
		-DDETENT_BUILD_TESTS=OFF -DDETENT_BUILD_EXAMPLES=OFF -DDETENT_BUILD_BENCHMARKS=OFF -DDETENT_BUILD_AUTHORITY=OFF \
		>"$build.configure.log" 2>&1 || { tail -40 "$build.configure.log"; exit 1; }
	cmake --build "$build" --target namespace_measure_t9 -j4
	file "$exe"
	;;
prepare)
	check_corpus
	file "$exe"
	rm -rf "$root/prepared"
	"$exe" full "$corpus" "$root/prepared"
	du -sk "$root/prepared"
	;;
round)
	check_corpus
	file "$exe"
	n=$1
	shift
	[[ -d $root/prepared ]] || { echo "no prepared store: run prepare" >&2; exit 1; }
	base=0
	for arm in "$@"; do
		if [[ -n ${T9_BUDGET_S:-} ]]; then
			est=$(awk -v b="$base" -v w="$(weight "$arm")" 'BEGIN { printf "%d", b * w * 1.15 }')
			if ((SECONDS + est > T9_BUDGET_S)); then
				echo "skip arm $arm: $SECONDS s elapsed + $est s estimated > budget $T9_BUDGET_S s"
				continue
			fi
		fi
		began=$SECONDS
		rm -rf "$root/run"
		memory
		if [[ $arm == full ]]; then
			"$exe" full "$corpus" "$root/run"
		else
			clone "$root/prepared" "$root/run"
			"$exe" delta "$corpus" "$root/run" "$arm" 1009
		fi
		rm -rf "$root/run"
		base=$(awk -v b="$base" -v d=$((SECONDS - began)) -v w="$(weight "$arm")" 'BEGIN { x = d / w; print (x > b ? x : b) }')
		echo "arm $arm took $((SECONDS - began)) s; $SECONDS s elapsed"
	done
	;;
*)
	echo "unknown mode $mode" >&2
	exit 2
	;;
esac
