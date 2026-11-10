#!/bin/bash
# Runs the six 500K coordinator subscription cases ROUNDS times at -j4 in BUILD (configured when
# it has no cache, always rebuilt) and counts failing cases without stopping.
# Exits 0 when every round passes, 8 when any case failed, 3 when setup fails.
# REPEAT_500K_VERBOSE=1 adds ctest -V, which prints every run's fixture timeline.
set -uo pipefail

rounds=${1:?rounds}
build=${2:-.scratch/build}
regex='^test_delivery_coordinator_subscription-(500000|relay-500000|relay-sessions-500000|relay-paged-500000|relay-paged-migrate-500000|relay-paged-transitions-500000)$'
mkdir -p .scratch
log=.scratch/repeat-500k.log
exec > >(tee "$log") 2>&1
# A reused slot or mutant build keeps its cache; the build is incremental, never skipped.
if test ! -f "$build/CMakeCache.txt"; then
    cmake -S . -B "$build" -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_CXX_COMPILER=/usr/bin/clang++ -DCMAKE_C_COMPILER=/usr/bin/clang \
        -DCMAKE_OSX_SYSROOT= -DDETENT_BUILD_AUTHORITY=OFF -DDETENT_BUILD_RELAY=OFF \
        -DDETENT_BUILD_EXAMPLES=OFF -DDETENT_BUILD_BENCHMARKS=OFF -DDETENT_BUILD_TESTS=ON || exit 3
fi
cmake --build "$build" -j4 --target test_delivery_coordinator || exit 3
# ctest -N also lists the TLS certificate fixture the cases require.
cases=$(ctest --test-dir "$build" -N -R "$regex" | grep -c ': test_delivery_coordinator_subscription-')
test "$cases" -eq 6 || { echo "expected 6 cases, found $cases"; exit 3; }
failed=0
for ((round = 1; round <= rounds; round++)); do
    ctest --test-dir "$build" --output-on-failure ${REPEAT_500K_VERBOSE:+-V} -j4 --timeout 600 -R "$regex"
    status=$?
    test "$status" -eq 0 -o "$status" -eq 8 || { echo "ctest exited $status"; exit 3; }
    test "$status" -eq 0 || failed=$((failed + 1))
    echo "REPEAT round $round exit $status"
done
runs=$(grep -cE '^[[:space:]]+[0-9]+ - test_delivery_coordinator_subscription-' "$log")
echo "REPEAT SUMMARY: $failed of $rounds rounds failed; $runs of $((rounds * 6)) case runs failed"
grep -E '^[[:space:]]+[0-9]+ - test_delivery_coordinator_subscription-' "$log" |
    sed -E 's/^[[:space:]]+[0-9]+ - //' | sort | uniq -c
grep -ohE '^fixture server: .*|^coordinator invariant at line [0-9]+' "$log" | sort | uniq -c
test "$failed" -eq 0 || exit 8
