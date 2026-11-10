#!/usr/bin/env bash
# T9 (namespace-measure) runner. Rules: docs/perf/namespace-measure.md.
#
#   namespace-measure-t9.sh build                 release build of the harness (light)
#   namespace-measure-t9.sh prepare               the prepared 5M store, once (light)
#   namespace-measure-t9.sh round N ARM ...       one run of each ARM in order (exclusive); an ARM
#                                                 is `full` or a fraction (0.05, 0.25, 0.5, 1)
#
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
	for arm in "$@"; do
		rm -rf "$root/run"
		memory
		if [[ $arm == full ]]; then
			"$exe" full "$corpus" "$root/run"
		else
			clone "$root/prepared" "$root/run"
			"$exe" delta "$corpus" "$root/run" "$arm" 1009
		fi
		rm -rf "$root/run"
	done
	;;
*)
	echo "unknown mode $mode" >&2
	exit 2
	;;
esac
