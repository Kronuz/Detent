#!/bin/bash
# Applies one mutant patch from tests/detent_local/mutants, builds the tree and runs the dense
# and paged tests, then DENSE_MUTANT_RUN if set. Exits 1 when a test kills the mutant, 0 when it
# survives, and 3 when the patch does not apply or the mutant does not build (no verdict).
set -uo pipefail

patch=${1:?mutant patch}
regex=${2:-^test_local_(dense|paged)_}
build=${DENSE_MUTANT_BUILD:-build-mutant}
git apply --check "$patch" && git apply "$patch" || exit 3
git diff
cmake -S . -B "$build" -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_COMPILER=/usr/bin/clang++ -DCMAKE_C_COMPILER=/usr/bin/clang \
    -DCMAKE_OSX_SYSROOT= -DDETENT_BUILD_MANAGED_AGENT=ON -DDETENT_BUILD_AUTHORITY=OFF \
    -DDETENT_BUILD_RELAY=OFF -DDETENT_BUILD_BENCHMARKS=OFF -DDETENT_BUILD_EXAMPLES=OFF || exit 3
cmake --build "$build" -j4 || exit 3
ctest --test-dir "$build" --output-on-failure --timeout 600 -R "$regex"
status=$?
if test "$status" -eq 0 && test -n "${DENSE_MUTANT_RUN:-}"; then
    bash -c "$DENSE_MUTANT_RUN"
    status=$?
fi
if test "$status" -ne 0; then
    echo "MUTANT KILLED: $patch (status $status)"
    exit 1
fi
echo "MUTANT SURVIVED: $patch"
exit 0
