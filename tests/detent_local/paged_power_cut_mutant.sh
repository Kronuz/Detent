#!/usr/bin/env bash
# usage, from the checkout root, in a light job: paged_power_cut_mutant.sh MUTANT
# Applies one agent store mutant, builds test_local_paged_publication_artifacts and runs it. The mutant
# counts as killed only when the test fails at one of its T7 cuts (MUTANT's own pattern), not at the
# counter or order checks the run makes after its cuts. Exits 1 when a cut kills it, 0 when it survives
# or fails elsewhere, and 126 when it does not apply or build (no verdict): `campaign job submit
# --fails-without` passes on a nonzero status but 124, 126, 127.
set -u
mutant=${1:?usage: paged_power_cut_mutant.sh MUTANT}
build=.scratch/power-cut-mutant-build
log=.scratch/power-cut-mutant.log
no_verdict() {
	echo "NO VERDICT: $mutant: $1"
	exit 126
}
trap 'git checkout -- src' EXIT
case "$mutant" in
# Adopts an orphan whose bytes match although no durable table reaches it, so its bytes are never synced:
# a kill that left them unsynced, a restart and a cut must catch it.
f2) git apply tests/detent_agent/mutants/f2-reuse-unreached.patch || no_verdict "patch does not apply"
	cut='kill, restart, power cut after event' ;;
# The owner group is never synced (job 261118-055016-95665a's mutant): a cut after the table's rename must
# catch it before the counters do.
owners-unsynced)
	perl -0pi -e 's/\t\ts\.sync_group\(group\);\n(\t\tprimitive\("sync owner records"\);)/$1/' src/detent_local/agent_store.cc
	cut='power cut after event' ;;
*) no_verdict "unknown mutant" ;;
esac
git diff --quiet -- src && no_verdict "mutant changed nothing"
git diff -- src
mkdir -p .scratch
cmake -S . -B "$build" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=/usr/bin/clang++ -DCMAKE_C_COMPILER=/usr/bin/clang \
	-DCMAKE_OSX_SYSROOT= -DDETENT_BUILD_AUTHORITY=OFF -DDETENT_BUILD_RELAY=OFF -DDETENT_BUILD_EXAMPLES=OFF \
	-DDETENT_BUILD_BENCHMARKS=OFF -DDETENT_BUILD_TESTS=ON >/dev/null || no_verdict "configure failed"
cmake --build "$build" -j4 --target test_local_paged_publication_artifacts || no_verdict "mutant does not build"
"$build/tests/detent_local/test_local_paged_publication_artifacts" >"$log" 2>&1
status=$?
tail -c 3000 "$log"
if test "$status" -eq 0; then
	echo "MUTANT SURVIVED: $mutant"
	exit 0
fi
if grep -q -- "$cut" "$log"; then
	echo "MUTANT KILLED BY A CUT: $mutant (test exit $status)"
	exit 1
fi
echo "MUTANT NOT KILLED BY A CUT: $mutant failed elsewhere (test exit $status)"
exit 0
