#!/usr/bin/env bash
# Runner for delta_plan_warm (test_delta_plan_scaling_warm.cc).
#
#   test_delta_plan_scaling_warm.sh build            release build of the harness (light)
#   test_delta_plan_scaling_warm.sh subcorpus N      derives $CORPUS's N-definition corpus into $W_ROOT
#   test_delta_plan_scaling_warm.sh prepare          the prepared store of $CORPUS, once (light)
#   test_delta_plan_scaling_warm.sh round N ARM ...  one process per ARM (0.05, c0.05, 0.5), each planning
#                                                    the delta in sequence AAB (BBA when N is even),
#                                                    on a fresh clone of the prepared store
#
# $CORPUS names the DTCORP01 corpus and $CORPUS_SHA256 its digest. Stores live under $W_ROOT (default
# .scratch/delta-plan-warm/<corpus name> in this checkout).
set -euo pipefail
trap 'echo "failed: line $LINENO: $BASH_COMMAND (exit $?)" >&2' ERR
mode=$1
shift
here=$(cd "$(dirname "$0")" && pwd)
build=${W_BUILD:-$PWD/.scratch/delta-plan-warm/build}
exe=$build/delta_plan_warm
t9=$build/namespace_measure_t9
corpus=${CORPUS:-}
root=${W_ROOT:-$PWD/.scratch/delta-plan-warm/$(basename "${corpus:-none}" .bin)}
check_corpus() {
	local got
	: "${CORPUS:?CORPUS names the corpus}"
	got=$(shasum -a 256 "$corpus" | cut -d' ' -f1)
	[[ ${CORPUS_SHA256:-$got} == "$got" ]] || { echo "corpus digest $got != $CORPUS_SHA256" >&2; exit 1; }
	echo "corpus $corpus sha256 $got"
}
clone() {
	if [[ $(uname) == Darwin ]]; then cp -c -R "$1" "$2"; else cp -a "$1" "$2"; fi
}
echo "detent $(git rev-parse HEAD) $(uname -sm)"
case $mode in
build)
	mkdir -p "$(dirname "$build")"
	cmake -S . -B "$build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PROJECT_detent_INCLUDE="$here/test_delta_plan_scaling_warm.cmake" \
		-DDETENT_BUILD_TESTS=OFF -DDETENT_BUILD_EXAMPLES=OFF -DDETENT_BUILD_BENCHMARKS=OFF -DDETENT_BUILD_AUTHORITY=OFF \
		>"$build.configure.log" 2>&1 || { tail -40 "$build.configure.log"; exit 1; }
	cmake --build "$build" --target namespace_measure_t9 delta_plan_warm -j4
	file "$t9" "$exe"
	;;
subcorpus)
	check_corpus
	mkdir -p "$(dirname "$root")"
	python3 -I "$here/test_delta_plan_scaling_subcorpus.py" "$corpus" "$(dirname "$root")/corpus-${1}.bin" "$1"
	shasum -a 256 "$(dirname "$root")/corpus-${1}.bin"
	;;
prepare)
	check_corpus
	file "$t9"
	rm -rf "$root/prepared"
	"$t9" full "$corpus" "$root/prepared"
	du -sk "$root/prepared"
	;;
round)
	check_corpus
	file "$exe"
	n=$1
	shift
	[[ -d $root/prepared ]] || { echo "no prepared store: run prepare" >&2; exit 1; }
	sequence=AAB
	((n % 2 == 0)) && sequence=BBA
	for arm in "$@"; do
		began=$SECONDS
		rm -rf "$root/run"
		clone "$root/prepared" "$root/run"
		echo "round $n arm $arm plans $sequence"
		"$exe" "$corpus" "$root/run" "$arm" 1009 "$sequence"
		rm -rf "$root/run"
		echo "arm $arm took $((SECONDS - began)) s"
	done
	;;
*)
	echo "unknown mode $mode" >&2
	exit 2
	;;
esac
