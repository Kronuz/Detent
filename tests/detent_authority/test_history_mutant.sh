#!/usr/bin/env bash
# usage, from the checkout root, in a light job: test_history_mutant.sh PATCH TARGET [ARG...]
# Applies one mutant patch (tests/detent_authority/test_history_mutant_*.patch), builds the test TARGET and
# runs it. Exits 0 when the test fails (the mutant is killed) and 1 when it passes or the mutant or its
# build does not apply, as `campaign job submit --fails-without` expects.
set -u
patch=${1:?usage: test_history_mutant.sh PATCH TARGET [ARG...]}
test=${2:?target}
shift 2
build=.scratch/history-mutant-build
git apply --check "$patch" && git apply "$patch" || { echo "mutant does not apply"; exit 1; }
git diff --stat
trap 'git apply -R "$patch"' EXIT
cmake -S . -B "$build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=/usr/bin/clang++ -DCMAKE_C_COMPILER=/usr/bin/clang \
	-DCMAKE_OSX_SYSROOT= -DDETENT_BUILD_AUTHORITY=ON -DDETENT_BUILD_RELAY=ON -DDETENT_BUILD_MANAGED_AGENT=ON \
	-DDETENT_BUILD_EXAMPLES=OFF -DDETENT_BUILD_BENCHMARKS=OFF -DDETENT_BUILD_TESTS=ON >/dev/null || { echo "configure failed"; exit 1; }
cmake --build "$build" -j4 --target "$test" >/dev/null || { echo "build failed"; exit 1; }
binary=$(find "$build" -name "$test" -type f -perm -u+x | head -n 1)
[ -n "$binary" ] || { echo "no $test"; exit 1; }
"$binary" "$@"
status=$?
if [ "$status" -eq 0 ]; then echo "mutant $patch survived $test"; exit 1; fi
echo "mutant $patch killed by $test (status $status)"
exit 0
