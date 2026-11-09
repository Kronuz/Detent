#!/bin/bash
# Applies one mutant patch from tests/detent_local/mutants, builds the tree, runs the matching
# tests and then DENSE_MUTANT_RUN if set. Exits 0 only when a test fails (the mutant is killed),
# 1 when every test passes (it survives) and 3 when there is no verdict: the patch does not apply,
# the mutant does not build, no test matches, or a step fails other than by a test failing.
set -uo pipefail

patch=${1:?mutant patch}
regex=${2:-^test_local_(dense|paged)_}
build=${DENSE_MUTANT_BUILD:-build-mutant}
no_verdict() {
    echo "NO VERDICT: $patch: $1"
    exit 3
}
git apply --check "$patch" && git apply "$patch" || no_verdict "patch does not apply"
git diff
cmake -S . -B "$build" -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_COMPILER=/usr/bin/clang++ -DCMAKE_C_COMPILER=/usr/bin/clang \
    -DCMAKE_OSX_SYSROOT= -DDETENT_BUILD_MANAGED_AGENT=ON -DDETENT_BUILD_AUTHORITY=OFF \
    -DDETENT_BUILD_RELAY=OFF -DDETENT_BUILD_BENCHMARKS=OFF -DDETENT_BUILD_EXAMPLES=OFF ||
    no_verdict "configure failed"
cmake --build "$build" -j4 || no_verdict "mutant does not build"
tests=$(ctest --test-dir "$build" -N -R "$regex" | sed -n 's/^Total Tests: //p')
test "${tests:-0}" -gt 0 || no_verdict "no test matches $regex"
ctest --test-dir "$build" --output-on-failure --timeout 600 -R "$regex"
status=$?
# ctest exits 8 when a test fails or times out; anything else is ctest's own failure.
if test "$status" -eq 8; then
    echo "MUTANT KILLED: $patch by ctest ($tests tests matching $regex)"
    exit 0
fi
test "$status" -eq 0 || no_verdict "ctest exited $status"
if test -n "${DENSE_MUTANT_RUN:-}"; then
    # The command exits 3 when its own setup fails; any other failure is a test failing.
    bash -c "$DENSE_MUTANT_RUN"
    status=$?
    test "$status" -ne 3 || no_verdict "DENSE_MUTANT_RUN setup failed"
    if test "$status" -ne 0; then
        echo "MUTANT KILLED: $patch by DENSE_MUTANT_RUN (status $status)"
        exit 0
    fi
fi
echo "MUTANT SURVIVED: $patch"
exit 1
